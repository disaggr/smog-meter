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

#include "./vmas.h"
#include "./util.h"

#define PM_PFRAME_BITS 55
#define PM_PFRAME_MASK ((1LL << PM_PFRAME_BITS) - 1)
#define PM_PRESENT (1ULL << 63)

#define PM_SOFT_DIRTY (1ULL << 55)

#define write4(FD, BUF) do { \
    assert(sizeof(*(BUF)) == 4); \
    write((FD), (BUF), 4); } \
while(0)

#define write8(FD, BUF) do { \
    assert(sizeof(*(BUF)) == 8); \
    write((FD), (BUF), 8); } \
while(0)

// defaults
struct arguments arguments = { 0, 0, 1000, 0, 0, 0, NULL };

// globals
size_t g_system_pagesize = 0;

extern struct argp argp;

int main(int argc, char* argv[]) {
    // determine system characteristics
    g_system_pagesize = sysconf(_SC_PAGE_SIZE);

    // parse CLI options
    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    printf("SMOG dirty-rate meter\n");
    printf("  System page size:       %s\n", format_size_string(g_system_pagesize));
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

    // the sampling interval
    struct timespec delay = TIMESPEC_FROM_MILLIS(arguments.delay);

    // parse the smaps to warn about hugepages
    res = parse_smaps(proc_smaps);
    if (res != 0) {
        perror("parse_smaps");
        return res;
    }

    struct vma *vmas = NULL;
    size_t num_vmas = 0;

    while (1) {
        // clear all softdirty flags to iniate the measurement period
        int res = clear_softdirty(proc_clear_refs);
        if (res != 0) {
            perror("clear_softdirty");
            return res;
        }

        // delay
        res = nanosleep(&delay, NULL);
        if (res != 0) {
            perror("nanosleep");
            return res;
        }

        // update VMAs from /proc/<pid>/maps
        res = update_vmas(proc_maps, &vmas, &num_vmas);
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
        int pagemap_fd = open(proc_pagemap, O_RDONLY);
        if (pagemap_fd < 0) {
            fprintf(stderr, "%s: ", proc_pagemap);
            perror("open");
            return 1;
        }

        size_t total_reserved = 0;
        size_t total_committed = 0;
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
                perror("pread");
                return 1;
            }

            if (bytes > 0 && (size_t)bytes < len * sizeof(*pagemap)) {
                perror("pread (FIXME)");
                return 1;
            }

            vmas[i].committed = 0;
            vmas[i].softdirty = 0;
            for (size_t j = 0; j < (size_t)len; ++j) {
                if (!(pagemap[j] & PM_PRESENT))
                    continue;

                vmas[i].committed++;
                if (pagemap[j] & PM_SOFT_DIRTY) {
                    vmas[i].softdirty++;
                }
            }

            total_reserved += len;
            total_committed += vmas[i].committed;
            total_softdirty += vmas[i].softdirty;

            if (arguments.verbose
                    && (size_t)len >= arguments.min_vma_reserved
                    && vmas[i].committed >= arguments.min_vma_committed
                    && vmas[i].softdirty >= arguments.min_vma_dirty) {

                printf("  VMA #%zu: %#zx ... %#zx\n",
                       i, vmas[i].start, vmas[i].end);
                
                double persec = vmas[i].softdirty * 1000.0 / arguments.delay;
                printf("    - Reserved:  %zu Pages, %s\n",
                       len,
                       format_size_string(len * g_system_pagesize));
                printf("    - Committed: %zu Pages, %s\n",
                       vmas[i].committed,
                       format_size_string(vmas[i].committed * g_system_pagesize));
                printf("    - Softdirty: %zu Pages, %s in %zu ms (%.0f/s; %.2f%%)\n",
                       vmas[i].softdirty,
                       format_size_string(vmas[i].softdirty * g_system_pagesize),
                       arguments.delay, persec, 100.0 * vmas[i].softdirty / vmas[i].committed);

                if (arguments.verbose >= 2) {
                    for (size_t j = 0; j < (size_t)len; ++j) {
                        if (!(pagemap[j] & PM_PRESENT)) {
                            printf("_");
                        } else if (pagemap[j] & PM_SOFT_DIRTY) {
                            printf("#");
                        } else {
                            printf(".");
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

                uint32_t num_pages = len;
                write4(trace_fd, &num_pages);

                uint32_t flags = 0;
                size_t index = 0;
                for (size_t j = 0; j < (size_t)len; ++j) {
                    if (pagemap[j] & PM_PRESENT) {
                        flags |= 1 << index++;
                        flags |= ((pagemap[j] & PM_SOFT_DIRTY) != 0) << index++;
                    } else {
                        index += 2;
                    }
                    
                    if (index >= 32 || j == (size_t)len - 1) {
                        write4(trace_fd, &flags);
                        flags = 0;
                        index = 0;
                    }
                }

                fsync(trace_fd);
            }

            free(pagemap);
        }

        close(pagemap_fd);

        double persec = total_softdirty * 1000.0 / arguments.delay;
        printf("Reserved:  %zu Pages, %s\n",
               total_reserved,
               format_size_string(total_reserved * g_system_pagesize));
        printf("Committed: %zu Pages, %s\n",
               total_committed,
               format_size_string(total_committed * g_system_pagesize));
        printf("Softdirty: %zu Pages, %s in %zu ms (%.0f/s; %.2f%%)\n",
               total_softdirty,
               format_size_string(total_softdirty * g_system_pagesize),
               arguments.delay, persec, 100.0 * total_softdirty / total_committed);

        for (size_t i = 0; i < num_vmas; ++i) {
            if (vmas[i].committed && vmas[i].softdirty >= vmas[i].committed) {
                fprintf(stderr, "warning: VMA #%zu: maxed out dirty pages!\n", i);
            }
        }
    }

    close(trace_fd);

    return 0;
}
