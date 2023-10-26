/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include "./vmas.h"

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "./util.h"
#include "./smog-meter.h"

int parse_vmas(FILE *f, struct vma **buf, size_t *len) {
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
            vm_start / g_system_pagesize,
            vm_end / g_system_pagesize,
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
                   format_size_string((vmas[i].end - vmas[i].start) * g_system_pagesize));
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
                   format_size_string((vmas[i].end - vmas[i].start) * g_system_pagesize));
        }

        printf("\n");
        printf("Total Reserved: %zu Pages, %s\n",
               total_reserved,
               format_size_string(total_reserved * g_system_pagesize));
    }

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

    return 0;
}

