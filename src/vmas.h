/*
 * Copyright (c) 2022 - 2023 OSM Group @ HPI, University of Potsdam
 */

#ifndef VMAS_H_
#define VMAS_H_

#include <stddef.h>
#include <stdio.h>

struct vma {
    size_t start;
    size_t end;

    size_t committed;
    size_t accessed;
    size_t softdirty;

    char *pathname;
};

int update_vmas(const char *path, struct vma **buf, size_t *len, char *vma_filter);

int clear_softdirty(const char *path);

int clear_accessed(const char *path);

#endif  // VMAS_H_
