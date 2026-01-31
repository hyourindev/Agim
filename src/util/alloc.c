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

/* Thread-local error storage */
static _Thread_local AgimAllocError tls_last_error = AGIM_E_OK;

AgimAllocError agim_last_error(void) {
    return tls_last_error;
}

void agim_set_error(AgimAllocError err) {
    tls_last_error = err;
}

void agim_clear_error(void) {
    tls_last_error = AGIM_E_OK;
}

const char *agim_error_string(AgimAllocError err) {
    switch (err) {
    case AGIM_E_OK:             return "no error";
    case AGIM_E_NOMEM:          return "out of memory";
    case AGIM_E_OVERFLOW:       return "integer overflow";
    case AGIM_E_INVALID_ARG:    return "invalid argument";
    case AGIM_E_POOL_EXHAUSTED: return "pool allocator exhausted";
    case AGIM_E_IO:             return "I/O error";
    case AGIM_E_INTERNAL:       return "internal error";
    default:                    return "unknown error";
    }
}

void *agim_alloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        agim_set_error(AGIM_E_NOMEM);
    }
    return ptr;
}

void *agim_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        agim_set_error(AGIM_E_NOMEM);
    }
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
