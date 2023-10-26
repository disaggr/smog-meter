/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <stddef.h>

char *format_size_string(size_t s);

char *makestr(const char *format, ...);

#endif  // _UTIL_H
