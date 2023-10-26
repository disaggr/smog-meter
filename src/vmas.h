/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#ifndef _VMAS_H
#define _VMAS_H

#include <stddef.h>
#include <stdio.h>

struct vma {
    size_t start;
    size_t end;
};

int parse_vmas(FILE *f, struct vma **buf, size_t *len);

int clear_softdirty(const char *path);

#endif  // _VMAS_H
