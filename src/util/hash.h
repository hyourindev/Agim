/*
 * Agim - Hash Utilities
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_UTIL_HASH_H
#define AGIM_UTIL_HASH_H

#include <stddef.h>
#include <stdint.h>

/*============================================================================
 * String Hashing (FNV-1a)
 *============================================================================*/

/**
 * Compute FNV-1a hash of a string with known length.
 */
size_t agim_hash_string(const char *str, size_t length);

/**
 * Compute FNV-1a hash of a null-terminated string.
 */
size_t agim_hash_cstring(const char *str);

/**
 * Combine two hashes.
 */
size_t agim_hash_combine(size_t h1, size_t h2);

#endif /* AGIM_UTIL_HASH_H */
