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

typedef struct Value Value;
typedef struct String String;

/* Map Structures */

typedef struct MapEntry {
    String *key;
    Value *value;
    struct MapEntry *next;
} MapEntry;

typedef struct Map {
    size_t size;
    size_t capacity;
    MapEntry **buckets;
} Map;

/* Map Creation */

Value *value_map(void);
Value *value_map_with_capacity(size_t capacity);

/* Map Properties */

size_t map_size(const Value *v);
size_t map_capacity(const Value *v);

/* Map Access */

Value *map_get(const Value *v, const char *key);
Value *map_set(Value *v, const char *key, Value *value);
bool map_has(const Value *v, const char *key);
Value *map_delete(Value *v, const char *key);
Value *map_clear(Value *v);

/* Map Iteration */

Value *map_keys(const Value *v);
Value *map_values(const Value *v);
Value *map_entries(const Value *v);

/* Internal (for GC and debugging) */

MapEntry *map_find_entry(const Value *v, const char *key);

/* GC Integration */

typedef struct Heap Heap;

void map_set_gc_heap(Heap *heap);
Heap *map_get_gc_heap(void);

#endif /* AGIM_TYPES_MAP_H */
