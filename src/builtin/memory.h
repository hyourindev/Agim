/*
 * Agim - Persistent Memory Store
 *
 * Key-value storage for persistent agent memory.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_BUILTIN_MEMORY_H
#define AGIM_BUILTIN_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
typedef struct Value Value;

/*============================================================================
 * Memory Entry
 *============================================================================*/

typedef struct MemoryEntry {
    char *key;
    Value *value;
    struct MemoryEntry *next;
} MemoryEntry;

/*============================================================================
 * Memory Store
 *============================================================================*/

typedef struct MemoryStore {
    MemoryEntry **buckets;
    size_t capacity;
    size_t size;
} MemoryStore;

/*============================================================================
 * Memory API
 *============================================================================*/

/**
 * Create a new memory store.
 */
MemoryStore *memory_store_new(void);

/**
 * Free a memory store.
 */
void memory_store_free(MemoryStore *store);

/**
 * Get a value from the store.
 * Returns a copy of the value, or NULL if not found.
 */
Value *memory_get(MemoryStore *store, const char *key);

/**
 * Set a value in the store.
 * The value is copied.
 */
bool memory_set(MemoryStore *store, const char *key, Value *value);

/**
 * Delete a key from the store.
 */
bool memory_delete(MemoryStore *store, const char *key);

/**
 * Check if a key exists.
 */
bool memory_has(MemoryStore *store, const char *key);

/**
 * Clear all entries.
 */
void memory_clear(MemoryStore *store);

/**
 * Get number of entries.
 */
size_t memory_size(MemoryStore *store);

#endif /* AGIM_BUILTIN_MEMORY_H */
