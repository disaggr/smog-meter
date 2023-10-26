/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#ifndef _SMOG_METER_H
#define _SMOG_METER_H

#include <argp.h>
#include <sys/types.h>

struct arguments {
    pid_t pid;
    int verbose;
};

extern struct arguments arguments;

// globals
extern size_t g_system_pagesize;

#endif  // _SMOG_METER_H
