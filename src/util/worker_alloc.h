/*
 * Agim - Per-Worker Allocator
 *
 * Thread-local allocator with pools for common allocation sizes.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_UTIL_WORKER_ALLOC_H
#define AGIM_UTIL_WORKER_ALLOC_H

#include <stdbool.h>
#include <stddef.h>

/* Configuration */

#define WORKER_ALLOC_NUM_POOLS 6
#define WORKER_ALLOC_MAX_SIZE 512
#define WORKER_ALLOC_CHUNK_SIZE 4096

/* Free List Node */

typedef struct WorkerFreeBlock {
    struct WorkerFreeBlock *next;
} WorkerFreeBlock;

/* Chunk */

typedef struct WorkerChunk {
    struct WorkerChunk *next;
    char data[];
} WorkerChunk;

/* Per-Size Pool */

typedef struct WorkerPool {
    size_t block_size;
    size_t blocks_per_chunk;
    WorkerFreeBlock *free_list;
    WorkerChunk *chunks;
    size_t allocated_count;
    size_t free_count;
} WorkerPool;

/* Worker Allocator */

typedef struct WorkerAllocator {
    WorkerPool pools[WORKER_ALLOC_NUM_POOLS];
    int worker_id;
} WorkerAllocator;

/* Allocator API */

void worker_alloc_init(WorkerAllocator *alloc, int worker_id);
void worker_alloc_free(WorkerAllocator *alloc);
void *worker_alloc_alloc(WorkerAllocator *alloc, size_t size);
void worker_alloc_dealloc(WorkerAllocator *alloc, void *ptr, size_t size);

/* Thread-Local Current Allocator */

void worker_alloc_set_current(WorkerAllocator *alloc);
WorkerAllocator *worker_alloc_get_current(void);
void *worker_alloc(size_t size);
void worker_dealloc(void *ptr, size_t size);

/* Statistics */

typedef struct WorkerAllocStats {
    size_t pool_sizes[WORKER_ALLOC_NUM_POOLS];
    size_t pool_allocated[WORKER_ALLOC_NUM_POOLS];
    size_t pool_free[WORKER_ALLOC_NUM_POOLS];
    size_t total_chunks;
    size_t total_memory;
} WorkerAllocStats;

WorkerAllocStats worker_alloc_stats(const WorkerAllocator *alloc);

#endif /* AGIM_UTIL_WORKER_ALLOC_H */
