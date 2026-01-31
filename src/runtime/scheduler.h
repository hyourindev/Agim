/*
 * Agim - Scheduler
 *
 * Manages execution of multiple blocks with fair scheduling.
 * Uses reduction counting for preemption.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_SCHEDULER_H
#define AGIM_RUNTIME_SCHEDULER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/block.h"
#include "vm/bytecode.h"

/* Forward declarations */
typedef struct PrimitivesRuntime PrimitivesRuntime;
typedef struct ProcessGroupRegistry ProcessGroupRegistry;
typedef struct Tracer Tracer;

/*============================================================================
 * Scheduler Configuration
 *============================================================================*/

typedef struct SchedulerConfig {
    size_t max_blocks;          /* Maximum concurrent blocks */
    size_t default_reductions;  /* Default reductions per slice */
    size_t num_workers;         /* Number of worker threads (0 = single-threaded) */
    bool enable_stealing;       /* Enable work-stealing between workers */
} SchedulerConfig;

/*============================================================================
 * Block Registry (Hash Table for O(1) lookup)
 *============================================================================*/

#define REGISTRY_SHARDS 64
#define REGISTRY_INITIAL_CAPACITY 64

typedef struct BlockEntry {
    Pid pid;
    Block *block;
    struct BlockEntry *next;
} BlockEntry;

typedef struct RegistryShard {
    BlockEntry **buckets;
    size_t capacity;
    size_t count;
    pthread_mutex_t lock;
} RegistryShard;

typedef struct BlockRegistry {
    RegistryShard shards[REGISTRY_SHARDS];
    _Atomic(size_t) total_count;
} BlockRegistry;

/*============================================================================
 * Run Queue
 *============================================================================*/

typedef struct RunQueue {
    Block *head;
    Block *tail;
    size_t count;
} RunQueue;

/*============================================================================
 * Scheduler
 *============================================================================*/

/* Forward declaration for worker */
typedef struct Worker Worker;

typedef struct Scheduler {
    /* Configuration */
    SchedulerConfig config;

    /* Block registry (sharded hash table for O(1) lookup) */
    BlockRegistry registry;

    /* PID allocation (atomic for multi-threading) */
    _Atomic(Pid) next_pid;

    /* Single-threaded run queue (used when num_workers == 0) */
    RunQueue run_queue;

    /* Multi-threaded workers */
    Worker **workers;           /* Array of worker threads */
    size_t worker_count;
    _Atomic(size_t) next_worker; /* For round-robin assignment */

    /* State */
    _Atomic(bool) running;
    Block *current;             /* Currently executing block (single-threaded only) */

    /* Synchronization */
    pthread_mutex_t block_mutex; /* Protects block registry */

    /* Primitives runtime */
    PrimitivesRuntime *primitives;

    /* Process groups */
    ProcessGroupRegistry *groups;

    /* Global tracer (for system-wide tracing) */
    Tracer *tracer;

    /* Statistics (atomic for multi-threading) */
    _Atomic(size_t) total_spawned;
    _Atomic(size_t) total_terminated;
    _Atomic(size_t) total_reductions;
    _Atomic(size_t) context_switches;
} Scheduler;

/*============================================================================
 * Scheduler Lifecycle
 *============================================================================*/

/**
 * Get default scheduler configuration.
 */
SchedulerConfig scheduler_config_default(void);

/**
 * Create a new scheduler.
 */
Scheduler *scheduler_new(const SchedulerConfig *config);

/**
 * Free a scheduler and all its blocks.
 */
void scheduler_free(Scheduler *scheduler);

/*============================================================================
 * Block Management
 *============================================================================*/

/**
 * Spawn a new block with bytecode.
 * Returns the new block's PID, or PID_INVALID on failure.
 */
Pid scheduler_spawn(Scheduler *scheduler, Bytecode *code, const char *name);

/**
 * Spawn with specific capabilities and limits.
 */
