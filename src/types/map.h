/*
 * Agim - Map Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_TYPES_MAP_H
#define AGIM_TYPES_MAP_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
typedef struct Value Value;
typedef struct String String;

/*============================================================================
 * Map Structures
 *============================================================================*/

/**
 * Hash map entry (separate chaining).
 */
typedef struct MapEntry {
    String *key;            /* Key string (owned) */
    Value *value;           /* Value pointer */
    struct MapEntry *next;  /* Next entry in chain */
} MapEntry;

/**
 * Hash map with separate chaining.
 */
typedef struct Map {
    size_t size;            /* Number of entries */
    size_t capacity;        /* Number of buckets */
    MapEntry **buckets;     /* Array of bucket pointers */
} Map;

/*============================================================================
 * Map Creation
 *============================================================================*/

/**
 * Create an empty map.
 */
Value *value_map(void);

/**
 * Create a map with preallocated buckets.
 */
Value *value_map_with_capacity(size_t capacity);

/*============================================================================
 * Map Properties
 *============================================================================*/

/**
 * Get the number of entries in a map.
 * Returns 0 if not a map.
 */
size_t map_size(const Value *v);

/**
 * Get the bucket capacity of a map.
 * Returns 0 if not a map.
 */
size_t map_capacity(const Value *v);

/*============================================================================
 * Map Access
 *============================================================================*/

/**
 * Get value by key.
 * Returns NULL if key not found or not a map.
 */
Value *map_get(const Value *v, const char *key);

/**
 * Set key-value pair.
 * Creates new entry if key doesn't exist.
 * Returns the (possibly new) Value to use after COW.
 */
Value *map_set(Value *v, const char *key, Value *value);

/**
 * Check if key exists.
 */
bool map_has(const Value *v, const char *key);

/**
 * Delete key from map.
 * Returns the (possibly new) Value to use after COW.
 */
Value *map_delete(Value *v, const char *key);

/**
 * Remove all entries.
 * Returns the (possibly new) Value to use after COW.
 */
Value *map_clear(Value *v);

/*============================================================================
 * Map Iteration
 *============================================================================*/

/**
 * Get array of all keys.
 */
Value *map_keys(const Value *v);

/**
 * Get array of all values.
 */
Value *map_values(const Value *v);

/**
 * Get array of [key, value] pairs.
 */
Value *map_entries(const Value *v);

/*============================================================================
 * Map Internal (for GC and debugging)
 *============================================================================*/

/**
 * Find entry by key.
 * Returns NULL if not found.
 */
MapEntry *map_find_entry(const Value *v, const char *key);

/*============================================================================
 * GC Integration
 *============================================================================*/

/* Forward declaration for Heap */
typedef struct Heap Heap;

/**
 * Set the thread-local heap for write barriers.
 * Called by VM at start of execution.
 */
void map_set_gc_heap(Heap *heap);

/**
 * Get the current thread-local heap.
 */
Heap *map_get_gc_heap(void);

#endif /* AGIM_TYPES_MAP_H */
