/*
 * Agim - Worker Thread and Work-Stealing Deque
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE  /* For usleep */

#include "runtime/worker.h"
#include "runtime/scheduler.h"
#include "vm/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*============================================================================
 * Work-Stealing Deque Implementation (Chase-Lev)
 *============================================================================*/

void deque_init(WorkDeque *deque) {
    atomic_store(&deque->top, 0);
    atomic_store(&deque->bottom, 0);
    atomic_store(&deque->capacity, DEQUE_INITIAL_CAPACITY);

    Block **buffer = malloc(sizeof(Block *) * DEQUE_INITIAL_CAPACITY);
    memset(buffer, 0, sizeof(Block *) * DEQUE_INITIAL_CAPACITY);
    atomic_store(&deque->buffer, buffer);

    /* Initialize epoch-based reclamation */
    atomic_store(&deque->global_epoch, 0);
    deque->retired_head = NULL;
    pthread_mutex_init(&deque->retired_lock, NULL);
}

void deque_free(WorkDeque *deque) {
    /* Free current buffer */
    Block **buffer = atomic_load(&deque->buffer);
    if (buffer) {
        free(buffer);
    }

    /* Free all retired buffers */
    pthread_mutex_lock(&deque->retired_lock);
    RetiredBuffer *retired = deque->retired_head;
    while (retired) {
        RetiredBuffer *next = retired->next;
        free(retired->buffer);
        free(retired);
        retired = next;
    }
    pthread_mutex_unlock(&deque->retired_lock);
    pthread_mutex_destroy(&deque->retired_lock);
}

/**
 * Retire an old buffer for deferred reclamation.
 */
static void deque_retire_buffer(WorkDeque *deque, Block **old_buf) {
    RetiredBuffer *retired = malloc(sizeof(RetiredBuffer));
    if (!retired) {
        /* If we can't allocate, leak the old buffer (rare case) */
        return;
    }

    retired->buffer = old_buf;
    atomic_store(&retired->epoch, atomic_load(&deque->global_epoch));

    pthread_mutex_lock(&deque->retired_lock);
    retired->next = deque->retired_head;
    deque->retired_head = retired;
    pthread_mutex_unlock(&deque->retired_lock);
}

/**
 * Reclaim old buffers that are safe to free (epoch has advanced).
 * Called periodically by the deque owner.
 */
static void deque_reclaim_buffers(WorkDeque *deque) {
    uint64_t current_epoch = atomic_load(&deque->global_epoch);

    pthread_mutex_lock(&deque->retired_lock);

    RetiredBuffer **ptr = &deque->retired_head;
    while (*ptr) {
        RetiredBuffer *retired = *ptr;
        /* Safe to reclaim if at least 2 epochs have passed */
        if (current_epoch - atomic_load(&retired->epoch) >= 2) {
            *ptr = retired->next;
            free(retired->buffer);
            free(retired);
        } else {
            ptr = &(*ptr)->next;
        }
    }

    pthread_mutex_unlock(&deque->retired_lock);
}

/**
 * Grow the deque buffer (called by owner only).
 */
static void deque_grow(WorkDeque *deque) {
    size_t old_cap = atomic_load(&deque->capacity);
    size_t new_cap = old_cap * 2;

    Block **old_buf = atomic_load(&deque->buffer);
    Block **new_buf = malloc(sizeof(Block *) * new_cap);

    size_t top = atomic_load(&deque->top);
    size_t bottom = atomic_load(&deque->bottom);

    /* Copy existing elements */
    for (size_t i = top; i < bottom; i++) {
        new_buf[i % new_cap] = old_buf[i % old_cap];
    }

    atomic_store(&deque->buffer, new_buf);
    atomic_store(&deque->capacity, new_cap);

    /* Advance epoch and retire old buffer */
    atomic_fetch_add(&deque->global_epoch, 1);
    deque_retire_buffer(deque, old_buf);

    /* Opportunistically reclaim old buffers */
    deque_reclaim_buffers(deque);
}

void deque_push(WorkDeque *deque, Block *block) {
    size_t bottom = atomic_load_explicit(&deque->bottom, memory_order_relaxed);
    size_t top = atomic_load_explicit(&deque->top, memory_order_acquire);
    size_t capacity = atomic_load_explicit(&deque->capacity, memory_order_relaxed);

    /* Check if we need to grow */
    if (bottom - top >= capacity - 1) {
        deque_grow(deque);
        capacity = atomic_load(&deque->capacity);
    }

    Block **buffer = atomic_load_explicit(&deque->buffer, memory_order_relaxed);
    buffer[bottom % capacity] = block;

    /* Release store to make the push visible to stealers */
    atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_release);
}