Pid scheduler_spawn_ex(Scheduler *scheduler, Bytecode *code, const char *name,
                       CapabilitySet caps, const BlockLimits *limits);

/**
 * Get a block by PID.
 */
Block *scheduler_get_block(Scheduler *scheduler, Pid pid);

/**
 * Kill a block by PID.
 */
void scheduler_kill(Scheduler *scheduler, Pid pid);

/**
 * Get current executing block.
 */
Block *scheduler_current(Scheduler *scheduler);

/*============================================================================
 * Execution
 *============================================================================*/

/**
 * Run the scheduler until all blocks complete.
 */
void scheduler_run(Scheduler *scheduler);

/**
 * Run one scheduling cycle (execute one block for one time slice).
 * Returns true if there are still runnable blocks.
 */
bool scheduler_step(Scheduler *scheduler);

/**
 * Stop the scheduler.
 */
void scheduler_stop(Scheduler *scheduler);

/*============================================================================
 * Run Queue Operations
 *============================================================================*/

/**
 * Add a block to the run queue.
 */
void scheduler_enqueue(Scheduler *scheduler, Block *block);

/**
 * Remove and return next block from run queue.
 */
Block *scheduler_dequeue(Scheduler *scheduler);

/**
 * Check if run queue is empty.
 */
bool scheduler_queue_empty(const Scheduler *scheduler);

/*============================================================================
 * Primitives Runtime
 *============================================================================*/

/**
 * Set the primitives runtime for the scheduler.
 */
void scheduler_set_primitives(Scheduler *scheduler, PrimitivesRuntime *primitives);

/**
 * Get the primitives runtime.
 */
PrimitivesRuntime *scheduler_get_primitives(Scheduler *scheduler);

/*============================================================================
 * Statistics
 *============================================================================*/

typedef struct SchedulerStats {
    size_t blocks_total;        /* Total blocks ever created */
    size_t blocks_alive;        /* Currently alive blocks */
    size_t blocks_runnable;     /* Blocks in run queue */
    size_t blocks_waiting;      /* Blocks waiting for messages */
    size_t blocks_dead;         /* Terminated blocks */
    size_t total_reductions;    /* Total instructions executed */
    size_t context_switches;    /* Number of context switches */
} SchedulerStats;

/**
 * Get scheduler statistics.
 */
SchedulerStats scheduler_stats(const Scheduler *scheduler);

/**
 * Print scheduler statistics.
 */
void scheduler_print_stats(const Scheduler *scheduler);

/*============================================================================
 * Debug
 *============================================================================*/

/**
 * Print scheduler state for debugging.
 */
void scheduler_print(const Scheduler *scheduler);

/*============================================================================
 * Multi-threaded Scheduler
 *============================================================================*/

/**
 * Check if scheduler is running in multi-threaded mode.
 */
bool scheduler_is_multithreaded(const Scheduler *scheduler);

/**
 * Get number of worker threads.
 */
size_t scheduler_worker_count(const Scheduler *scheduler);

/**
 * Get a specific worker.
 */
Worker *scheduler_get_worker(Scheduler *scheduler, size_t index);

/**
 * Wake up a waiting block (thread-safe).
 */
void scheduler_wake_block(Scheduler *scheduler, Block *block);

/**
 * Get total block count.
 */
size_t scheduler_block_count(const Scheduler *scheduler);

/*============================================================================
 * Process Groups
 *============================================================================*/

/**
 * Get the process group registry.
 */
ProcessGroupRegistry *scheduler_get_groups(Scheduler *scheduler);

/*============================================================================
 * Tracing
 *============================================================================*/

/**
 * Get the global tracer.
 */
Tracer *scheduler_get_tracer(Scheduler *scheduler);

/**
 * Set the global tracer.
 */
void scheduler_set_tracer(Scheduler *scheduler, Tracer *tracer);

#endif /* AGIM_RUNTIME_SCHEDULER_H */
