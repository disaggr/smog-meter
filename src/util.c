/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#include "./util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

char *format_size_string(size_t s) {
    static char *buffer = NULL;
    static size_t buflen = 0;
    static const char *units[] = { "Bytes", "KiB", "MiB", "GiB" };

    int unit = 0;
    while (unit < 4 && s && !(s % 1024)) {
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

char *makestr(const char *format, ...) {
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

int parse_smaps(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: ", path);
        perror("fopen");
        return 1;
    }

    int uses_hugepages = 0;

    char buffer[4096];
    int lines_read = 0;
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        lines_read++;

        char key[512];
        size_t value;

        int n = sscanf(buffer, "%s %zu", key, &value);
        if (n < 2)
            continue;

        for (size_t i = 0; i < strlen(key); ++i) {
            key[i] = tolower(key[i]);
        }

        if (!strstr(key, "huge"))
            continue;

        if (value > 0) {
            uses_hugepages = 1;
            break;
        }
    }

    fclose(f);

    if (uses_hugepages) {
        fprintf(stderr, "warning: hugepages detected in VMA. measurements will be inaccurate!\n");
    }

    return 0;
}
