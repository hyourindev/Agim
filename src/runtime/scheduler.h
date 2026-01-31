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

typedef struct PrimitivesRuntime PrimitivesRuntime;
typedef struct ProcessGroupRegistry ProcessGroupRegistry;
typedef struct Tracer Tracer;

/* Scheduler Configuration */

typedef struct SchedulerConfig {
    size_t max_blocks;
    size_t default_reductions;
    size_t num_workers;		/* 0 = single-threaded */
    bool enable_stealing;
} SchedulerConfig;

/* Block Registry */

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

/* Run Queue */

typedef struct RunQueue {
    Block *head;
    Block *tail;
    size_t count;
} RunQueue;

/* Scheduler */

typedef struct Worker Worker;

typedef struct Scheduler {
    SchedulerConfig config;

    BlockRegistry registry;
    _Atomic(Pid) next_pid;

    RunQueue run_queue;

    Worker **workers;
    size_t worker_count;
    _Atomic(size_t) next_worker;

    _Atomic(bool) running;
    Block *current;

    pthread_mutex_t block_mutex;

    PrimitivesRuntime *primitives;
    ProcessGroupRegistry *groups;
    Tracer *tracer;

    _Atomic(size_t) total_spawned;
    _Atomic(size_t) total_terminated;
    _Atomic(size_t) total_reductions;
    _Atomic(size_t) context_switches;

    uint64_t start_time_ms;
} Scheduler;

/* Lifecycle */

SchedulerConfig scheduler_config_default(void);
Scheduler *scheduler_new(const SchedulerConfig *config);
void scheduler_free(Scheduler *scheduler);

/* Block Management */

Pid scheduler_spawn(Scheduler *scheduler, Bytecode *code, const char *name);
Pid scheduler_spawn_ex(Scheduler *scheduler, Bytecode *code, const char *name,
                       CapabilitySet caps, const BlockLimits *limits);
bool scheduler_register_block(Scheduler *scheduler, Block *block);
Block *scheduler_get_block(Scheduler *scheduler, Pid pid);
void scheduler_kill(Scheduler *scheduler, Pid pid);
void scheduler_propagate_exit(Scheduler *scheduler, Block *exited_block);
Block *scheduler_current(Scheduler *scheduler);

/* Execution */

void scheduler_run(Scheduler *scheduler);
bool scheduler_step(Scheduler *scheduler);
void scheduler_stop(Scheduler *scheduler);

/* Run Queue */

void scheduler_enqueue(Scheduler *scheduler, Block *block);
Block *scheduler_dequeue(Scheduler *scheduler);
bool scheduler_queue_empty(const Scheduler *scheduler);

/* Primitives */

void scheduler_set_primitives(Scheduler *scheduler, PrimitivesRuntime *primitives);
PrimitivesRuntime *scheduler_get_primitives(Scheduler *scheduler);

/* Statistics */

typedef struct SchedulerStats {
    size_t blocks_total;
    size_t blocks_alive;
    size_t blocks_runnable;
    size_t blocks_waiting;
    size_t blocks_dead;
    size_t total_reductions;
    size_t context_switches;
} SchedulerStats;

SchedulerStats scheduler_stats(const Scheduler *scheduler);
void scheduler_print_stats(const Scheduler *scheduler);

/* Debug */

void scheduler_print(const Scheduler *scheduler);

/* Multi-threaded */

bool scheduler_is_multithreaded(const Scheduler *scheduler);
size_t scheduler_worker_count(const Scheduler *scheduler);
Worker *scheduler_get_worker(Scheduler *scheduler, size_t index);
void scheduler_wake_block(Scheduler *scheduler, Block *block);
size_t scheduler_block_count(const Scheduler *scheduler);

/* Process Groups */

ProcessGroupRegistry *scheduler_get_groups(Scheduler *scheduler);

/* Tracing */

Tracer *scheduler_get_tracer(Scheduler *scheduler);
void scheduler_set_tracer(Scheduler *scheduler, Tracer *tracer);

#endif /* AGIM_RUNTIME_SCHEDULER_H */
