/*
 * Agim - Memory Pool
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "util/pool.h"

#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Helper Functions
 *============================================================================*/

static inline size_t align_size(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/*============================================================================
 * Pool Implementation
 *============================================================================*/

void pool_init(MemoryPool *pool, size_t block_size) {
    if (!pool) return;

    /* Ensure minimum block size for free list pointer */
    if (block_size < sizeof(PoolFreeBlock)) {
        block_size = sizeof(PoolFreeBlock);
    }

    pool->block_size = align_size(block_size, POOL_DEFAULT_ALIGNMENT);
    pool->chunk_size = POOL_DEFAULT_CHUNK_SIZE;
    pool->blocks_per_chunk = (pool->chunk_size - sizeof(PoolChunk)) / pool->block_size;

    pool->free_list = NULL;
    pool->chunks = NULL;

    atomic_store(&pool->allocated_count, 0);
    atomic_store(&pool->free_count, 0);
    pool->chunk_count = 0;

    pthread_mutex_init(&pool->lock, NULL);
}

void pool_free(MemoryPool *pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->lock);

    /* Free all chunks */
    PoolChunk *chunk = pool->chunks;
    while (chunk) {
        PoolChunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }

    pool->chunks = NULL;
    pool->free_list = NULL;
    pool->chunk_count = 0;

    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
}

/**
 * Allocate a new chunk and add blocks to free list.
 */
static bool pool_grow(MemoryPool *pool) {
    size_t total_size = sizeof(PoolChunk) + pool->block_size * pool->blocks_per_chunk;
    PoolChunk *chunk = malloc(total_size);
    if (!chunk) return false;

    /* Link into chunk list */
    chunk->next = pool->chunks;
    pool->chunks = chunk;
    pool->chunk_count++;

    /* Add all blocks to free list */
    char *block = chunk->data;
    for (size_t i = 0; i < pool->blocks_per_chunk; i++) {
        PoolFreeBlock *free_block = (PoolFreeBlock *)block;
        free_block->next = pool->free_list;
        pool->free_list = free_block;
        block += pool->block_size;
    }

    atomic_fetch_add(&pool->free_count, pool->blocks_per_chunk);
    return true;
}

void *pool_alloc(MemoryPool *pool) {
    if (!pool) return NULL;

    pthread_mutex_lock(&pool->lock);

    /* Grow if needed */
    if (!pool->free_list) {
        if (!pool_grow(pool)) {
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }
    }

    /* Pop from free list */
    PoolFreeBlock *block = pool->free_list;
    pool->free_list = block->next;

    pthread_mutex_unlock(&pool->lock);

    atomic_fetch_add(&pool->allocated_count, 1);
    atomic_fetch_sub(&pool->free_count, 1);

    return block;
}

void pool_dealloc(MemoryPool *pool, void *ptr) {
    if (!pool || !ptr) return;

    pthread_mutex_lock(&pool->lock);

    /* Push onto free list */
    PoolFreeBlock *block = (PoolFreeBlock *)ptr;
    block->next = pool->free_list;
    pool->free_list = block;

    pthread_mutex_unlock(&pool->lock);

    atomic_fetch_sub(&pool->allocated_count, 1);
    atomic_fetch_add(&pool->free_count, 1);
}

PoolStats pool_stats(const MemoryPool *pool) {
    PoolStats stats = {0};
    if (!pool) return stats;

    stats.block_size = pool->block_size;
    stats.allocated = atomic_load(&pool->allocated_count);
    stats.free = atomic_load(&pool->free_count);
    stats.chunks = pool->chunk_count;
    stats.total_memory = pool->chunk_count * pool->chunk_size;

    return stats;
}

/*============================================================================
 * Global Pools
 *============================================================================*/

/* Pool sizes: 16, 32, 64, 128, 256, 512 bytes */
#define NUM_GLOBAL_POOLS 6
static MemoryPool global_pools[NUM_GLOBAL_POOLS];
static const size_t pool_sizes[NUM_GLOBAL_POOLS] = {16, 32, 64, 128, 256, 512};
static bool pools_initialized = false;

void pools_init(void) {
    if (pools_initialized) return;

    for (int i = 0; i < NUM_GLOBAL_POOLS; i++) {
        pool_init(&global_pools[i], pool_sizes[i]);
    }
    pools_initialized = true;
}

void pools_free(void) {
    if (!pools_initialized) return;

    for (int i = 0; i < NUM_GLOBAL_POOLS; i++) {
        pool_free(&global_pools[i]);
    }
    pools_initialized = false;
}

/**
 * Find the appropriate pool for a given size.
 * Returns -1 if no suitable pool (use malloc).
 */
static int find_pool_index(size_t size) {
    for (int i = 0; i < NUM_GLOBAL_POOLS; i++) {
        if (size <= pool_sizes[i]) {
            return i;
        }
    }
    return -1;
}

void *pools_alloc(size_t size) {
    if (!pools_initialized) {
        pools_init();
    }

    int idx = find_pool_index(size);
    if (idx >= 0) {
        return pool_alloc(&global_pools[idx]);
    }

    /* Too large for pools, use malloc */
    return malloc(size);
}

void pools_dealloc(void *ptr, size_t size) {
    if (!ptr) return;

    int idx = find_pool_index(size);
    if (idx >= 0) {
        pool_dealloc(&global_pools[idx], ptr);
        return;
    }

    /* Was allocated with malloc */
    free(ptr);
}
