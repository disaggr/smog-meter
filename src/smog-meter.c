/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include "./smog-meter.h"

#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>

#define PM_PFRAME_BITS 55
#define PM_PFRAME_MASK ((1LL << PM_PFRAME_BITS) - 1)
#define PM_PRESENT (1ULL << 63)

#define PM_SOFT_DIRTY (1ULL << 55)

struct vma {
    size_t start;
    size_t end;
};

// defaults
struct arguments arguments = { 0, 0 };

static size_t system_pagesize;

extern struct argp argp;

static char *format_size_string(size_t s) {
    static char *buffer = NULL;
    static size_t buflen = 0;
    static const char *units[] = { "Bytes", "KiB", "MiB", "GiB" };

    int unit = 0;
    while (unit < 4 && !(s % 1024)) {
        unit++;
        s /= 1024;
    }

    size_t n = snprintf(buffer, buflen, "%zu %s", s, units[unit]);
    if (n >= buflen) {
        buflen = n + 1;
        buffer = realloc(buffer, buflen);
        if (!buffer) {
            return NULL;
        }

        snprintf(buffer, buflen, "%zu %s", s, units[unit]);
    }

    return buffer;
}

static char *makestr(const char *format, ...) {
    char *buf = NULL;
    size_t n = 0;

    va_list ap;
    va_start(ap, format);
    n = vsnprintf(buf, n, format, ap);
    va_end(ap);

    n++;

    buf = malloc(n);
    if (!buf) {
        perror("malloc");
        return NULL;
    }

    va_start(ap, format);
    vsnprintf(buf, n, format, ap);
    va_end(ap);

    return buf;
}

static int clear_softdirty(const char *path) {
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: ", path);
        perror("open");
        return 1;
    }
    const char buf[] = "4";

    int res = write(fd, buf, sizeof(buf));
    if (res < 0) {
        fprintf(stderr, "%s: ", path);
        perror("write");
        return 1;
    }

    return 0;
}

static int parse_vmas(FILE *f, struct vma **buf, size_t *len) {
    struct vma *vmas = NULL;
    size_t num_vmas = 0;

    char buffer[4096];
    int lines_read = 0;
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        lines_read++;

        size_t vm_start;
        size_t vm_end;
        off_t pgoff;
        int major, minor;
        char r, w, x, s;
        size_t ino;

        int n = sscanf(buffer, "%zx-%zx %c%c%c%c %zx %x:%x %lu",
                       &vm_start, &vm_end,
                       &r, &w, &x, &s,
                       &pgoff,
                       &major, &minor,
                       &ino);
        if (n < 10) {
            fprintf(stderr, "unexpected line: \"%s\"\n", buffer);
            return 1;
        }

        struct vma vma = {
            vm_start / system_pagesize,
            vm_end / system_pagesize,
        };

        vmas = realloc(vmas, sizeof(*vmas) * (num_vmas + 1));
        if (!vmas) {
            perror("realloc");
            return 2;
        }
        vmas[num_vmas] = vma;
        num_vmas++;
    }

    if (arguments.verbose) {
        printf("\n");
        printf("Parsed %zu VMAs:\n", num_vmas);
        printf("\n");

        size_t total_reserved = 0;
        for (size_t i = 0; i < num_vmas; ++i) {
            total_reserved += vmas[i].end - vmas[i].start;
            printf("  #%zu: %#zx ... %#zx (%zu Pages, %s)\n",
                   i, vmas[i].start, vmas[i].end, vmas[i].end - vmas[i].start,
                   format_size_string((vmas[i].end - vmas[i].start) * system_pagesize));
        }
    }

    // collapse VMAs into consecutive regions of virtual memory
    struct vma *big_vmas = NULL;
    size_t num_big_vmas = 0;

    for (size_t i = 0; i < num_vmas; ++i) {
        if (num_big_vmas > 0 && vmas[i].start == big_vmas[num_big_vmas - 1].end) {
            big_vmas[num_big_vmas - 1].end = vmas[i].end;
            continue;
        }

        big_vmas = realloc(big_vmas, sizeof(*big_vmas) * (num_big_vmas + 1));
        if (!big_vmas) {
            perror("realloc");
            return 2;
        }
        big_vmas[num_big_vmas] = vmas[i];
        num_big_vmas++;
    }

    free(vmas);
    vmas = big_vmas;
    num_vmas = num_big_vmas;

    if (arguments.verbose) {
        printf("\n");
        printf("Aggregated into %zu consecutive regions:\n", num_vmas);
        printf("\n");

        size_t total_reserved = 0;
        for (size_t i = 0; i < num_vmas; ++i) {
            total_reserved += vmas[i].end - vmas[i].start;
            printf("  #%zu: %#zx ... %#zx (%zu Pages, %s)\n",
                   i, vmas[i].start, vmas[i].end, vmas[i].end - vmas[i].start,
                   format_size_string((vmas[i].end - vmas[i].start) * system_pagesize));
        }

        printf("\n");
        printf("Total Reserved: %zu Pages, %s\n",
               total_reserved,
               format_size_string(total_reserved * system_pagesize));
    }

    *buf = vmas;
    *len = num_vmas;

    return 0;
}

