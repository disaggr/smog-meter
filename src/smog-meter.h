/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#ifndef SMOG_METER_H_
#define SMOG_METER_H_

#include <argp.h>
#include <sys/types.h>
#include <stdint.h>

struct arguments {
    pid_t pid;
    uint64_t delay;
    size_t min_vma_size;
    int verbose;
};

extern struct arguments arguments;

// globals
extern size_t g_system_pagesize;

#endif  // SMOG_METER_H_
