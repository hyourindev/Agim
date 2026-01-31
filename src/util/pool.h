/*
 * Agim - Memory Pool
 *
 * Fixed-size block allocator for reducing fragmentation.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_UTIL_POOL_H
#define AGIM_UTIL_POOL_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

/* Configuration */

#define POOL_DEFAULT_CHUNK_SIZE 4096
#define POOL_DEFAULT_ALIGNMENT 8

/* Pool Structures */

typedef struct PoolFreeBlock {
    struct PoolFreeBlock *next;
} PoolFreeBlock;

typedef struct PoolChunk {
    struct PoolChunk *next;
    char data[];
} PoolChunk;

typedef struct MemoryPool {
    size_t block_size;
    size_t blocks_per_chunk;
    size_t chunk_size;

    PoolFreeBlock *free_list;
    PoolChunk *chunks;

    _Atomic(size_t) allocated_count;
    _Atomic(size_t) free_count;
    size_t chunk_count;

    pthread_mutex_t lock;
} MemoryPool;

/* Pool API */

void pool_init(MemoryPool *pool, size_t block_size);
void pool_free(MemoryPool *pool);
void *pool_alloc(MemoryPool *pool);
void pool_dealloc(MemoryPool *pool, void *ptr);

typedef struct PoolStats {
    size_t block_size;
    size_t allocated;
    size_t free;
    size_t chunks;
    size_t total_memory;
} PoolStats;

PoolStats pool_stats(const MemoryPool *pool);

/* Global Pools */

void pools_init(void);
void pools_free(void);
void *pools_alloc(size_t size);
void pools_dealloc(void *ptr, size_t size);

#endif /* AGIM_UTIL_POOL_H */
