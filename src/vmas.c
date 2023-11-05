/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include "./vmas.h"

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "./util.h"
#include "./smog-meter.h"

int update_vmas(const char *path, struct vma **buf, size_t *len) {
    // parse all VMAs from /proc/<pid>/maps
    struct vma *vmas = *buf;
    size_t num_vmas = *len;
    size_t i = 0;

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: ", path);
        perror("fopen");
        return 1;
    }

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
            fprintf(stderr, "%s:%d: unexpected line: \"%s\"\n", path, lines_read, buffer);
            return 1;
        }

        struct vma vma = {
            vm_start / g_system_pagesize,
            vm_end / g_system_pagesize,
            0, 0,
        };

        if (i >= num_vmas) {
            // add to the end, probably only used in first pass
            vmas = realloc(vmas, sizeof(*vmas) * (num_vmas + 1));
            if (!vmas) {
                perror("realloc");
                return 2;
            }
            vmas[i] = vma;
            if (arguments.verbose) {
                printf("  appended new VMA: #%zu: %#zx ... %#zx (%zu Pages, %s)\n",
                       i, vmas[i].start, vmas[i].end, vmas[i].end - vmas[i].start,
                       format_size_string((vmas[i].end - vmas[i].start) * g_system_pagesize));
            }
            num_vmas++;
            i++;
        } else if (vmas[i].start == vma.start) {
            // we have seen this one before, update end if necessary
            if (vmas[i].end != vma.end) {
                vmas[i].end = vma.end;
                if (arguments.verbose) {
                    printf("  updated VMA: #%zu: %#zx ... %#zx (%zu Pages, %s)\n",
                           i, vmas[i].start, vmas[i].end, vmas[i].end - vmas[i].start,
                           format_size_string((vmas[i].end - vmas[i].start) * g_system_pagesize));
                }
            }
            i++;
        } else if (vmas[i].end == vma.end) {
            // we also have seen this one before, update start if necessary
            if (vmas[i].start != vma.start) {
                vmas[i].start = vma.start;
                if (arguments.verbose) {
                    printf("  updated VMA: #%zu: %#zx ... %#zx (%zu Pages, %s)\n",
                           i, vmas[i].start, vmas[i].end, vmas[i].end - vmas[i].start,
                           format_size_string((vmas[i].end - vmas[i].start) * g_system_pagesize));
                }
            }
            i++;
        } else if (vmas[i].start > vma.start) {
            // this is a new insert
            vmas = realloc(vmas, sizeof(*vmas) * (num_vmas + 1));
            if (!vmas) {
                perror("realloc");
                return 2;
            }
            memmove(vmas + i + 1, vmas + i, sizeof(*vmas) * (num_vmas - i));
            vmas[i] = vma;
            if (arguments.verbose) {
                printf("  inserted new VMA: #%zu: %#zx ... %#zx (%zu Pages, %s)\n",
                       i, vmas[i].start, vmas[i].end, vmas[i].end - vmas[i].start,
                       format_size_string((vmas[i].end - vmas[i].start) * g_system_pagesize));
            }
            num_vmas++;
            i++;
        } else {
            // we lost one?
            if (arguments.verbose) {
                printf("  lost VMA: #%zu: %#zx ... %#zx (%zu Pages, %s)\n",
                       i, vmas[i].start, vmas[i].end, vmas[i].end - vmas[i].start,
                       format_size_string((vmas[i].end - vmas[i].start) * g_system_pagesize));
            }
            memmove(vmas + i, vmas + i + 1, sizeof(*vmas) * (num_vmas - i - 1));
            num_vmas--;
        }
    }

    fclose(f);

    // assert that no VMAs overlap
    for (size_t i = 0; i < num_vmas; ++i) {
        struct vma vma = vmas[i];

        if (vma.start >= vma.end) {
            fprintf(stderr, "error: VMA has zero or negative size\n");
            return -1;
        }

        if (i == 0)
            continue;

        struct vma pre = vmas[i - 1];

        if (pre.end > vma.start) {
            fprintf(stderr, "error: VMAs overlap\n");
            return -1;
        }
    }

    // // collapse VMAs into consecutive regions of virtual memory
    // struct vma *big_vmas = NULL;
    // size_t num_big_vmas = 0;

    // for (size_t i = 0; i < num_vmas; ++i) {
    //     if (num_big_vmas > 0 && vmas[i].start == big_vmas[num_big_vmas - 1].end) {
    //         big_vmas[num_big_vmas - 1].end = vmas[i].end;
    //         continue;
    //     }

    //     big_vmas = realloc(big_vmas, sizeof(*big_vmas) * (num_big_vmas + 1));
    //     if (!big_vmas) {
    //         perror("realloc");
    //         return 2;
    //     }
    //     big_vmas[num_big_vmas] = vmas[i];
    //     num_big_vmas++;
    // }

    // free(vmas);
    // vmas = big_vmas;
    // num_vmas = num_big_vmas;

    // if (arguments.verbose) {
    //     printf("\n");
    //     printf("Aggregated into %zu consecutive regions:\n", num_vmas);
    //     printf("\n");

    //     size_t total_reserved = 0;
    //     for (size_t i = 0; i < num_vmas; ++i) {
    //         total_reserved += vmas[i].end - vmas[i].start;
    //         printf("  #%zu: %#zx ... %#zx (%zu Pages, %s)\n",
    //                i, vmas[i].start, vmas[i].end, vmas[i].end - vmas[i].start,
    //                format_size_string((vmas[i].end - vmas[i].start) * g_system_pagesize));
    //     }

    //     printf("\n");
    //     printf("Total Reserved: %zu Pages, %s\n",
    //            total_reserved,
    //            format_size_string(total_reserved * g_system_pagesize));
    // }

    *buf = vmas;
    *len = num_vmas;

    return 0;
}

int clear_softdirty(const char *path) {
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

    close(fd);
    return 0;
}