int main(int argc, char* argv[]) {
    // determine system characteristics
    system_pagesize = sysconf(_SC_PAGE_SIZE);

    // parse CLI options
    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    printf("SMOG dirty-rate meter\n");
    printf("  System page size:       %s\n", format_size_string(system_pagesize));

    if (arguments.verbose) {
        printf("\n");
        printf("  Monitor PID:          %d\n", arguments.pid);
    }

    // produce paths to /proc/<pid>/pagemap and /proc/<pid>/maps
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
    char *proc_clear_refs = makestr("/proc/%d/clear_refs", arguments.pid);
    if (!proc_maps) {
        perror("makestr");
        return 2;
    }

    while (1) {
        int res = clear_softdirty(proc_clear_refs);
        if (res != 0) {
            perror("clear_softdirty");
            return res;
        }

        sleep(1);

        // parse VMAs from /proc/<pid>/maps
        FILE *maps = fopen(proc_maps, "r");
        if (!maps) {
            fprintf(stderr, "%s: ", proc_maps);
            perror("fopen");
            return 1;
        }

        struct vma *vmas = NULL;
        size_t num_vmas = 0;

        res = parse_vmas(maps, &vmas, &num_vmas);
        if (res != 0) {
            fprintf(stderr, "%s: ", proc_maps);
            perror("parse_vmas");
            return 1;
        }

        fclose(maps);

        // walk pagemap for the aggregated regions
        int pagemap_fd = open(proc_pagemap, O_RDONLY);
        if (pagemap_fd < 0) {
            fprintf(stderr, "%s: ", proc_pagemap);
            perror("open");
            return 1;
        }

        size_t total_committed = 0;
        size_t total_softdirty = 0;

        for (size_t i = 0; i < num_vmas; ++i) {
            size_t start = vmas[i].start;
            size_t end = vmas[i].end;

            ssize_t len = end - start;
            off_t off = start;

            uint64_t *pagemap = malloc(sizeof(*pagemap) * len);
            if (!pagemap) {
                perror("malloc");
                return 2;
            }
            ssize_t bytes = pread(pagemap_fd, pagemap,
                                  sizeof(*pagemap) * len,
                                  sizeof(*pagemap) * off);
            if (bytes < 0) {
                perror("pread");
                return 1;
            }

            if (bytes == 0) {
                continue;
            }

            if (bytes < len * sizeof(*pagemap)) {
                perror("pread (FIXME)");
                return 1;
            }

            for (size_t j = 0; j < len; ++j) {
                if (!(pagemap[j] & PM_PRESENT))
                    continue;

                total_committed++;
                if (pagemap[j] & PM_SOFT_DIRTY) {
                    total_softdirty++;
                }
            }
        }

        close(pagemap_fd);

        if (arguments.verbose) {
            printf("\n");
            printf("Total Committed: %zu Pages, %s\n",
                   total_committed,
                   format_size_string(total_committed * system_pagesize));
            printf("\n");
        }

        printf("Dirty pages: %zu /s\n", total_softdirty);

        free(vmas);
    }

    return 0;
}
