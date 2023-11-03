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

#include "./vmas.h"
#include "./util.h"

#define PM_PFRAME_BITS 55
#define PM_PFRAME_MASK ((1LL << PM_PFRAME_BITS) - 1)
#define PM_PRESENT (1ULL << 63)

#define PM_SOFT_DIRTY (1ULL << 55)

// defaults
struct arguments arguments = { 0, 1000, 10, 0 };

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

    struct timespec delay = TIMESPEC_FROM_MILLIS(arguments.delay);

    int res = parse_smaps(proc_smaps);
    if (res != 0) {
        perror("parse_smaps");
        return res;
    }

    // parse VMAs from /proc/<pid>/maps
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

            vmas[i].committed = 0;
            vmas[i].softdirty = 0;
            for (size_t j = 0; j < len; ++j) {
                if (!(pagemap[j] & PM_PRESENT))
                    continue;

                vmas[i].committed++;
                if (pagemap[j] & PM_SOFT_DIRTY) {
                    vmas[i].softdirty++;
                }
            }

            free(pagemap);

            total_reserved += len;
            total_committed += vmas[i].committed;
            total_softdirty += vmas[i].softdirty;
        }

        close(pagemap_fd);

        if (arguments.verbose) {
            for (size_t i = 0; i < num_vmas; ++i) {
                if (vmas[i].softdirty > 0)
                    printf("  VMA #%zu: R %zu, C %zu, D %zu (%.2f%%)\n", i,
                           vmas[i].end - vmas[i].start,
                           vmas[i].committed,
                           vmas[i].softdirty,
                           100.0 * vmas[i].softdirty / vmas[i].committed);
            }
        }

        printf("Reserved: %zu Pages, %s; ",
               total_reserved,
               format_size_string(total_reserved * g_system_pagesize));
        printf("Committed: %zu Pages, %s\n",
               total_committed,
               format_size_string(total_committed * g_system_pagesize));

        double persec = total_softdirty * 1000.0 / arguments.delay;
        printf("Dirty pages: %zu (%.2f%%) in %zu ms; %.0f/s\n",
               total_softdirty, 100.0 * total_softdirty / total_committed,
               arguments.delay, persec);

        for (size_t i = 0; i < num_vmas; ++i) {
            if (vmas[i].committed && vmas[i].softdirty >= vmas[i].committed) {
                fprintf(stderr, "warning: VMA #%zu: maxed out dirty pages!\n", i);
            }
        }
    }

    return 0;
}
