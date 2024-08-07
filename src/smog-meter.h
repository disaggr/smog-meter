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
    int self_map;
    int verbose;
    uint64_t delay;
    uint64_t frames;
    int track_accessed;

    size_t min_vma_reserved;
    size_t min_vma_committed;
    size_t min_vma_accessed;
    size_t min_vma_dirty;

    char *tracefile;
    char *vma;
};

extern struct arguments arguments;

// globals
extern size_t g_system_pagesize;
extern size_t g_system_physical_pages;

#endif  // SMOG_METER_H_
