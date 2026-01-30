/*
 * Agim - Per-Worker Allocator
 *
 * Thread-local allocator with pools for common allocation sizes.
 * Eliminates mutex contention for frequent small allocations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_UTIL_WORKER_ALLOC_H
#define AGIM_UTIL_WORKER_ALLOC_H

#include <stdbool.h>
#include <stddef.h>

/*============================================================================
 * Configuration
 *============================================================================*/

/* Pool sizes: 16, 32, 64, 128, 256, 512 bytes */
#define WORKER_ALLOC_NUM_POOLS 6
#define WORKER_ALLOC_MAX_SIZE 512

/* Blocks per chunk allocation */
#define WORKER_ALLOC_CHUNK_SIZE 4096

/*============================================================================
 * Free List Node (stored in free blocks)
 *============================================================================*/

typedef struct WorkerFreeBlock {
    struct WorkerFreeBlock *next;
} WorkerFreeBlock;

/*============================================================================
 * Chunk (holds multiple blocks)
 *============================================================================*/

typedef struct WorkerChunk {
    struct WorkerChunk *next;
    char data[];
} WorkerChunk;

/*============================================================================
 * Per-Size Pool
 *============================================================================*/

typedef struct WorkerPool {
    size_t block_size;          /* Size of blocks in this pool (aligned) */
    size_t blocks_per_chunk;    /* Blocks per chunk */
    WorkerFreeBlock *free_list; /* Lock-free: only owner accesses */
    WorkerChunk *chunks;        /* Allocated chunks */
    size_t allocated_count;     /* Stats: blocks allocated */
    size_t free_count;          /* Stats: blocks in free list */
} WorkerPool;

/*============================================================================
 * Worker Allocator (per-thread)
 *============================================================================*/

typedef struct WorkerAllocator {
    WorkerPool pools[WORKER_ALLOC_NUM_POOLS];
    int worker_id;              /* Owning worker ID */
} WorkerAllocator;

/*============================================================================
 * Allocator API
 *============================================================================*/

/**
 * Initialize a worker allocator.
 * Call once per worker thread during worker creation.
 */
void worker_alloc_init(WorkerAllocator *alloc, int worker_id);

/**
 * Free all memory in a worker allocator.
 * Call during worker shutdown.
 */
void worker_alloc_free(WorkerAllocator *alloc);

/**
 * Allocate memory from the worker allocator.
 * Falls back to malloc for sizes > WORKER_ALLOC_MAX_SIZE.
 * This is NOT thread-safe - only the owning worker should call this.
 */
void *worker_alloc_alloc(WorkerAllocator *alloc, size_t size);

/**
 * Free memory back to the worker allocator.
 * Falls back to free for sizes > WORKER_ALLOC_MAX_SIZE.
 * This is NOT thread-safe - only the owning worker should call this.
 */
void worker_alloc_dealloc(WorkerAllocator *alloc, void *ptr, size_t size);

/*============================================================================
 * Thread-Local Current Allocator
 *============================================================================*/

/**
 * Set the current thread's worker allocator.
 * Call at the start of worker_loop().
 */
void worker_alloc_set_current(WorkerAllocator *alloc);

/**
 * Get the current thread's worker allocator.
 * Returns NULL if not set (not a worker thread).
 */
WorkerAllocator *worker_alloc_get_current(void);

/**
 * Allocate using current thread's allocator (or malloc fallback).
 */
void *worker_alloc(size_t size);

/**
 * Free using current thread's allocator (or free fallback).
 */
void worker_dealloc(void *ptr, size_t size);

/*============================================================================
 * Statistics
 *============================================================================*/

typedef struct WorkerAllocStats {
    size_t pool_sizes[WORKER_ALLOC_NUM_POOLS];
    size_t pool_allocated[WORKER_ALLOC_NUM_POOLS];
    size_t pool_free[WORKER_ALLOC_NUM_POOLS];
    size_t total_chunks;
    size_t total_memory;
} WorkerAllocStats;

WorkerAllocStats worker_alloc_stats(const WorkerAllocator *alloc);

#endif /* AGIM_UTIL_WORKER_ALLOC_H */
