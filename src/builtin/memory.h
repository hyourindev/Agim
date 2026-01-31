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

typedef struct Value Value;

/* Memory Entry */

typedef struct MemoryEntry {
    char *key;
    Value *value;
    struct MemoryEntry *next;
} MemoryEntry;

/* Memory Store */

typedef struct MemoryStore {
    MemoryEntry **buckets;
    size_t capacity;
    size_t size;
} MemoryStore;

/* Memory API */

MemoryStore *memory_store_new(void);
void memory_store_free(MemoryStore *store);
Value *memory_get(MemoryStore *store, const char *key);
bool memory_set(MemoryStore *store, const char *key, Value *value);
bool memory_delete(MemoryStore *store, const char *key);
bool memory_has(MemoryStore *store, const char *key);
void memory_clear(MemoryStore *store);
size_t memory_size(MemoryStore *store);

#endif /* AGIM_BUILTIN_MEMORY_H */
