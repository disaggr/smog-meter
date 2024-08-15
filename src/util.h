/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <stddef.h>

char *format_size_string(size_t s);

char *makestr(const char *format, ...);

int parse_smaps(const char *path);

#define TIMEVAL_FROM_MILLIS(M) { (M) / 1000, ((M) % 1000) * 1000 }

#define TIMESPEC_FROM_MILLIS(M) { (M) / 1000, ((M) % 1000) * 1000000 }

#define TIMEVAL_TO_TIMESPEC(TV) { (TV).tv_sec, (TV).tv_usec * 1000 }

#endif  // UTIL_H_
