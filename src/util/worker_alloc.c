/*
 * Agim - Per-Worker Allocator
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "util/worker_alloc.h"
#include "util/alloc.h"

#include <stdlib.h>
#include <string.h>

/* Pool Size Configuration */

static const size_t pool_sizes[WORKER_ALLOC_NUM_POOLS] = {
    16, 32, 64, 128, 256, 512
};

static inline int find_pool_index(size_t size) {
    for (int i = 0; i < WORKER_ALLOC_NUM_POOLS; i++) {
        if (size <= pool_sizes[i]) {
            return i;
        }
    }
    return -1;
}

/* Pool Operations */

static void pool_init(WorkerPool *pool, size_t block_size) {
    if (block_size < sizeof(WorkerFreeBlock)) {
        block_size = sizeof(WorkerFreeBlock);
    }

    pool->block_size = align_size(block_size, 8);
    pool->blocks_per_chunk = (WORKER_ALLOC_CHUNK_SIZE - sizeof(WorkerChunk)) / pool->block_size;
    pool->free_list = NULL;
    pool->chunks = NULL;
    pool->allocated_count = 0;
    pool->free_count = 0;
}

static void pool_free_all(WorkerPool *pool) {
    WorkerChunk *chunk = pool->chunks;
    while (chunk) {
        WorkerChunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    pool->chunks = NULL;
    pool->free_list = NULL;
    pool->allocated_count = 0;
    pool->free_count = 0;
}

static bool pool_grow(WorkerPool *pool) {
    size_t chunk_size = sizeof(WorkerChunk) + pool->block_size * pool->blocks_per_chunk;
    WorkerChunk *chunk = malloc(chunk_size);
    if (!chunk) return false;

    chunk->next = pool->chunks;
    pool->chunks = chunk;

    char *block = chunk->data;
    for (size_t i = 0; i < pool->blocks_per_chunk; i++) {
        WorkerFreeBlock *free_block = (WorkerFreeBlock *)block;
        free_block->next = pool->free_list;
        pool->free_list = free_block;
        block += pool->block_size;
    }

    pool->free_count += pool->blocks_per_chunk;
    return true;
}

static void *pool_alloc(WorkerPool *pool) {
    if (!pool->free_list) {
        if (!pool_grow(pool)) {
            return NULL;
        }
    }

    WorkerFreeBlock *block = pool->free_list;
    pool->free_list = block->next;

    pool->allocated_count++;
    pool->free_count--;

    return block;
}

static void pool_dealloc(WorkerPool *pool, void *ptr) {
    WorkerFreeBlock *block = (WorkerFreeBlock *)ptr;
    block->next = pool->free_list;
    pool->free_list = block;

    pool->allocated_count--;
    pool->free_count++;
}

/* Worker Allocator Implementation */

void worker_alloc_init(WorkerAllocator *alloc, int worker_id) {
    if (!alloc) return;

    alloc->worker_id = worker_id;

    for (int i = 0; i < WORKER_ALLOC_NUM_POOLS; i++) {
        pool_init(&alloc->pools[i], pool_sizes[i]);
    }
}

void worker_alloc_free(WorkerAllocator *alloc) {
    if (!alloc) return;

    for (int i = 0; i < WORKER_ALLOC_NUM_POOLS; i++) {
        pool_free_all(&alloc->pools[i]);
    }
}

void *worker_alloc_alloc(WorkerAllocator *alloc, size_t size) {
    if (!alloc || size == 0) {
        return malloc(size);
    }

    int idx = find_pool_index(size);
    if (idx >= 0) {
        return pool_alloc(&alloc->pools[idx]);
    }

    return malloc(size);
}

void worker_alloc_dealloc(WorkerAllocator *alloc, void *ptr, size_t size) {
    if (!ptr) return;

    if (!alloc) {
        free(ptr);
        return;
    }

    int idx = find_pool_index(size);
    if (idx >= 0) {
        pool_dealloc(&alloc->pools[idx], ptr);
        return;
    }

    free(ptr);
}

/* Thread-Local Storage */

static _Thread_local WorkerAllocator *tls_current_alloc = NULL;

void worker_alloc_set_current(WorkerAllocator *alloc) {
    tls_current_alloc = alloc;
}

WorkerAllocator *worker_alloc_get_current(void) {
    return tls_current_alloc;
}

void *worker_alloc(size_t size) {
    WorkerAllocator *alloc = tls_current_alloc;
    if (alloc) {
        return worker_alloc_alloc(alloc, size);
    }
    return malloc(size);
}

void worker_dealloc(void *ptr, size_t size) {
    if (!ptr) return;

    WorkerAllocator *alloc = tls_current_alloc;
    if (alloc) {
        worker_alloc_dealloc(alloc, ptr, size);
        return;
    }
    free(ptr);
}

/* Statistics */

WorkerAllocStats worker_alloc_stats(const WorkerAllocator *alloc) {
    WorkerAllocStats stats = {0};

    if (!alloc) return stats;

    size_t total_chunks = 0;
    size_t total_memory = 0;

    for (int i = 0; i < WORKER_ALLOC_NUM_POOLS; i++) {
        const WorkerPool *pool = &alloc->pools[i];
        stats.pool_sizes[i] = pool->block_size;
        stats.pool_allocated[i] = pool->allocated_count;
        stats.pool_free[i] = pool->free_count;

        WorkerChunk *chunk = pool->chunks;
        while (chunk) {
            total_chunks++;
            total_memory += sizeof(WorkerChunk) + pool->block_size * pool->blocks_per_chunk;
            chunk = chunk->next;
        }
    }

    stats.total_chunks = total_chunks;
    stats.total_memory = total_memory;

    return stats;
}
