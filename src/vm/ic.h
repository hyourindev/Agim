/*
 * Agim - Inline Cache
 *
 * Caches property lookups for O(1) access on repeated map accesses.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_IC_H
#define AGIM_VM_IC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct Value Value;
typedef struct Map Map;

/* Inline Cache States */

typedef enum ICState {
    IC_UNINITIALIZED,
    IC_MONO,
    IC_POLY,
    IC_MEGA,
} ICState;

/* Inline Cache Entry */

#define IC_MAX_ENTRIES 8

typedef struct ICEntry {
    uint64_t shape_id;
    size_t bucket;
} ICEntry;

/* Inline Cache */

typedef struct InlineCache {
    ICState state;
    uint8_t count;
    ICEntry entries[IC_MAX_ENTRIES];
} InlineCache;

/* Cache hash function using multiplicative hashing for better distribution.
 * Uses a prime multiplier from Knuth's golden ratio (2^64 / phi). */
static inline size_t ic_hash(uintptr_t shape) {
    uint64_t h = (uint64_t)shape * 0x9E3779B97F4A7C15ULL;
    return (size_t)((h >> 61) ^ (h & (IC_MAX_ENTRIES - 1)));
}
#define IC_HASH(shape) ic_hash((uintptr_t)(shape))
#define IC_CACHE_MASK (IC_MAX_ENTRIES - 1)

/* Inline Cache API */

void ic_init(InlineCache *ic);
bool ic_lookup(InlineCache *ic, Value *map, const char *key, Value **result);
void ic_update(InlineCache *ic, Value *map, size_t bucket);
uint64_t ic_shape_id(Value *map);

static inline bool ic_is_mega(const InlineCache *ic) {
    return ic->state == IC_MEGA;
}

/* Statistics (Debug) */

#ifdef AGIM_DEBUG
typedef struct ICStats {
    size_t hits;
    size_t misses;
    size_t updates;
    size_t megamorphic_calls;
} ICStats;

void ic_stats_init(ICStats *stats);
void ic_stats_print(const ICStats *stats);
#endif

#endif /* AGIM_VM_IC_H */
