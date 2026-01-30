/*
 * Agim - Worker Thread and Work-Stealing Deque
 *
 * Implements the multi-threaded scheduler infrastructure:
 * - Chase-Lev work-stealing deque for per-worker run queues
 * - Worker threads with their own VM instances
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_WORKER_H
#define AGIM_RUNTIME_WORKER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/block.h"
#include "util/worker_alloc.h"

/* Forward declarations */
typedef struct Scheduler Scheduler;
typedef struct VM VM;

/*============================================================================
 * Work-Stealing Deque (Chase-Lev)
 *
 * Lock-free deque for work-stealing:
 * - Owner pushes/pops from bottom (LIFO for cache locality)
 * - Thieves steal from top (FIFO for fairness)
 *============================================================================*/

#define DEQUE_INITIAL_CAPACITY 64

/* Cache line size for alignment (prevents false sharing) */
#define CACHE_LINE_SIZE 64

/* Retired buffer for deferred reclamation */
typedef struct RetiredBuffer {
    Block **buffer;
    _Atomic(uint64_t) epoch;
    struct RetiredBuffer *next;
} RetiredBuffer;

typedef struct WorkDeque {
    /* Align top and bottom to separate cache lines to prevent false sharing
     * between stealing threads and the owning thread */
    _Alignas(CACHE_LINE_SIZE) _Atomic(size_t) top;      /* Index for stealing (other workers) */
    _Alignas(CACHE_LINE_SIZE) _Atomic(size_t) bottom;   /* Index for push/pop (owner) */
    _Atomic(Block **) buffer; /* Circular buffer of block pointers */
    _Atomic(size_t) capacity; /* Current buffer capacity */

    /* Epoch-based reclamation for old buffers */
    _Atomic(uint64_t) global_epoch;
    RetiredBuffer *retired_head;
    pthread_mutex_t retired_lock;
} WorkDeque;

/**
 * Initialize a work-stealing deque.
 */
void deque_init(WorkDeque *deque);

/**
 * Free deque resources.
 */
void deque_free(WorkDeque *deque);

/**
 * Push a block onto the deque (owner only).
 */
void deque_push(WorkDeque *deque, Block *block);

/**
 * Pop a block from the deque (owner only).
 * Returns NULL if empty.
 */
Block *deque_pop(WorkDeque *deque);

/**
 * Steal a block from the deque (any thread).
 * Returns NULL if empty or contention.
 */
Block *deque_steal(WorkDeque *deque);

/**
 * Check if deque is empty (approximate).
 */
bool deque_empty(WorkDeque *deque);

/**
 * Get approximate size (for load balancing).
 */
size_t deque_size(WorkDeque *deque);

/*============================================================================
 * Worker Thread
 *============================================================================*/

typedef enum WorkerState {
    WORKER_IDLE,        /* Not started */
    WORKER_RUNNING,     /* Actively executing blocks */
    WORKER_STEALING,    /* Looking for work from others */
    WORKER_STOPPED,     /* Shutdown requested */
} WorkerState;

typedef struct Worker {
    /* Identity */
    int id;
    pthread_t thread;
    _Atomic(bool) thread_started;  /* Track if thread was created */

    /* Run queue (work-stealing deque) */
    WorkDeque runq;

    /* VM instance for this worker */
    VM *vm;

    /* Scheduler reference */
    Scheduler *scheduler;

    /* Per-worker allocator (lock-free, only this worker accesses) */
    WorkerAllocator allocator;

    /* State */
    _Atomic(WorkerState) state;

    /* Random state for victim selection (xorshift64) */
    uint64_t rng_state;

    /* Statistics */
    _Atomic(size_t) blocks_executed;
    _Atomic(size_t) steals_attempted;
    _Atomic(size_t) steals_successful;
    _Atomic(size_t) total_reductions;
} Worker;

/**
 * Create a new worker.
 */
Worker *worker_new(int id, Scheduler *scheduler);

/**
 * Free a worker and its resources.
 */
void worker_free(Worker *worker);

/**
 * Start the worker thread.
 */
bool worker_start(Worker *worker);

/**
 * Stop the worker thread.
 */
void worker_stop(Worker *worker);

/**
 * Wait for worker thread to finish.
 */
void worker_join(Worker *worker);

/**
 * Add a block to this worker's run queue.
 */
void worker_enqueue(Worker *worker, Block *block);

/**
 * Try to steal work from other workers.
 * Returns stolen block or NULL.
 */
Block *worker_steal(Worker *worker);

/*============================================================================
 * Multi-threaded Scheduler Configuration
 *============================================================================*/

typedef struct MTSchedulerConfig {
    size_t num_workers;         /* Number of worker threads (0 = auto) */
    size_t max_blocks;          /* Maximum concurrent blocks */
    size_t default_reductions;  /* Default reductions per slice */
    bool enable_stealing;       /* Enable work-stealing */
} MTSchedulerConfig;

/**
 * Get default multi-threaded scheduler configuration.
 */
MTSchedulerConfig mt_scheduler_config_default(void);

#endif /* AGIM_RUNTIME_WORKER_H */
