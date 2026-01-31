/*
 * Agim - Worker Thread and Work-Stealing Deque
 *
 * Implements Chase-Lev work-stealing deque for per-worker run queues
 * and worker threads with their own VM instances.
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

typedef struct Scheduler Scheduler;
typedef struct VM VM;

/* Work-Stealing Deque (Chase-Lev) */

#define DEQUE_INITIAL_CAPACITY 64
#define CACHE_LINE_SIZE 64

typedef struct RetiredBuffer {
    Block **buffer;
    _Atomic(uint64_t) epoch;
    struct RetiredBuffer *next;
} RetiredBuffer;

typedef struct WorkDeque {
    _Alignas(CACHE_LINE_SIZE) _Atomic(size_t) top;
    _Alignas(CACHE_LINE_SIZE) _Atomic(size_t) bottom;
    _Atomic(Block **) buffer;
    _Atomic(size_t) capacity;
    _Atomic(uint64_t) global_epoch;
    RetiredBuffer *retired_head;
    pthread_mutex_t retired_lock;
} WorkDeque;

void deque_init(WorkDeque *deque);
void deque_free(WorkDeque *deque);
void deque_push(WorkDeque *deque, Block *block);
Block *deque_pop(WorkDeque *deque);
Block *deque_steal(WorkDeque *deque);
bool deque_empty(WorkDeque *deque);
size_t deque_size(WorkDeque *deque);

/* Worker Thread */

typedef enum WorkerState {
    WORKER_IDLE,
    WORKER_RUNNING,
    WORKER_STEALING,
    WORKER_STOPPED,
} WorkerState;

typedef struct Worker {
    int id;
    pthread_t thread;
    _Atomic(bool) thread_started;
    WorkDeque runq;
    VM *vm;
    Scheduler *scheduler;
    WorkerAllocator allocator;
    _Atomic(WorkerState) state;
    uint64_t rng_state;
    _Atomic(size_t) blocks_executed;
    _Atomic(size_t) steals_attempted;
    _Atomic(size_t) steals_successful;
    _Atomic(size_t) total_reductions;
} Worker;

Worker *worker_new(int id, Scheduler *scheduler);
void worker_free(Worker *worker);
bool worker_start(Worker *worker);
void worker_stop(Worker *worker);
void worker_join(Worker *worker);
void worker_enqueue(Worker *worker, Block *block);
Block *worker_steal(Worker *worker);

/* Multi-threaded Scheduler Configuration */

typedef struct MTSchedulerConfig {
    size_t num_workers;
    size_t max_blocks;
    size_t default_reductions;
    bool enable_stealing;
} MTSchedulerConfig;

MTSchedulerConfig mt_scheduler_config_default(void);

#endif /* AGIM_RUNTIME_WORKER_H */