Block *deque_pop(WorkDeque *deque) {
    size_t bottom = atomic_load_explicit(&deque->bottom, memory_order_relaxed);
    if (bottom == 0) return NULL;

    bottom = bottom - 1;
    atomic_store_explicit(&deque->bottom, bottom, memory_order_relaxed);

    /* Memory fence to ensure the store to bottom is visible before we read top */
    atomic_thread_fence(memory_order_seq_cst);

    size_t top = atomic_load_explicit(&deque->top, memory_order_relaxed);

    if (top > bottom) {
        /* Deque is empty, restore bottom */
        atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_relaxed);
        return NULL;
    }

    size_t capacity = atomic_load_explicit(&deque->capacity, memory_order_relaxed);
    Block **buffer = atomic_load_explicit(&deque->buffer, memory_order_relaxed);
    Block *block = buffer[bottom % capacity];

    if (top == bottom) {
        /* Last element - may race with stealer */
        if (!atomic_compare_exchange_strong_explicit(
                &deque->top, &top, top + 1,
                memory_order_seq_cst, memory_order_relaxed)) {
            /* Stealer got it */
            block = NULL;
        }
        atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_relaxed);
    }

    return block;
}

Block *deque_steal(WorkDeque *deque) {
    size_t top = atomic_load_explicit(&deque->top, memory_order_acquire);

    /*
     * Use acquire-release instead of seq_cst for better performance.
     * The acquire load of top above and the acquire load of bottom below
     * provide sufficient ordering for the steal operation.
     */

    size_t bottom = atomic_load_explicit(&deque->bottom, memory_order_acquire);

    if (top >= bottom) {
        return NULL; /* Empty */
    }

    size_t capacity = atomic_load_explicit(&deque->capacity, memory_order_relaxed);
    Block **buffer = atomic_load_explicit(&deque->buffer, memory_order_relaxed);
    Block *block = buffer[top % capacity];

    /* Try to increment top to claim this item.
     * Use acq_rel instead of seq_cst - provides sufficient synchronization
     * while being cheaper on most architectures. */
    if (!atomic_compare_exchange_strong_explicit(
            &deque->top, &top, top + 1,
            memory_order_acq_rel, memory_order_relaxed)) {
        /* Another stealer or the owner got it */
        return NULL;
    }

    return block;
}

bool deque_empty(WorkDeque *deque) {
    size_t top = atomic_load_explicit(&deque->top, memory_order_relaxed);
    size_t bottom = atomic_load_explicit(&deque->bottom, memory_order_relaxed);
    return top >= bottom;
}

size_t deque_size(WorkDeque *deque) {
    size_t top = atomic_load_explicit(&deque->top, memory_order_relaxed);
    size_t bottom = atomic_load_explicit(&deque->bottom, memory_order_relaxed);
    return (bottom > top) ? (bottom - top) : 0;
}

/*============================================================================
 * Random Number Generation (for victim selection)
 *============================================================================*/

/**
 * xorshift64 PRNG - fast, simple, good statistical properties.
 * Used for randomized victim selection in work-stealing.
 */
static inline uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/*============================================================================
 * Worker Thread Implementation
 *============================================================================*/

/* Forward declaration of worker loop */
static void *worker_loop(void *arg);

Worker *worker_new(int id, Scheduler *scheduler) {
    Worker *worker = malloc(sizeof(Worker));
    if (!worker) return NULL;

    worker->id = id;
    worker->scheduler = scheduler;
    worker->vm = vm_new();
    if (!worker->vm) {
        free(worker);
        return NULL;
    }

    deque_init(&worker->runq);

    /* Initialize per-worker allocator */
    worker_alloc_init(&worker->allocator, id);

    atomic_store(&worker->state, WORKER_IDLE);
    atomic_store(&worker->thread_started, false);
    atomic_store(&worker->blocks_executed, 0);
    atomic_store(&worker->steals_attempted, 0);
    atomic_store(&worker->steals_successful, 0);
    atomic_store(&worker->total_reductions, 0);

    /* Initialize RNG with unique seed based on worker id and address */
    worker->rng_state = (uint64_t)id * 2654435761UL + (uint64_t)(uintptr_t)worker;
    if (worker->rng_state == 0) worker->rng_state = 1;  /* Ensure non-zero */

    return worker;
}

void worker_free(Worker *worker) {
    if (!worker) return;

    /* Ensure worker is stopped */
    worker_stop(worker);
    worker_join(worker);

    /* Free resources */
    deque_free(&worker->runq);
    worker_alloc_free(&worker->allocator);
    if (worker->vm) {
        vm_free(worker->vm);
    }
    free(worker);
}

bool worker_start(Worker *worker) {
    if (!worker) return false;

    WorkerState expected = WORKER_IDLE;
    if (!atomic_compare_exchange_strong(&worker->state, &expected, WORKER_RUNNING)) {
        return false; /* Already running or stopped */
    }

    if (pthread_create(&worker->thread, NULL, worker_loop, worker) != 0) {
        atomic_store(&worker->state, WORKER_IDLE);
        return false;
    }

    atomic_store(&worker->thread_started, true);
    return true;
}

void worker_stop(Worker *worker) {
    if (!worker) return;
    atomic_store(&worker->state, WORKER_STOPPED);
}

void worker_join(Worker *worker) {
    if (!worker) return;

    /* Only join if thread was actually started and not already joined */
    if (atomic_exchange(&worker->thread_started, false)) {
        pthread_join(worker->thread, NULL);
        atomic_store(&worker->state, WORKER_IDLE);
    }
}

