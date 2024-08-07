/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include "./smog-meter.h"

#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "./vmas.h"
#include "./util.h"

#define PM_PFRAME_BITS 55
#define PM_PFN_MASK ((1LL << PM_PFRAME_BITS) - 1)
#define PM_PRESENT (1ULL << 63)

#define PM_SOFT_DIRTY (1ULL << 55)
#define PM_ACCESSED (1ULL << 57)  // using a free bit in the pte structure here

#define KPF_REFERENCED (1ULL << 6)

#define write4(FD, BUF) do { \
    assert(sizeof(*(BUF)) == 4); \
    ssize_t bytes = write((FD), (BUF), 4); \
    assert(bytes == 4); \
} while(0)

#define write8(FD, BUF) do { \
    assert(sizeof(*(BUF)) == 8); \
    ssize_t bytes = write((FD), (BUF), 8); \
    assert(bytes == 8); \
} while(0)

// defaults
struct arguments arguments = { -1, 0, 0, 1000, 0, 0, 0, 0, 0, 0, NULL, NULL };

// globals
size_t g_system_pagesize = 0;
size_t g_system_physical_pages = 0;

extern struct argp argp;

int main(int argc, char* argv[]) {
    // determine system characteristics
    g_system_pagesize = sysconf(_SC_PAGE_SIZE);
    g_system_physical_pages = sysconf(_SC_PHYS_PAGES);

    // parse CLI options
    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    printf("SMOG dirty-rate meter\n");
    printf("  System page size:       %s\n", format_size_string(g_system_pagesize));
    printf("  System physical pages:  %zu (%s)\n",
           g_system_physical_pages,
           format_size_string(g_system_physical_pages * g_system_pagesize));
    printf("Monitored PID:            %d\n", arguments.pid);

    // prepare tracefile
    int trace_fd = -1;
    if (arguments.tracefile) {
        trace_fd = open(arguments.tracefile, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (trace_fd < 0) {
            fprintf(stderr, "%s: ", arguments.tracefile);
            perror("open");
            return 1;
        }
    }

    // produce paths to various procfs files for the monitored process
    char *proc_pagemap = makestr("/proc/%d/pagemap", arguments.pid);
    if (!proc_pagemap) {
        perror("makestr");
        return 2;
    }
    char *proc_maps = makestr("/proc/%d/maps", arguments.pid);
    if (!proc_maps) {
        perror("makestr");
        return 2;
    }
    char *proc_smaps = makestr("/proc/%d/smaps", arguments.pid);
    if (!proc_smaps) {
        perror("makestr");
        return 2;
    }
    char *proc_clear_refs = makestr("/proc/%d/clear_refs", arguments.pid);
    if (!proc_clear_refs) {
        perror("makestr");
        return 2;
    }
    char *proc_cmdline = makestr("/proc/%d/cmdline", arguments.pid);
    if (!proc_cmdline) {
        perror("makestr");
        return 2;
    }

    // parse and output the process cmdline
    int cmdline_fd = open(proc_cmdline, O_RDONLY);
    if (cmdline_fd < 0) {
        fprintf(stderr, "%s: ", proc_cmdline);
        perror("open");
        return 1;
    }

    char cmdline_buf[512] = { 0 };
    int res = read(cmdline_fd, cmdline_buf, 512);
    if (res < 0) {
        fprintf(stderr, "%s: ", proc_cmdline);
        perror("read");
        return 1;
    }

    printf("Monitored Process:        %s\n", cmdline_buf);
    printf("\n");

    close(cmdline_fd);

    int mapping_fd;
    size_t mapping_sz;
    void *mapping;
    if (arguments.self_map) {
        printf("Mapping file:             %s\n", arguments.vma);

        mapping_fd = open(arguments.vma, O_RDONLY);
        if (mapping_fd < 0) {
            fprintf(stderr, "%s: ", arguments.vma);
            perror("open");
            return 1;
        }

        struct stat mapping_sb;
        int res = fstat(mapping_fd, &mapping_sb);
        if (res < 0) {
            fprintf(stderr, "%s: ", arguments.vma);
            perror("fstat");
            return 1;
        }

        mapping_sz = mapping_sb.st_size;

        mapping = mmap(NULL, mapping_sz, PROT_READ, MAP_SHARED, mapping_fd, 0);
        if (mapping == MAP_FAILED) {
            fprintf(stderr, "%s: ", arguments.vma);
            perror("mmap");
            return 1;
        }
    }

    int pagemap_fd = open(proc_pagemap, O_RDONLY);
    if (pagemap_fd < 0) {
        fprintf(stderr, "%s: ", proc_pagemap);
        perror("open");
        return 1;
    }

    int page_idle_fd = -1;
    if (arguments.track_accessed) {
        page_idle_fd = open("/sys/kernel/mm/page_idle/bitmap", O_RDWR);
        if (page_idle_fd < 0) {
            fprintf(stderr, "/sys/kernel/mm/page_idle/bitmap: ");
            perror("open");
            return 1;
        }
    }

    // the sampling interval
    struct timespec delay = TIMESPEC_FROM_MILLIS(arguments.delay);

    // parse the smaps to warn about hugepages
    res = parse_smaps(proc_smaps);
    if (res != 0) {
        fprintf(stderr, "%s: ", proc_smaps);
        perror("parse_smaps");
        return res;
    }

    struct vma *vmas = NULL;
    size_t num_vmas = 0;

    const size_t CHONK = 8;
    uint64_t *pfn_cache = NULL;
    uint64_t *idle_cache = NULL;
    uint64_t *idle_map = NULL;
    size_t idle_cache_capacity = 0;
    size_t idle_map_capacity = 0;

    size_t num_frames = 0;

    while (1) {
        // clear all softdirty flags to initiate the measurement period
        int res = clear_softdirty(proc_clear_refs);
        if (res != 0) {
            fprintf(stderr, "%s: ", proc_clear_refs);
            perror("clear_softdirty");
            return res;
        }

        // clear all tracked accessed bits
        if (arguments.track_accessed && idle_cache) {
            ssize_t wsize = pwrite(page_idle_fd, pfn_cache, idle_cache_capacity * 8, 0);
            if (wsize < 0) {
                fprintf(stderr, "/sys/kernel/mm/page_idle/bitmap: ");
                perror("pwrite");
                return 1;
            }
            while (wsize < (ssize_t)idle_cache_capacity * 8) {
                // fprintf(stderr, "/sys/kernel/mm/page_idle/bitmap: partial write %zi / %zu\n",
                //         wsize, idle_cache_capacity * 8);
                wsize -= wsize % 8;
                ssize_t _wsize = pwrite(page_idle_fd, pfn_cache + wsize / 8,
                                        idle_cache_capacity * 8 - wsize, wsize);
                if (_wsize < 0) {
                    if (errno = ENXIO) {
                        errno = 0;
                        break;
                    }
                    fprintf(stderr, "/sys/kernel/mm/page_idle/bitmap: ");
                    perror("pwrite");
                    return 1;
                }
                wsize += _wsize;
            }
            memset(pfn_cache, 0, idle_cache_capacity * 8);
            memset(idle_cache, 0, idle_cache_capacity * 8);
            memset(idle_map, 0, idle_map_capacity * 8);
        }

        // delay
        res = nanosleep(&delay, NULL);
        if (res != 0) {
            perror("nanosleep");
            return res;
        }

        // update VMAs from /proc/<pid>/maps
        res = update_vmas(proc_maps, &vmas, &num_vmas, arguments.vma);
        if (res != 0) {
            fprintf(stderr, "%s: ", proc_maps);
            perror("parse_vmas");
            return 1;
        }

        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm *ti = localtime(&tv.tv_sec);

        char time_buf[64] = { 0 };
        strftime(time_buf, 64, "%F_%T", ti);

        if (arguments.tracefile) {
            uint32_t sec = tv.tv_sec;
            uint32_t usec = tv.tv_usec;
            write4(trace_fd, &sec);
            write4(trace_fd, &usec);

            uint32_t nvmas = num_vmas;
            write4(trace_fd, &nvmas);
        }

        if (arguments.verbose) {
            printf("\n");
            printf("%s.%06lu - Parsed %zu VMAs from %s:\n",
                   time_buf, tv.tv_usec, num_vmas, proc_maps);
        } else {
            printf("%s.%06lu - Parsed %zu VMAs from %s\n",
                   time_buf, tv.tv_usec, num_vmas, proc_maps);
        }

        // walk pagemap for the aggregated regions
        size_t total_reserved = 0;
        size_t total_committed = 0;
        size_t total_accessed = 0;
        size_t total_softdirty = 0;

        for (size_t i = 0; i < num_vmas; ++i) {
            size_t start = vmas[i].start;
            size_t end = vmas[i].end;

            ssize_t len = end - start;
            off_t off = start;

            uint64_t *pagemap = calloc(len, sizeof(*pagemap));
            if (!pagemap) {
                perror("calloc");
                return 2;
            }
            ssize_t bytes = pread(pagemap_fd, pagemap,
                                  sizeof(*pagemap) * len,
                                  sizeof(*pagemap) * off);
            if (bytes < 0) {
                fprintf(stderr, "%s: ", proc_pagemap);
                perror("pread");
                return 1;
            }

            if (bytes > 0 && (size_t)bytes < len * sizeof(*pagemap)) {
                fprintf(stderr, "%s: partial read\n", proc_pagemap);
                return 1;
            }

            vmas[i].committed = 0;
            vmas[i].accessed = 0;
            vmas[i].softdirty = 0;
            for (size_t j = 0; j < (size_t)len; ++j) {
                if (!(pagemap[j] & PM_PRESENT))
                    continue;

                vmas[i].committed++;

                if (arguments.track_accessed) {
                    // extract pageframe number from the pte
                    size_t pfn_bit = pagemap[j] & PM_PFN_MASK;
                    size_t pfn_word = pfn_bit / 64;
                    uint64_t pfn_mask = 1ULL << (pfn_bit % 64);

                    size_t map_bit = pfn_word / CHONK;
                    size_t map_word = map_bit / 64;
                    uint64_t map_mask = 1ULL << (map_bit % 64);

                    // make sure the idle map has sufficient capacity
                    if (map_word >= idle_map_capacity) {
                        size_t new_capacity = map_word + 1;

                        idle_map = realloc(idle_map, new_capacity * 8);

                        if (!idle_map) {
                            perror("realloc");
                            return 2;
                        }

                        memset(idle_map + idle_map_capacity, 0,
                               (new_capacity - idle_map_capacity) * 8);

                        idle_map_capacity = new_capacity;
                    }

                    // make sure the pfn and idle caches have sufficient capacity
                    if (pfn_word >= idle_cache_capacity) {
                        size_t new_capacity = idle_map_capacity * 64 * CHONK;

                        // printf("reallocating idle_cache to %zu bytes\n", pfn_index * 8);
                        pfn_cache = realloc(pfn_cache, new_capacity * 8);
                        idle_cache = realloc(idle_cache, new_capacity * 8);

                        if (!pfn_cache || !idle_cache) {
                            perror("realloc");
                            return 2;
                        }

                        memset(pfn_cache + idle_cache_capacity, 0,
                               (new_capacity - idle_cache_capacity) * 8);
                        memset(idle_cache + idle_cache_capacity, 0,
                               (new_capacity - idle_cache_capacity) * 8);

                        idle_cache_capacity = new_capacity;
                    }

                    // mark the page in the pfn cache, used to clear idle bits later
                    pfn_cache[pfn_word] |= pfn_mask;

                    // read a chonk from the idle bitmap, if necessary
                    if (!(idle_map[map_word] & map_mask)) {
                        // printf("reading %zu bytes \n", CHONK * 64 * 8);
                        ssize_t rbytes = pread(page_idle_fd,
                                               idle_cache + map_bit * CHONK,
                                               CHONK * 8,
                                               map_bit * CHONK * 8);
                        if (rbytes < 0) {
                            fprintf(stderr, "/sys/kernel/mm/page_idle/bitmap: ");
                            perror("pread");
                            return 1;
                        }
                        if (rbytes < (ssize_t)CHONK * 8) {
                            fprintf(stderr, "/sys/kernel/mm/page_idle/bitmap: partial read");
                        }

                        idle_map[map_word] |= map_mask;
                    }

                    // translate the idle map into an accessed bit
                    pagemap[j] &= ~(PM_ACCESSED);
                    // printf("%#zx\n", idle_cache[pfn_index]);
                    if (!(idle_cache[pfn_word] & pfn_mask)) {
                        pagemap[j] |= PM_ACCESSED;
                    }
                }

                if (pagemap[j] & PM_ACCESSED) {
                    vmas[i].accessed++;
                }
                if (pagemap[j] & PM_SOFT_DIRTY) {
                    vmas[i].softdirty++;
                }
            }

            total_reserved += len;
            total_committed += vmas[i].committed;
            total_accessed += vmas[i].accessed;
            total_softdirty += vmas[i].softdirty;

            if (arguments.verbose
                    && (size_t)len >= arguments.min_vma_reserved
                    && vmas[i].committed >= arguments.min_vma_committed
                    && (!arguments.track_accessed || vmas[i].accessed >= arguments.min_vma_accessed)
                    && vmas[i].softdirty >= arguments.min_vma_dirty) {
                printf("  VMA #%zu: %#zx ... %#zx %s\n",
                       i, vmas[i].start, vmas[i].end, vmas[i].pathname);

                double persec = vmas[i].softdirty * 1000.0 / arguments.delay;
                printf("    - Reserved:  %zu Pages, %s\n",
                       len,
                       format_size_string(len * g_system_pagesize));
                printf("    - Committed: %zu Pages, %s\n",
                       vmas[i].committed,
                       format_size_string(vmas[i].committed * g_system_pagesize));
                if (arguments.track_accessed) {
                    printf("    - Accessed: %zu Pages, %s\n",
                           vmas[i].accessed,
                           format_size_string(vmas[i].accessed * g_system_pagesize));
                }
                printf("    - Softdirty: %zu Pages, %s in %zu ms (%.0f/s; %.2f%%)\n",
                       vmas[i].softdirty,
                       format_size_string(vmas[i].softdirty * g_system_pagesize),
                       arguments.delay, persec, 100.0 * vmas[i].softdirty / vmas[i].committed);

                if (arguments.verbose >= 2) {
                    for (size_t j = 0; j < (size_t)len; ++j) {
                        if (!(pagemap[j] & PM_PRESENT)) {
                            printf("_");
                        } else if (arguments.track_accessed && (pagemap[j] & PM_ACCESSED)
                                   && !(pagemap[j] & PM_SOFT_DIRTY)) {
                            printf("\e[0;32m#\e[0m");
                        } else if (arguments.track_accessed && !(pagemap[j] & PM_ACCESSED)
                                   && (pagemap[j] & PM_SOFT_DIRTY)) {
                            printf("\e[0;33m#\e[0m");
                        } else if (pagemap[j] & PM_SOFT_DIRTY) {
                            printf("\e[0;31m#\e[0m");
                        } else {
                            printf("#");
                        }
                    }
                    printf("\n");
                }
            }

            if (arguments.tracefile) {
                uint64_t addr_start = start;
                uint64_t addr_end = end;
                write8(trace_fd, &addr_start);
                write8(trace_fd, &addr_end);

                uint32_t name_length = strlen(vmas[i].pathname) + 1;
                write4(trace_fd, &name_length);
                ssize_t bytes = write(trace_fd, vmas[i].pathname, name_length);
                if (bytes < name_length) {
                    fprintf(stderr, "%s: partial write\n", arguments.tracefile);
                    return 1;
                }

                uint32_t flags = 0;
                size_t index = 0;
                for (size_t j = 0; j < (size_t)len; ++j) {
                    // tracefile is encoded as:
                    //   00 not present
                    //   01 idle
                    //   10 accessed
                    //   11 softdirty
                    //
                    // this loses some information where pages are dirty but
                    // not accessed, but given that these are caused by
                    // imprecise measurements and time drifting, it's probalby
                    // okay. still, as usual, here be dragons.

                    int v;
                    if (!(pagemap[j] & PM_PRESENT)) {
                        v = 0x0;
                    } else if (arguments.track_accessed && (pagemap[j] & PM_ACCESSED)
                               && !(pagemap[j] & PM_SOFT_DIRTY)) {
                        v = 0x2;
                    } else if (pagemap[j] & PM_SOFT_DIRTY) {
                        v = 0x3;
                    } else {
                        v = 0x1;
                    }

                    flags |= v << index;
                    index += 2;

                    if (index >= 32 || j == (size_t)len - 1) {
                        write4(trace_fd, &flags);
                        flags = 0;
                        index = 0;
                    }
                }
            }

            free(pagemap);
        }

        double persec = total_softdirty * 1000.0 / arguments.delay;
        printf("Reserved:  %zu Pages, %s\n",
               total_reserved,
               format_size_string(total_reserved * g_system_pagesize));
        printf("Committed: %zu Pages, %s\n",
               total_committed,
               format_size_string(total_committed * g_system_pagesize));
        if (arguments.track_accessed) {
            printf("Accessed: %zu Pages, %s\n",
                   total_accessed,
                   format_size_string(total_accessed * g_system_pagesize));
        }
        printf("Softdirty: %zu Pages, %s in %zu ms (%.0f/s; %.2f%%)\n",
               total_softdirty,
               format_size_string(total_softdirty * g_system_pagesize),
               arguments.delay, persec, 100.0 * total_softdirty / total_committed);

        if (arguments.verbose) {
            for (size_t i = 0; i < num_vmas; ++i) {
                if (vmas[i].committed && vmas[i].softdirty >= vmas[i].committed) {
                    fprintf(stderr, "warning: VMA #%zu: maxed out dirty pages!\n", i);
                }
            }
        }

        if (arguments.frames && ++num_frames >= arguments.frames)
            break;
    }

    close(trace_fd);

    return 0;
}
