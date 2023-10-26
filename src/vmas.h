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
    size_t softdirty;
};

int update_vmas(const char *path, struct vma **buf, size_t *len);

int clear_softdirty(const char *path);

#endif  // VMAS_H_
