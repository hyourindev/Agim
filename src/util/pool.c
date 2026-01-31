/*
 * Agim - Memory Pool
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "util/pool.h"
#include "util/alloc.h"

#include <stdlib.h>
#include <string.h>

/* Pool Implementation */

void pool_init(MemoryPool *pool, size_t block_size) {
    if (!pool) return;

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

static bool pool_grow(MemoryPool *pool) {
    size_t total_size = sizeof(PoolChunk) + pool->block_size * pool->blocks_per_chunk;
    PoolChunk *chunk = malloc(total_size);
    if (!chunk) return false;

    chunk->next = pool->chunks;
    pool->chunks = chunk;
    pool->chunk_count++;

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

    if (!pool->free_list) {
        if (!pool_grow(pool)) {
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }
    }

    PoolFreeBlock *block = pool->free_list;
    pool->free_list = block->next;

    /* Update statistics inside the lock to avoid race conditions */
    atomic_fetch_add(&pool->allocated_count, 1);
    atomic_fetch_sub(&pool->free_count, 1);

    pthread_mutex_unlock(&pool->lock);

    return block;
}

void pool_dealloc(MemoryPool *pool, void *ptr) {
    if (!pool || !ptr) return;

    pthread_mutex_lock(&pool->lock);

    PoolFreeBlock *block = (PoolFreeBlock *)ptr;
    block->next = pool->free_list;
    pool->free_list = block;

    /* Update statistics inside the lock to avoid race conditions */
    atomic_fetch_sub(&pool->allocated_count, 1);
    atomic_fetch_add(&pool->free_count, 1);

    pthread_mutex_unlock(&pool->lock);
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

/* Global Pools */

/* Pool sizes optimized for common allocation patterns:
 * 24: Cons cell, small closure
 * 48: String header
 * 64: Small array
 * 96: Map entry
 * 128: Stack frame
 * 256: Medium objects
 * 512: Large objects
 */
#define NUM_GLOBAL_POOLS 7
static MemoryPool global_pools[NUM_GLOBAL_POOLS];
static const size_t pool_sizes[NUM_GLOBAL_POOLS] = {24, 48, 64, 96, 128, 256, 512};
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

    return malloc(size);
}

void pools_dealloc(void *ptr, size_t size) {
    if (!ptr) return;

    int idx = find_pool_index(size);
    if (idx >= 0) {
        pool_dealloc(&global_pools[idx], ptr);
        return;
    }

    free(ptr);
}
