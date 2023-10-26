/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include "./smog-meter.h"

#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>

#include "./vmas.h"
#include "./util.h"

#define PM_PFRAME_BITS 55
#define PM_PFRAME_MASK ((1LL << PM_PFRAME_BITS) - 1)
#define PM_PRESENT (1ULL << 63)

#define PM_SOFT_DIRTY (1ULL << 55)

// defaults
struct arguments arguments = { 0, 0 };

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
                   format_size_string(total_committed * g_system_pagesize));
            printf("\n");
        }

        printf("Dirty pages: %zu /s\n", total_softdirty);

        free(vmas);
    }

    return 0;
}
