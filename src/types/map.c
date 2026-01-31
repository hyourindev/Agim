/*
 * Agim - Map Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "types/map.h"
#include "types/string.h"
#include "types/array.h"
#include "vm/value.h"
#include "vm/gc.h"
#include "util/alloc.h"
#include "util/hash.h"

#include <stdatomic.h>
#include <string.h>

void map_set_gc_heap(Heap *heap) {
    gc_set_current_heap(heap);
}

Heap *map_get_gc_heap(void) {
    return gc_get_current_heap();
}

/* Map Creation */

Value *value_map_with_capacity(size_t capacity) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;

    v->type = VAL_MAP;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    Map *map = agim_alloc(sizeof(Map));
    if (!map) {
        agim_free(v);
        return NULL;
    }
    map->size = 0;
    map->capacity = capacity > 0 ? capacity : 16;
    map->buckets = agim_alloc(sizeof(MapEntry *) * map->capacity);
    if (!map->buckets) {
        agim_free(map);
        agim_free(v);
        return NULL;
    }
    memset(map->buckets, 0, sizeof(MapEntry *) * map->capacity);

    v->as.map = map;
    return v;
}

Value *value_map(void) {
    return value_map_with_capacity(16);
}

/* Map Properties */

size_t map_size(const Value *v) {
    if (!v || v->type != VAL_MAP) return 0;
    return v->as.map->size;
}

size_t map_capacity(const Value *v) {
    if (!v || v->type != VAL_MAP) return 0;
    return v->as.map->capacity;
}

/* Internal Helpers */

/* Limit chain depth to protect against hash collision DoS */
#define MAP_MAX_CHAIN_DEPTH 16

static MapEntry *find_entry_internal(Map *map, const char *key, size_t key_hash) {
    size_t index = key_hash % map->capacity;
    MapEntry *entry = map->buckets[index];
    size_t depth = 0;

    while (entry && depth < MAP_MAX_CHAIN_DEPTH) {
        if (entry->key->hash == key_hash &&
            strcmp(entry->key->data, key) == 0) {
            return entry;
        }
        entry = entry->next;
        depth++;
    }

    return NULL;
}

static size_t chain_depth_at(Map *map, size_t index) {
    size_t depth = 0;
    MapEntry *entry = map->buckets[index];
    while (entry) {
        depth++;
        entry = entry->next;
    }
    return depth;
}

static void map_resize(Map *map, size_t new_capacity) {
    MapEntry **new_buckets = agim_alloc(sizeof(MapEntry *) * new_capacity);
    if (!new_buckets) return;  /* Keep using old buckets on allocation failure */
    memset(new_buckets, 0, sizeof(MapEntry *) * new_capacity);

    for (size_t i = 0; i < map->capacity; i++) {
        MapEntry *entry = map->buckets[i];
        while (entry) {
            MapEntry *next = entry->next;

            size_t new_index = entry->key->hash % new_capacity;
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;

            entry = next;
        }
    }

    agim_free(map->buckets);
    map->buckets = new_buckets;
    map->capacity = new_capacity;
}

static Value *map_ensure_writable(Value *v) {
    if (!v || v->type != VAL_MAP) return v;

    if (!value_needs_cow(v)) {
        return v;
    }

    /* COW: create new Value with cloned Map */
    Map *old = v->as.map;

    Value *new_v = agim_alloc(sizeof(Value));
    new_v->type = VAL_MAP;
    atomic_store_explicit(&new_v->refcount, 1, memory_order_relaxed);
    new_v->flags = 0;
    new_v->gc_state = 0;
    new_v->next = NULL;

    Map *new_map = agim_alloc(sizeof(Map));
    new_map->size = old->size;
    new_map->capacity = old->capacity;
    new_map->buckets = agim_alloc(sizeof(MapEntry *) * new_map->capacity);
    memset(new_map->buckets, 0, sizeof(MapEntry *) * new_map->capacity);

    for (size_t i = 0; i < old->capacity; i++) {
        MapEntry *src = old->buckets[i];
        MapEntry **dst = &new_map->buckets[i];

        while (src) {
            MapEntry *entry = agim_alloc(sizeof(MapEntry));

            size_t key_len = src->key->length;
            String *key_str = agim_alloc(sizeof(String) + key_len + 1);
            key_str->length = key_len;
            key_str->hash = src->key->hash;
            memcpy(key_str->data, src->key->data, key_len + 1);

            entry->key = key_str;
            entry->value = value_retain(src->value);
            entry->next = NULL;

            *dst = entry;
            dst = &entry->next;
            src = src->next;
        }
    }

    new_v->as.map = new_map;
    value_release(v);

    return new_v;
}

/* Map Access */

