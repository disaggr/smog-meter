/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

// fuzzying stress tests for smog-meter

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>

#include "./util.h"

#define RECURSION_DELAY 1  // ms

int phase1(size_t d) {
    printf("recursion depth is %zu\n", d);
    if (d > 100000)
        return 0;

    struct timespec delay = TIMESPEC_FROM_MILLIS(RECURSION_DELAY);
    nanosleep(&delay, NULL);

    phase1(d + 1);

    return 0;
}

#define MALLOC_DELAY 100  // ms
#define MALLOC_BUFFERS 32
#define MALLOC_MAX_SIZE 128  // 4th root(256MiB)

int phase2() {
    void *buffers[MALLOC_BUFFERS] = { 0 };

    while (1) {
        size_t bid = rand() % MALLOC_BUFFERS;
        free(buffers[bid]);

        size_t a = rand() % MALLOC_MAX_SIZE;
        size_t b = rand() % MALLOC_MAX_SIZE;
        size_t c = rand() % MALLOC_MAX_SIZE;
        size_t s = a * a * a * a + b * b * c;
        s /= 8;
        s += 1;
        s *= 8;
        buffers[bid] = malloc(s);

        printf("allocated %s at slot %zu\n", format_size_string(s), bid);

        uint64_t *p = buffers[bid];
        for (size_t i = 0; i < s / 8; ++i) {
            *p = 1;
        }

        struct timespec delay = TIMESPEC_FROM_MILLIS(MALLOC_DELAY);
        nanosleep(&delay, NULL);
    }
}

int main(void) {
    // phase 1: grow stack through recursion
    phase1(0);

    // phase 2: grow heap through allocations
    phase2();
}
