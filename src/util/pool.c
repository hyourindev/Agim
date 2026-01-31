/*
 * Agim - Memory Pool
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "util/pool.h"
#include "util/alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Debug mode: Header-based magic number validation for double-free detection.
 * In debug mode, each allocation has a header that stores a magic number.
 * This header is placed before the user's data, so it doesn't conflict
 * with the data being stored. The user gets a pointer after the header.
 */
#ifdef AGIM_DEBUG
#define POOL_DEBUG_HEADERS 1
#define POOL_MAGIC_ALLOCATED 0xAB12CD34U
#define POOL_MAGIC_FREE      0xDEADBEEFU

typedef struct PoolBlockHeader {
    uint32_t magic;
    uint32_t size;  /* Original requested size for validation */
} PoolBlockHeader;

#define POOL_HEADER_SIZE sizeof(PoolBlockHeader)
#else
#define POOL_DEBUG_HEADERS 0
#define POOL_HEADER_SIZE 0
#endif

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

/* Check if a pointer belongs to one of the pool's chunks.
 * Must be called with pool->lock held.
 */
static bool pool_owns_ptr(MemoryPool *pool, void *ptr) {
    for (PoolChunk *chunk = pool->chunks; chunk; chunk = chunk->next) {
        char *start = chunk->data;
        char *end = start + pool->block_size * pool->blocks_per_chunk;
        if ((char *)ptr >= start && (char *)ptr < end) {
            /* Also verify alignment within the chunk */
            size_t offset = (size_t)((char *)ptr - start);
            if (offset % pool->block_size == 0) {
                return true;
            }
        }
    }
    return false;
}

void pool_dealloc(MemoryPool *pool, void *ptr) {
    if (!pool || !ptr) return;

    pthread_mutex_lock(&pool->lock);

    /* Validate that the pointer belongs to this pool.
     * This prevents silent corruption from invalid pointers.
     */
    if (!pool_owns_ptr(pool, ptr)) {
        pthread_mutex_unlock(&pool->lock);
#ifdef AGIM_DEBUG
        fprintf(stderr, "agim: pool_dealloc called with pointer not owned by pool "
                "(ptr=%p, block_size=%zu)\n", ptr, pool->block_size);
        abort();
#else
        /* In release mode, log warning but don't corrupt the free list */
        fprintf(stderr, "agim: warning: invalid pool_dealloc ignored (ptr=%p)\n", ptr);
        return;
#endif
    }

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

#if POOL_DEBUG_HEADERS
    /* In debug mode, add header size to the request */
    size_t total_size = size + POOL_HEADER_SIZE;
    int idx = find_pool_index(total_size);
    if (idx >= 0) {
        void *block = pool_alloc(&global_pools[idx]);
        if (block) {
            PoolBlockHeader *header = (PoolBlockHeader *)block;
            header->magic = POOL_MAGIC_ALLOCATED;
            header->size = (uint32_t)size;
            return (char *)block + POOL_HEADER_SIZE;
        }
        agim_set_error(AGIM_E_POOL_EXHAUSTED);
        return NULL;
    }

    /* Fall back to malloc for large allocations */
    void *block = malloc(total_size);
    if (block) {
        PoolBlockHeader *header = (PoolBlockHeader *)block;
        header->magic = POOL_MAGIC_ALLOCATED;
        header->size = (uint32_t)size;
        return (char *)block + POOL_HEADER_SIZE;
    }
    agim_set_error(AGIM_E_NOMEM);
    return NULL;
#else
    int idx = find_pool_index(size);
    if (idx >= 0) {
        void *result = pool_alloc(&global_pools[idx]);
        if (!result) {
            agim_set_error(AGIM_E_POOL_EXHAUSTED);
        }
        return result;
    }

    void *result = malloc(size);
    if (!result && size > 0) {
        agim_set_error(AGIM_E_NOMEM);
    }
    return result;
#endif
}

void pools_dealloc(void *ptr, size_t size) {
    if (!ptr) return;

#if POOL_DEBUG_HEADERS
    /* In debug mode, go back to find the header */
    PoolBlockHeader *header = (PoolBlockHeader *)((char *)ptr - POOL_HEADER_SIZE);

    /* Check for double-free */
    if (header->magic == POOL_MAGIC_FREE) {
        fprintf(stderr, "agim: double-free detected in pool (ptr=%p, size=%zu)\n", ptr, size);
        abort();
    }

    /* Check for invalid pointer (not allocated by pool) */
    if (header->magic != POOL_MAGIC_ALLOCATED) {
        fprintf(stderr, "agim: invalid pointer passed to pools_dealloc "
                "(ptr=%p, size=%zu, magic=0x%08x)\n", ptr, size, header->magic);
        abort();
    }

    /* Check size matches */
    if (header->size != (uint32_t)size) {
        fprintf(stderr, "agim: size mismatch in pools_dealloc "
                "(ptr=%p, expected=%u, got=%zu)\n", ptr, header->size, size);
        abort();
    }

    /* Mark as freed */
    header->magic = POOL_MAGIC_FREE;

    /* Deallocate the actual block (including header) */
    size_t total_size = size + POOL_HEADER_SIZE;
    int idx = find_pool_index(total_size);
    if (idx >= 0) {
        pool_dealloc(&global_pools[idx], header);
        return;
    }

    free(header);
#else
    int idx = find_pool_index(size);
    if (idx >= 0) {
        pool_dealloc(&global_pools[idx], ptr);
        return;
    }

    free(ptr);
#endif
}