void worker_enqueue(Worker *worker, Block *block) {
    if (!worker || !block) return;
    deque_push(&worker->runq, block);
}

Block *worker_steal(Worker *worker) {
    if (!worker || !worker->scheduler) return NULL;

    Scheduler *sched = worker->scheduler;
    if (sched->worker_count <= 1) return NULL;

    atomic_fetch_add(&worker->steals_attempted, 1);

    /* Use randomized victim selection for better load balancing.
     * Starting from a random offset reduces contention when multiple
     * workers try to steal simultaneously. */
    size_t start = xorshift64(&worker->rng_state) % sched->worker_count;

    for (size_t i = 0; i < sched->worker_count; i++) {
        size_t victim_idx = (start + i) % sched->worker_count;
        if (victim_idx == (size_t)worker->id) continue;

        Worker *victim = sched->workers[victim_idx];
        if (!victim) continue;

        Block *stolen = deque_steal(&victim->runq);
        if (stolen) {
            return stolen;
        }
    }

    return NULL;
}

/*============================================================================
 * Worker Main Loop
 *============================================================================*/

/**
 * Check if all work is complete.
 * Uses atomic counters: when total_terminated >= total_spawned, all blocks are done.
 */
static bool all_work_done(Scheduler *sched) {
    size_t spawned = atomic_load(&sched->total_spawned);
    size_t terminated = atomic_load(&sched->total_terminated);
    return spawned > 0 && terminated >= spawned;
}

static void *worker_loop(void *arg) {
    Worker *worker = (Worker *)arg;
    if (!worker) return NULL;

    /* Set thread-local allocator for this worker thread */
    worker_alloc_set_current(&worker->allocator);

    size_t idle_spins = 0;
    const size_t SPIN_THRESHOLD = 100;        /* Start backing off after 100 spins */
    const size_t TERMINATION_CHECK_INTERVAL = 100;

    /* Exponential backoff parameters */
    size_t backoff_us = 10;                   /* Start at 10us */
    const size_t MAX_BACKOFF_US = 1000;       /* Cap at 1ms */

    while (atomic_load(&worker->state) != WORKER_STOPPED) {
        /* Try to get work from our own queue */
        Block *block = deque_pop(&worker->runq);

        if (!block) {
            /* Try to steal from others */
            block = worker_steal(worker);

            if (block) {
                atomic_fetch_add(&worker->steals_successful, 1);
            }
        }

        if (block) {
            /* Reset backoff state on successful work */
            idle_spins = 0;
            backoff_us = 10;

            /*
             * Use the block's own VM, not the worker's VM.
             * Each block needs its own execution state (IP, stack, frames).
             * The worker VM is not used for execution - each block has its own.
             */
            VM *vm = block->vm;
            vm->scheduler = worker->scheduler;

            /* Configure reduction limit for this time slice */
            vm->reduction_limit = block->limits.max_reductions;
            vm->reductions = 0;

            /* Run block for one time slice */
            VMResult result = vm_run(vm);

            atomic_fetch_add(&worker->blocks_executed, 1);
            atomic_fetch_add(&worker->total_reductions, vm->reductions);

            /* Handle result */
            switch (result) {
            case VM_YIELD:
                /* Block yielded, re-enqueue */
                if (atomic_load(&block->state) == BLOCK_RUNNABLE) {
                    deque_push(&worker->runq, block);
                }
                break;

            case VM_WAITING:
                /* Block is waiting for message, don't re-enqueue */
                break;

            case VM_OK:
            case VM_HALT:
                /* Block completed */
                atomic_store(&block->state, BLOCK_DEAD);
                atomic_fetch_add(&worker->scheduler->total_terminated, 1);
                break;

            default:
                /* Error */
                atomic_store(&block->state, BLOCK_DEAD);
                atomic_fetch_add(&worker->scheduler->total_terminated, 1);
                break;
            }
        } else {
            /* No work available */
            idle_spins++;

            /* Periodically check if all work is done */
            if (idle_spins % TERMINATION_CHECK_INTERVAL == 0) {
                if (all_work_done(worker->scheduler)) {
                    /* All blocks terminated, exit worker */
                    break;
                }
            }

            /* Exponential backoff to reduce CPU usage when idle */
            if (idle_spins > SPIN_THRESHOLD) {
                usleep((useconds_t)backoff_us);
                /* Double backoff time, capped at max */
                if (backoff_us < MAX_BACKOFF_US) {
                    backoff_us *= 2;
                    if (backoff_us > MAX_BACKOFF_US) {
                        backoff_us = MAX_BACKOFF_US;
                    }
                }
            }
        }
    }

    /* Clear thread-local allocator on exit */
    worker_alloc_set_current(NULL);

    return NULL;
}

/*============================================================================
 * Configuration
 *============================================================================*/

MTSchedulerConfig mt_scheduler_config_default(void) {
    /* Auto-detect number of CPUs */
    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cpus < 1) num_cpus = 1;

    return (MTSchedulerConfig){
        .num_workers = (size_t)num_cpus,
        .max_blocks = 10000,
        .default_reductions = 10000,
        .enable_stealing = true,
    };
}
