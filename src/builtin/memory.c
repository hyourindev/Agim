/*
 * Agim - Persistent Memory Store
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "builtin/memory.h"
#include "vm/value.h"
#include "util/alloc.h"
#include "util/hash.h"

#include <string.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

static MemoryEntry *memory_find(MemoryStore *store, const char *key) {
    size_t index = agim_hash_cstring(key) % store->capacity;
    MemoryEntry *entry = store->buckets[index];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

/*============================================================================
 * Memory Store Lifecycle
 *============================================================================*/

MemoryStore *memory_store_new(void) {
    MemoryStore *store = agim_alloc(sizeof(MemoryStore));
    store->capacity = 64;
    store->size = 0;
    store->buckets = agim_alloc(sizeof(MemoryEntry *) * store->capacity);
    memset(store->buckets, 0, sizeof(MemoryEntry *) * store->capacity);
    return store;
}

void memory_store_free(MemoryStore *store) {
    if (!store) return;

    for (size_t i = 0; i < store->capacity; i++) {
        MemoryEntry *entry = store->buckets[i];
        while (entry) {
            MemoryEntry *next = entry->next;
            agim_free(entry->key);
            value_free(entry->value);
            agim_free(entry);
            entry = next;
        }
    }
    agim_free(store->buckets);
    agim_free(store);
}

/*============================================================================
 * Memory Operations
 *============================================================================*/

Value *memory_get(MemoryStore *store, const char *key) {
    if (!store || !key) return NULL;

    MemoryEntry *entry = memory_find(store, key);
    if (entry) {
        return value_copy(entry->value);
    }
    return NULL;
}

bool memory_set(MemoryStore *store, const char *key, Value *value) {
    if (!store || !key) return false;

    MemoryEntry *existing = memory_find(store, key);

    if (existing) {
        /* Update existing entry */
        value_free(existing->value);
        existing->value = value_copy(value);
        return true;
    }

    /* Create new entry */
    MemoryEntry *entry = agim_alloc(sizeof(MemoryEntry));
    entry->key = strdup(key);
    entry->value = value_copy(value);

    /* Insert at bucket head */
    size_t index = agim_hash_cstring(key) % store->capacity;
    entry->next = store->buckets[index];
    store->buckets[index] = entry;
    store->size++;

    return true;
}

bool memory_delete(MemoryStore *store, const char *key) {
    if (!store || !key) return false;

    size_t index = agim_hash_cstring(key) % store->capacity;

    MemoryEntry *prev = NULL;
    MemoryEntry *entry = store->buckets[index];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                store->buckets[index] = entry->next;
            }
            agim_free(entry->key);
            value_free(entry->value);
            agim_free(entry);
            store->size--;
            return true;
        }
        prev = entry;
        entry = entry->next;
    }

    return false;
}

bool memory_has(MemoryStore *store, const char *key) {
    if (!store || !key) return false;
    return memory_find(store, key) != NULL;
}

void memory_clear(MemoryStore *store) {
    if (!store) return;

    for (size_t i = 0; i < store->capacity; i++) {
        MemoryEntry *entry = store->buckets[i];
        while (entry) {
            MemoryEntry *next = entry->next;
            agim_free(entry->key);
            value_free(entry->value);
            agim_free(entry);
            entry = next;
        }
        store->buckets[i] = NULL;
    }
    store->size = 0;
}

size_t memory_size(MemoryStore *store) {
    return store ? store->size : 0;
}
