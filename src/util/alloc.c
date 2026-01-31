/*
 * Agim - Memory Allocation Utilities
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "util/alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *agim_alloc(size_t size) {
    void *ptr = malloc(size);
    /* Return NULL on OOM - caller must handle gracefully */
    return ptr;
}

void *agim_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    /* Return NULL on OOM - original ptr is still valid */
    return new_ptr;
}

void agim_free(void *ptr) {
    free(ptr);
}

char *agim_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *dup = agim_alloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

char *agim_strndup(const char *str, size_t n) {
    if (!str) return NULL;
    char *dup = agim_alloc(n + 1);
    if (dup) {
        memcpy(dup, str, n);
        dup[n] = '\0';
    }
    return dup;
}
