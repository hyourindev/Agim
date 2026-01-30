/*
 * Agim - Memory Pool
 *
 * Fixed-size block allocator for reducing fragmentation and
 * improving allocation performance for small objects.
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

/*============================================================================
 * Memory Pool Configuration
 *============================================================================*/

#define POOL_DEFAULT_CHUNK_SIZE 4096  /* 4KB chunks */
#define POOL_DEFAULT_ALIGNMENT 8      /* 8-byte alignment */

/*============================================================================
 * Pool Structures
 *============================================================================*/

/* Free block in the pool (stored in the block itself) */
typedef struct PoolFreeBlock {
    struct PoolFreeBlock *next;
} PoolFreeBlock;

/* Memory chunk (holds multiple blocks) */
typedef struct PoolChunk {
    struct PoolChunk *next;
    char data[];  /* Flexible array of blocks */
} PoolChunk;

/* Memory pool for fixed-size allocations */
typedef struct MemoryPool {
    size_t block_size;          /* Size of each block (aligned) */
    size_t blocks_per_chunk;    /* Number of blocks per chunk */
    size_t chunk_size;          /* Total size of each chunk */

    PoolFreeBlock *free_list;   /* List of free blocks */
    PoolChunk *chunks;          /* List of allocated chunks */

    /* Statistics */
    _Atomic(size_t) allocated_count;
    _Atomic(size_t) free_count;
    size_t chunk_count;

    /* Thread safety */
    pthread_mutex_t lock;
} MemoryPool;

/*============================================================================
 * Pool API
 *============================================================================*/

/**
 * Initialize a memory pool for fixed-size blocks.
 * block_size: Size of each allocation (will be aligned)
 */
void pool_init(MemoryPool *pool, size_t block_size);

/**
 * Free all memory in a pool.
 */
void pool_free(MemoryPool *pool);

/**
 * Allocate a block from the pool.
 * Returns NULL if allocation fails.
 */
void *pool_alloc(MemoryPool *pool);

/**
 * Return a block to the pool.
 */
void pool_dealloc(MemoryPool *pool, void *ptr);

/**
 * Get pool statistics.
 */
typedef struct PoolStats {
    size_t block_size;
    size_t allocated;
    size_t free;
    size_t chunks;
    size_t total_memory;
} PoolStats;

PoolStats pool_stats(const MemoryPool *pool);

/*============================================================================
 * Global Pools (for common allocation sizes)
 *============================================================================*/

/**
 * Initialize global memory pools.
 * Call once at startup.
 */
void pools_init(void);

/**
 * Free global memory pools.
 * Call at shutdown.
 */
void pools_free(void);

/**
 * Allocate from appropriate global pool.
 * Falls back to malloc for sizes > max pool size.
 */
void *pools_alloc(size_t size);

/**
 * Free memory allocated from global pools.
 */
void pools_dealloc(void *ptr, size_t size);

#endif /* AGIM_UTIL_POOL_H */