MapEntry *map_find_entry(const Value *v, const char *key) {
    if (!v || v->type != VAL_MAP) return NULL;
    Map *map = v->as.map;

    size_t key_hash = agim_hash_cstring(key);
    return find_entry_internal(map, key, key_hash);
}

Value *map_get(const Value *v, const char *key) {
    MapEntry *entry = map_find_entry(v, key);
    return entry ? entry->value : NULL;
}

Value *map_set(Value *v, const char *key, Value *value) {
    if (!v || v->type != VAL_MAP) return v;

    Value *writable = map_ensure_writable(v);
    if (!writable || writable->type != VAL_MAP) return writable;

    Map *map = writable->as.map;

    Heap *heap = gc_get_current_heap();
    if (heap) {
        gc_write_barrier(heap, writable, value);
    }

    size_t key_len = strlen(key);
    size_t key_hash = agim_hash_string(key, key_len);

    MapEntry *existing = find_entry_internal(map, key, key_hash);
    if (existing) {
        existing->value = value;
        return writable;
    }

    /* Resize if load factor > 0.7 */
    if (map->size * 10 > map->capacity * 7) {
        map_resize(map, map->capacity * 2);
    }

    /* Check chain depth for hash collision attack protection */
    size_t index = key_hash % map->capacity;
    size_t depth = chain_depth_at(map, index);

    if (depth >= MAP_MAX_CHAIN_DEPTH) {
        map_resize(map, map->capacity * 2);
        index = key_hash % map->capacity;
    }

    MapEntry *entry = agim_alloc(sizeof(MapEntry));
    if (!entry) return writable;

    String *key_str = agim_alloc(sizeof(String) + key_len + 1);
    if (!key_str) {
        agim_free(entry);
        return writable;
    }
    key_str->length = key_len;
    key_str->hash = key_hash;
    memcpy(key_str->data, key, key_len + 1);

    entry->key = key_str;
    entry->value = value;

    entry->next = map->buckets[index];
    map->buckets[index] = entry;
    map->size++;
    return writable;
}

bool map_has(const Value *v, const char *key) {
    return map_find_entry(v, key) != NULL;
}

Value *map_delete(Value *v, const char *key) {
    if (!v || v->type != VAL_MAP) return v;

    Value *writable = map_ensure_writable(v);
    if (!writable || writable->type != VAL_MAP) return writable;

    Map *map = writable->as.map;

    size_t key_len = strlen(key);
    size_t key_hash = agim_hash_string(key, key_len);
    size_t index = key_hash % map->capacity;

    MapEntry *prev = NULL;
    MapEntry *entry = map->buckets[index];

    while (entry) {
        if (entry->key->hash == key_hash &&
            strcmp(entry->key->data, key) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                map->buckets[index] = entry->next;
            }
            agim_free(entry->key);
            agim_free(entry);
            map->size--;
            return writable;
        }
        prev = entry;
        entry = entry->next;
    }
    return writable;
}

Value *map_clear(Value *v) {
    if (!v || v->type != VAL_MAP) return v;

    Value *writable = map_ensure_writable(v);
    if (!writable || writable->type != VAL_MAP) return writable;

    Map *map = writable->as.map;

    for (size_t i = 0; i < map->capacity; i++) {
        MapEntry *entry = map->buckets[i];
        while (entry) {
            MapEntry *next = entry->next;
            agim_free(entry->key);
            agim_free(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
    return writable;
}

/* Map Iteration */

Value *map_keys(const Value *v) {
    Value *result = value_array();
    if (!v || v->type != VAL_MAP) return result;
    Map *map = v->as.map;

    for (size_t i = 0; i < map->capacity; i++) {
        MapEntry *entry = map->buckets[i];
        while (entry) {
            result = array_push(result, value_string(entry->key->data));
            entry = entry->next;
        }
    }

    return result;
}

Value *map_values(const Value *v) {
    Value *result = value_array();
    if (!v || v->type != VAL_MAP) return result;
    Map *map = v->as.map;

    for (size_t i = 0; i < map->capacity; i++) {
        MapEntry *entry = map->buckets[i];
        while (entry) {
            result = array_push(result, entry->value);
            entry = entry->next;
        }
    }

    return result;
}

Value *map_entries(const Value *v) {
    Value *result = value_array();
    if (!v || v->type != VAL_MAP) return result;
    Map *map = v->as.map;

    for (size_t i = 0; i < map->capacity; i++) {
        MapEntry *entry = map->buckets[i];
        while (entry) {
            Value *pair = value_array_with_capacity(2);
            pair = array_push(pair, value_string(entry->key->data));
            pair = array_push(pair, entry->value);
            result = array_push(result, pair);
            entry = entry->next;
        }
    }

    return result;
}
