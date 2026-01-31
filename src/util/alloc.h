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
 * Error codes for allocation failures and other operations.
 * Use agim_last_error() to retrieve the last error after
 * a function returns NULL or indicates failure.
 */
typedef enum AgimAllocError {
    AGIM_E_OK = 0,           /* No error */
    AGIM_E_NOMEM,            /* Out of memory (malloc/realloc failed) */
    AGIM_E_OVERFLOW,         /* Integer overflow in size calculation */
    AGIM_E_INVALID_ARG,      /* Invalid argument passed to function */
    AGIM_E_POOL_EXHAUSTED,   /* Pool allocator out of blocks */
    AGIM_E_IO,               /* I/O error */
    AGIM_E_INTERNAL,         /* Internal error */
} AgimAllocError;

/*
 * Get the last error code (thread-local).
 * Returns AGIM_E_OK if no error has occurred.
 */
AgimAllocError agim_last_error(void);

/*
 * Set the last error code (thread-local).
 * Used internally by allocation functions.
 */
void agim_set_error(AgimAllocError err);

/*
 * Clear the last error (set to AGIM_E_OK).
 */
void agim_clear_error(void);

/*
 * Get a human-readable string for an error code.
 */
const char *agim_error_string(AgimAllocError err);

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
