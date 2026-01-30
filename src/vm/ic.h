/*
 * Agim - Inline Cache
 *
 * Inline caching optimizes property lookups by caching the result of
 * expensive hash table searches. When the same object shape is accessed
 * repeatedly, the cache provides O(1) lookup instead of O(1) hash lookup.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_IC_H
#define AGIM_VM_IC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct Value Value;
typedef struct Map Map;

/*============================================================================
 * Inline Cache States
 *============================================================================*/

typedef enum ICState {
    IC_UNINITIALIZED,  /* Never used */
    IC_MONO,           /* Single shape cached (monomorphic) */
    IC_POLY,           /* Multiple shapes cached (polymorphic) */
    IC_MEGA,           /* Too many shapes, use slow path */
} ICState;

/*============================================================================
 * Inline Cache Entry
 *============================================================================*/

#define IC_MAX_ENTRIES 4  /* Max polymorphic entries before megamorphic */

typedef struct ICEntry {
    uint64_t shape_id;  /* Map identity (address or hash) */
    size_t bucket;      /* Cached bucket index for hash table */
} ICEntry;

/*============================================================================
 * Inline Cache
 *============================================================================*/

typedef struct InlineCache {
    ICState state;
    uint8_t count;                 /* Number of entries (for polymorphic) */
    ICEntry entries[IC_MAX_ENTRIES];
} InlineCache;

/*============================================================================
 * Inline Cache API
 *============================================================================*/

/**
 * Initialize an inline cache.
 */
void ic_init(InlineCache *ic);

/**
 * Look up a value in the map using the inline cache.
 *
 * @param ic      The inline cache
 * @param map     The map value to look up in
 * @param key     The key to look up
 * @param result  Output: the found value (or NULL if not found)
 * @return        true if cache hit (result is valid), false if cache miss
 */
bool ic_lookup(InlineCache *ic, Value *map, const char *key, Value **result);

/**
 * Update the inline cache after a cache miss.
 *
 * @param ic        The inline cache
 * @param map       The map that was looked up
 * @param bucket    The bucket index where the key was found
 */
void ic_update(InlineCache *ic, Value *map, size_t bucket);

/**
 * Get the shape ID for a map value.
 * Currently uses the map's address as a simple identity.
 */
uint64_t ic_shape_id(Value *map);

/**
 * Check if the cache is in megamorphic state (too many shapes).
 */
static inline bool ic_is_mega(const InlineCache *ic) {
    return ic->state == IC_MEGA;
}

/*============================================================================
 * Statistics (for debugging/profiling)
 *============================================================================*/

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
