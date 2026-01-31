/*
 * Agim - Memory Allocation Utilities
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_UTIL_ALLOC_H
#define AGIM_UTIL_ALLOC_H

#include <stddef.h>

/*
 * Align size to the given alignment (must be power of 2).
 */
static inline size_t align_size(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/*
 * Allocate memory with error handling.
 * Exits the program if allocation fails.
 */
void *agim_alloc(size_t size);

/**
 * Reallocate memory with error handling.
 * Exits the program if reallocation fails.
 */
void *agim_realloc(void *ptr, size_t size);

/**
 * Free memory (NULL-safe wrapper).
 */
void agim_free(void *ptr);

/*
 * Duplicate a string.
 * Returns NULL if str is NULL.
 */
char *agim_strdup(const char *str);

/*
 * Duplicate at most n bytes of a string.
 * Returns NULL if str is NULL.
 */
char *agim_strndup(const char *str, size_t n);

#endif /* AGIM_UTIL_ALLOC_H */
