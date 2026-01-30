/*
 * Agim - Hash Utilities
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "util/hash.h"

/*
 * FNV-1a hash constants
 * http://www.isthe.com/chongo/tech/comp/fnv/
 */
#define FNV_OFFSET_BASIS 2166136261u
#define FNV_PRIME 16777619u

size_t agim_hash_string(const char *str, size_t length) {
    size_t hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < length; i++) {
        hash ^= (uint8_t)str[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

size_t agim_hash_cstring(const char *str) {
    size_t hash = FNV_OFFSET_BASIS;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= FNV_PRIME;
    }
    return hash;
}

size_t agim_hash_combine(size_t h1, size_t h2) {
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}
