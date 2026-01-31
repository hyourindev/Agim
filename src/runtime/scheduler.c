/*
 * Agim - Scheduler
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/scheduler.h"
#include "runtime/worker.h"
#include "runtime/procgroup.h"
#include "runtime/supervisor.h"
#include "runtime/telemetry.h"
#include "runtime/timer.h"
#include "vm/primitives.h"
#include "vm/value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Default Configuration */

SchedulerConfig scheduler_config_default(void) {
    return (SchedulerConfig){
        .max_blocks = 10000,
        .default_reductions = 10000,
        .num_workers = 0,
        .enable_stealing = true,
    };
}

/* Block Registry */

static void registry_init(BlockRegistry *reg) {
    atomic_store(&reg->total_count, 0);

    for (size_t i = 0; i < REGISTRY_SHARDS; i++) {
        RegistryShard *shard = &reg->shards[i];
        shard->capacity = REGISTRY_INITIAL_CAPACITY;
        shard->count = 0;
        shard->buckets = calloc(shard->capacity, sizeof(BlockEntry *));
        pthread_mutex_init(&shard->lock, NULL);
    }
}

static void registry_free(BlockRegistry *reg) {
    for (size_t i = 0; i < REGISTRY_SHARDS; i++) {
        RegistryShard *shard = &reg->shards[i];
        pthread_mutex_lock(&shard->lock);

        for (size_t j = 0; j < shard->capacity; j++) {
            BlockEntry *entry = shard->buckets[j];
            while (entry) {
                BlockEntry *next = entry->next;
                if (entry->block) {
                    block_free(entry->block);
                }
                free(entry);
                entry = next;
            }
        }
        free(shard->buckets);
        pthread_mutex_unlock(&shard->lock);
        pthread_mutex_destroy(&shard->lock);
    }
}

static inline size_t registry_shard_index(Pid pid) {
    return pid % REGISTRY_SHARDS;
}

static inline size_t registry_bucket_index(Pid pid, size_t capacity) {
    return (pid / REGISTRY_SHARDS) % capacity;
}

static bool registry_grow_shard(RegistryShard *shard) {
    size_t new_capacity = shard->capacity * 2;
    BlockEntry **new_buckets = calloc(new_capacity, sizeof(BlockEntry *));
    if (!new_buckets) {
        return false;  /* Keep using old buckets */
    }

    for (size_t i = 0; i < shard->capacity; i++) {
        BlockEntry *entry = shard->buckets[i];
        while (entry) {
            BlockEntry *next = entry->next;
            size_t new_idx = registry_bucket_index(entry->pid, new_capacity);
            entry->next = new_buckets[new_idx];
            new_buckets[new_idx] = entry;
            entry = next;
        }
    }

    free(shard->buckets);
    shard->buckets = new_buckets;
    shard->capacity = new_capacity;
    return true;
}

/* Internal insert without updating total_count (caller handles it) */
static bool registry_insert_internal(BlockRegistry *reg, Block *block) {
    Pid pid = block->pid;
    size_t shard_idx = registry_shard_index(pid);
    RegistryShard *shard = &reg->shards[shard_idx];

    pthread_mutex_lock(&shard->lock);

    if (shard->count * 4 >= shard->capacity * 3) {
        /* Try to grow, but continue if it fails (more collisions, but works) */
        (void)registry_grow_shard(shard);
    }

    size_t bucket_idx = registry_bucket_index(pid, shard->capacity);

    BlockEntry *entry = malloc(sizeof(BlockEntry));
    if (!entry) {
        pthread_mutex_unlock(&shard->lock);
        return false;
    }

    entry->pid = pid;
    entry->block = block;
    entry->next = shard->buckets[bucket_idx];
    shard->buckets[bucket_idx] = entry;
    shard->count++;

    pthread_mutex_unlock(&shard->lock);

    return true;
}

__attribute__((unused))
static bool registry_insert(BlockRegistry *reg, Block *block) {
    if (!registry_insert_internal(reg, block)) {
        return false;
    }
    atomic_fetch_add(&reg->total_count, 1);
    return true;
}

static Block *registry_lookup(BlockRegistry *reg, Pid pid) {
    size_t shard_idx = registry_shard_index(pid);
    RegistryShard *shard = &reg->shards[shard_idx];

    pthread_mutex_lock(&shard->lock);

    size_t bucket_idx = registry_bucket_index(pid, shard->capacity);
    BlockEntry *entry = shard->buckets[bucket_idx];

    while (entry) {
        if (entry->pid == pid) {
            Block *block = entry->block;
            pthread_mutex_unlock(&shard->lock);
            return block;
        }
        entry = entry->next;
    }

    pthread_mutex_unlock(&shard->lock);
    return NULL;
}

__attribute__((unused))
static void registry_remove(BlockRegistry *reg, Pid pid) {
    size_t shard_idx = registry_shard_index(pid);
    RegistryShard *shard = &reg->shards[shard_idx];

    pthread_mutex_lock(&shard->lock);

    size_t bucket_idx = registry_bucket_index(pid, shard->capacity);
    BlockEntry **entry_ptr = &shard->buckets[bucket_idx];

    while (*entry_ptr) {
        BlockEntry *entry = *entry_ptr;
        if (entry->pid == pid) {
            *entry_ptr = entry->next;
            free(entry);
            shard->count--;
            pthread_mutex_unlock(&shard->lock);
            atomic_fetch_sub(&reg->total_count, 1);
            return;
        }
        entry_ptr = &entry->next;
    }

    pthread_mutex_unlock(&shard->lock);
}

typedef void (*BlockIteratorFn)(Block *block, void *ctx);

static void registry_iterate(BlockRegistry *reg, BlockIteratorFn fn, void *ctx) {
    for (size_t i = 0; i < REGISTRY_SHARDS; i++) {
        RegistryShard *shard = &reg->shards[i];
        pthread_mutex_lock(&shard->lock);

        for (size_t j = 0; j < shard->capacity; j++) {
            BlockEntry *entry = shard->buckets[j];
            while (entry) {
                if (entry->block) {
                    fn(entry->block, ctx);
                }
                entry = entry->next;
            }
        }

        pthread_mutex_unlock(&shard->lock);
    }
}

/* Run Queue */

static void runqueue_init(RunQueue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    pthread_mutex_init(&queue->lock, NULL);
}

static void runqueue_push(RunQueue *queue, Block *block) {
    block->next = NULL;
    block->prev = queue->tail;

    if (queue->tail) {
        queue->tail->next = block;
    } else {
        queue->head = block;
    }
    queue->tail = block;
    queue->count++;
}

static Block *runqueue_pop(RunQueue *queue) {
    Block *block = queue->head;
    if (!block) return NULL;

    queue->head = block->next;
    if (queue->head) {
        queue->head->prev = NULL;
    } else {
        queue->tail = NULL;
    }

    block->next = NULL;
    block->prev = NULL;
    queue->count--;

    return block;
}

static void runqueue_remove_internal(RunQueue *queue, Block *block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        queue->head = block->next;
    }

    if (block->next) {
        block->next->prev = block->prev;
    } else {
        queue->tail = block->prev;
    }

    block->next = NULL;
    block->prev = NULL;
    queue->count--;
}

__attribute__((unused))
static void runqueue_remove(RunQueue *queue, Block *block) {
    runqueue_remove_internal(queue, block);
}

/* Thread-safe version for multi-threaded scheduler */
static void scheduler_runqueue_remove(Scheduler *scheduler, Block *block) {
    if (scheduler->worker_count > 0) {
        pthread_mutex_lock(&scheduler->run_queue.lock);
        runqueue_remove_internal(&scheduler->run_queue, block);
        pthread_mutex_unlock(&scheduler->run_queue.lock);
    } else {
        runqueue_remove_internal(&scheduler->run_queue, block);
    }
}

/* Scheduler Lifecycle */

Scheduler *scheduler_new(const SchedulerConfig *config) {
    Scheduler *scheduler = malloc(sizeof(Scheduler));
    if (!scheduler) return NULL;

    if (config) {
        scheduler->config = *config;
    } else {
        scheduler->config = scheduler_config_default();
    }

    registry_init(&scheduler->registry);

    atomic_store(&scheduler->next_pid, 1);

    runqueue_init(&scheduler->run_queue);

    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    atomic_store(&scheduler->next_worker, 0);

    if (scheduler->config.num_workers > 0) {
        scheduler->workers = malloc(sizeof(Worker *) * scheduler->config.num_workers);
        if (!scheduler->workers) {
            registry_free(&scheduler->registry);
            free(scheduler);
            return NULL;
        }

        for (size_t i = 0; i < scheduler->config.num_workers; i++) {
            scheduler->workers[i] = worker_new((int)i, scheduler);
            if (!scheduler->workers[i]) {
                for (size_t j = 0; j < i; j++) {
                    worker_free(scheduler->workers[j]);
                }
                free(scheduler->workers);
                registry_free(&scheduler->registry);
                free(scheduler);
                return NULL;
            }
        }
        scheduler->worker_count = scheduler->config.num_workers;
    }

    atomic_store(&scheduler->running, false);
    scheduler->current = NULL;

    pthread_mutex_init(&scheduler->block_mutex, NULL);

    scheduler->primitives = NULL;
    scheduler->groups = NULL;
    scheduler->tracer = NULL;

    atomic_store(&scheduler->total_spawned, 0);
    atomic_store(&scheduler->total_terminated, 0);
    atomic_store(&scheduler->total_reductions, 0);
    atomic_store(&scheduler->context_switches, 0);
    atomic_store(&scheduler->blocks_in_flight, 0);

    scheduler->start_time_ms = timer_current_time_ms();

    return scheduler;
}

void scheduler_free(Scheduler *scheduler) {
    if (!scheduler) return;

    if (scheduler->workers) {
        for (size_t i = 0; i < scheduler->worker_count; i++) {
            if (scheduler->workers[i]) {
                worker_stop(scheduler->workers[i]);
            }
        }
        for (size_t i = 0; i < scheduler->worker_count; i++) {
            if (scheduler->workers[i]) {
                worker_free(scheduler->workers[i]);
            }
        }
        free(scheduler->workers);
    }

    registry_free(&scheduler->registry);

    pthread_mutex_destroy(&scheduler->run_queue.lock);

    if (scheduler->groups) {
        procgroup_registry_free(scheduler->groups);
    }

    if (scheduler->tracer) {
        tracer_free(scheduler->tracer);
    }

    pthread_mutex_destroy(&scheduler->block_mutex);

    free(scheduler);
}

/* Block Management */

static bool register_block(Scheduler *scheduler, Block *block) {
    size_t max = scheduler->config.max_blocks;

    /* Use CAS loop to atomically check and increment count, avoiding TOCTOU race */
    size_t current = atomic_load(&scheduler->registry.total_count);
    while (current < max) {
        if (atomic_compare_exchange_weak(&scheduler->registry.total_count,
                                          &current, current + 1)) {
            /* Successfully reserved a slot, now insert */
            if (!registry_insert_internal(&scheduler->registry, block)) {
                /* Insert failed, revert the count */
                atomic_fetch_sub(&scheduler->registry.total_count, 1);
                return false;
            }
            return true;
        }
        /* CAS failed, current was updated - retry */
    }
    return false;  /* Max blocks reached */
}

bool scheduler_register_block(Scheduler *scheduler, Block *block) {
    if (!scheduler || !block) return false;
    return register_block(scheduler, block);
}

Pid scheduler_spawn(Scheduler *scheduler, Bytecode *code, const char *name) {
    /* Use CAP_NONE as secure default - callers should use scheduler_spawn_ex
     * with explicit capabilities for privileged operations */
    return scheduler_spawn_ex(scheduler, code, name, CAP_NONE, NULL);
}

Pid scheduler_spawn_ex(Scheduler *scheduler, Bytecode *code, const char *name,
                       CapabilitySet caps, const BlockLimits *limits) {
    if (!scheduler || !code) return PID_INVALID;

    Pid pid = atomic_fetch_add(&scheduler->next_pid, 1);

    Block *block = block_new(pid, name, limits);
    if (!block) return PID_INVALID;

    block->capabilities = caps;

    if (!block_load(block, code)) {
        block_free(block);
        return PID_INVALID;
    }

    if (!register_block(scheduler, block)) {
        block_free(block);
        return PID_INVALID;
    }

    block->vm->scheduler = scheduler;

    if (scheduler->primitives) {
        tools_register_from_bytecode(&scheduler->primitives->tools, code, block->vm);
    }

    if (scheduler->worker_count > 0) {
        size_t worker_idx = atomic_fetch_add(&scheduler->next_worker, 1) %
                            scheduler->worker_count;
        worker_enqueue(scheduler->workers[worker_idx], block);
    } else {
        scheduler_enqueue(scheduler, block);
    }

    atomic_fetch_add(&scheduler->total_spawned, 1);

    return pid;
}

Block *scheduler_get_block(Scheduler *scheduler, Pid pid) {
    if (!scheduler || pid == PID_INVALID) return NULL;
    return registry_lookup(&scheduler->registry, pid);
}

void scheduler_kill(Scheduler *scheduler, Pid pid) {
    Block *block = scheduler_get_block(scheduler, pid);
    if (!block) return;

    if (block_is_alive(block)) {
        block_crash(block, "killed");

        if (atomic_load(&block->state) == BLOCK_RUNNABLE) {
            scheduler_runqueue_remove(scheduler, block);
        }

        scheduler->total_terminated++;

        /* Propagate exit to linked blocks */
        scheduler_propagate_exit(scheduler, block);
    }
}

void scheduler_propagate_exit(Scheduler *scheduler, Block *exited_block) {
    if (!scheduler || !exited_block) return;

    /* Get exit info */
    bool abnormal = exited_block->u.exit.exit_code != 0 ||
                    exited_block->u.exit.exit_reason != NULL;
    Pid exited_pid = exited_block->pid;
    ExitReason reason = abnormal ? EXIT_CRASH : EXIT_NORMAL;

    /* Notify supervisor if any */
    if (exited_block->supervisor && exited_block->parent != PID_INVALID) {
        Block *parent_block = scheduler_get_block(scheduler, exited_block->parent);
        if (parent_block) {
            supervisor_handle_exit(exited_block->supervisor, scheduler, parent_block,
                                  exited_pid, reason, exited_block->u.exit.exit_code,
                                  exited_block->u.exit.exit_reason);
        }
    }

    /* Iterate linked blocks */
    for (uint32_t i = 0; i < exited_block->link_count; i++) {
        Pid linked_pid = exited_block->links[i];
        Block *linked_block = scheduler_get_block(scheduler, linked_pid);
        if (!linked_block || !block_is_alive(linked_block)) continue;

        /* Remove the link from the other side */
        block_unlink(linked_block, exited_pid);

        if (block_has_cap(linked_block, CAP_TRAP_EXIT)) {
            /* Send exit message to block that traps exits */
            Value *exit_msg = value_map();
            map_set(exit_msg, "type", value_string("exit"));
            map_set(exit_msg, "pid", value_pid(exited_pid));
            map_set(exit_msg, "code", value_int(exited_block->u.exit.exit_code));
            if (exited_block->u.exit.exit_reason) {
                map_set(exit_msg, "reason", value_string(exited_block->u.exit.exit_reason));
            }
            block_send(linked_block, exited_pid, exit_msg);

            /* Wake the block if it was waiting */
            if (atomic_load(&linked_block->state) == BLOCK_WAITING) {
                scheduler_wake_block(scheduler, linked_block);
            }
        } else if (abnormal) {
            /* Crash linked block if exit was abnormal */
            char reason[256];
            snprintf(reason, sizeof(reason), "linked process %lu crashed", exited_pid);
            block_crash(linked_block, reason);

            if (atomic_load(&linked_block->state) == BLOCK_RUNNABLE) {
                scheduler_runqueue_remove(scheduler, linked_block);
            }

            atomic_fetch_add(&scheduler->total_terminated, 1);

            /* Recursively propagate (with care for cycles - unlink was done above) */
            scheduler_propagate_exit(scheduler, linked_block);
        }
    }
}

Block *scheduler_current(Scheduler *scheduler) {
    return scheduler ? scheduler->current : NULL;
}

/* Execution */

void scheduler_enqueue(Scheduler *scheduler, Block *block) {
    if (!scheduler || !block) return;

    if (atomic_load(&block->state) == BLOCK_RUNNABLE) {
        if (scheduler->worker_count > 0) {
            pthread_mutex_lock(&scheduler->run_queue.lock);
            runqueue_push(&scheduler->run_queue, block);
            pthread_mutex_unlock(&scheduler->run_queue.lock);
        } else {
            runqueue_push(&scheduler->run_queue, block);
        }
    }
}

Block *scheduler_dequeue(Scheduler *scheduler) {
    if (!scheduler) return NULL;

    if (scheduler->worker_count > 0) {
        pthread_mutex_lock(&scheduler->run_queue.lock);
        Block *block = runqueue_pop(&scheduler->run_queue);
        pthread_mutex_unlock(&scheduler->run_queue.lock);
        return block;
    }
    return runqueue_pop(&scheduler->run_queue);
}

bool scheduler_queue_empty(const Scheduler *scheduler) {
    return !scheduler || scheduler->run_queue.count == 0;
}

typedef struct {
    size_t waiting_count;
    size_t waiting_with_messages;
} WaitingBlocksInfo;

static void check_waiting_callback(Block *block, void *ctx) {
    WaitingBlocksInfo *info = (WaitingBlocksInfo *)ctx;
    if (atomic_load(&block->state) == BLOCK_WAITING) {
        info->waiting_count++;
        if (block_has_messages(block)) {
            info->waiting_with_messages++;
        }
    }
}

static bool has_wakeable_waiting_blocks(Scheduler *scheduler) {
    WaitingBlocksInfo info = {0, 0};
    registry_iterate(&scheduler->registry, check_waiting_callback, &info);

    if (info.waiting_with_messages > 0) {
        return true;
    }

    return false;
}

bool scheduler_step(Scheduler *scheduler) {
    if (!scheduler) return false;

    Block *block = scheduler_dequeue(scheduler);
    if (!block) {
        if (has_wakeable_waiting_blocks(scheduler)) {
            return true;
        }
        return false;
    }

    scheduler->current = block;
    scheduler->context_switches++;

    BlockRunResult result = block_run(block);

    scheduler->total_reductions += block->counters.reductions;
    scheduler->current = NULL;

    switch (result) {
    case BLOCK_RUN_YIELD:
        scheduler_enqueue(scheduler, block);
        break;

    case BLOCK_RUN_WAITING:
        break;

    case BLOCK_RUN_OK:
    case BLOCK_RUN_HALTED:
    case BLOCK_RUN_ERROR:
        scheduler->total_terminated++;

        const char *exit_reason_str = (result == BLOCK_RUN_ERROR)
            ? (block->u.exit.exit_reason ? block->u.exit.exit_reason : "error")
            : "normal";
        bool is_abnormal = (result == BLOCK_RUN_ERROR);

        /* Notify linked blocks */
        {
            size_t link_count;
            const Pid *links = block_get_links(block, &link_count);
            for (size_t i = 0; i < link_count; i++) {
                Block *linked = scheduler_get_block(scheduler, links[i]);
                if (linked && block_is_alive(linked)) {
                    if (block_has_cap(linked, CAP_TRAP_EXIT)) {
                        Value *exit_msg = value_map();
                        exit_msg = map_set(exit_msg, "type", value_string("exit"));
                        exit_msg = map_set(exit_msg, "pid", value_pid(block->pid));
                        exit_msg = map_set(exit_msg, "reason", value_string(exit_reason_str));
                        exit_msg = map_set(exit_msg, "code", value_int(block->u.exit.exit_code));

                        if (block_send(linked, block->pid, exit_msg)) {
                            if (block_try_transition(linked, BLOCK_WAITING, BLOCK_RUNNABLE)) {
                                scheduler_enqueue(scheduler, linked);
                            }
                        }
                    } else if (is_abnormal) {
                        block_crash(linked, "linked process crashed");
                        if (atomic_load(&linked->state) == BLOCK_RUNNABLE) {
                            scheduler_runqueue_remove(scheduler, linked);
                        }
                    }
                    block_unlink(linked, block->pid);
                }
            }
        }

        /* Notify monitors */
        {
            for (size_t i = 0; i < block->monitored_by_count; i++) {
                Block *monitor = scheduler_get_block(scheduler, block->monitored_by[i]);
                if (monitor && block_is_alive(monitor)) {
                    Value *down_msg = value_map();
                    down_msg = map_set(down_msg, "type", value_string("down"));
                    down_msg = map_set(down_msg, "pid", value_pid(block->pid));
                    down_msg = map_set(down_msg, "reason", value_string(exit_reason_str));
                    down_msg = map_set(down_msg, "code", value_int(block->u.exit.exit_code));

                    if (block_send(monitor, block->pid, down_msg)) {
                        if (block_try_transition(monitor, BLOCK_WAITING, BLOCK_RUNNABLE)) {
                            scheduler_enqueue(scheduler, monitor);
                        }
                    }
                    block_demonitor(monitor, block->pid);
                }
            }
        }
        break;
    }

    return true;
}

void scheduler_run(Scheduler *scheduler) {
    if (!scheduler) return;

    atomic_store(&scheduler->running, true);

    if (scheduler->worker_count > 0) {
        for (size_t i = 0; i < scheduler->worker_count; i++) {
            worker_start(scheduler->workers[i]);
        }

        for (size_t i = 0; i < scheduler->worker_count; i++) {
            worker_join(scheduler->workers[i]);
        }

        for (size_t i = 0; i < scheduler->worker_count; i++) {
            Worker *w = scheduler->workers[i];
            atomic_fetch_add(&scheduler->total_reductions,
                             atomic_load(&w->total_reductions));
            atomic_fetch_add(&scheduler->context_switches,
                             atomic_load(&w->blocks_executed));
        }
    } else {
        while (atomic_load(&scheduler->running)) {
            if (!scheduler_step(scheduler)) {
                break;
            }
        }
    }

    atomic_store(&scheduler->running, false);
}

void scheduler_stop(Scheduler *scheduler) {
    if (!scheduler) return;

    atomic_store(&scheduler->running, false);

    if (scheduler->workers) {
        for (size_t i = 0; i < scheduler->worker_count; i++) {
            if (scheduler->workers[i]) {
                worker_stop(scheduler->workers[i]);
            }
        }
    }
}

/* Primitives */

void scheduler_set_primitives(Scheduler *scheduler, PrimitivesRuntime *primitives) {
    if (scheduler) {
        scheduler->primitives = primitives;
    }
}

PrimitivesRuntime *scheduler_get_primitives(Scheduler *scheduler) {
    return scheduler ? scheduler->primitives : NULL;
}

/* Statistics */

static void count_block_states_callback(Block *block, void *ctx) {
    SchedulerStats *stats = (SchedulerStats *)ctx;
    switch (atomic_load(&block->state)) {
    case BLOCK_RUNNABLE:
    case BLOCK_RUNNING:
        stats->blocks_runnable++;
        stats->blocks_alive++;
        break;
    case BLOCK_WAITING:
        stats->blocks_waiting++;
        stats->blocks_alive++;
        break;
    case BLOCK_DEAD:
        stats->blocks_dead++;
        break;
    }
}

SchedulerStats scheduler_stats(const Scheduler *scheduler) {
    SchedulerStats stats = {0};
    if (!scheduler) return stats;

    stats.blocks_total = scheduler->total_spawned;
    stats.total_reductions = scheduler->total_reductions;
    stats.context_switches = scheduler->context_switches;

    registry_iterate((BlockRegistry *)&scheduler->registry, count_block_states_callback, &stats);

    return stats;
}

void scheduler_print_stats(const Scheduler *scheduler) {
    SchedulerStats stats = scheduler_stats(scheduler);

    printf("Scheduler Statistics:\n");
    printf("  Blocks total:     %zu\n", stats.blocks_total);
    printf("  Blocks alive:     %zu\n", stats.blocks_alive);
    printf("  Blocks runnable:  %zu\n", stats.blocks_runnable);
    printf("  Blocks waiting:   %zu\n", stats.blocks_waiting);
    printf("  Blocks dead:      %zu\n", stats.blocks_dead);
    printf("  Total reductions: %zu\n", stats.total_reductions);
    printf("  Context switches: %zu\n", stats.context_switches);
}

/* Debug */

static void print_block_callback(Block *block, void *ctx) {
    size_t *index = (size_t *)ctx;
    printf("    [%zu] pid=%lu name=%s state=%s\n",
           (*index)++, block->pid,
           block->name ? block->name : "(none)",
           block_state_name(atomic_load(&block->state)));
}

void scheduler_print(const Scheduler *scheduler) {
    if (!scheduler) {
        printf("Scheduler: (null)\n");
        return;
    }

    printf("Scheduler {\n");
    printf("  running: %s\n", atomic_load(&scheduler->running) ? "yes" : "no");
    printf("  next_pid: %lu\n", atomic_load(&scheduler->next_pid));
    printf("  workers: %zu\n", scheduler->worker_count);
    printf("  run_queue: %zu blocks\n", scheduler->run_queue.count);
    printf("  total_blocks: %zu\n", atomic_load(&scheduler->registry.total_count));
    printf("  blocks:\n");

    size_t index = 0;
    registry_iterate((BlockRegistry *)&scheduler->registry, print_block_callback, &index);

    printf("}\n");
}

/* Multi-threaded */

bool scheduler_is_multithreaded(const Scheduler *scheduler) {
    return scheduler && scheduler->worker_count > 0;
}

size_t scheduler_worker_count(const Scheduler *scheduler) {
    return scheduler ? scheduler->worker_count : 0;
}

Worker *scheduler_get_worker(Scheduler *scheduler, size_t index) {
    if (!scheduler || index >= scheduler->worker_count) return NULL;
    return scheduler->workers[index];
}

void scheduler_wake_block(Scheduler *scheduler, Block *block) {
    if (!scheduler || !block) return;

    if (block_try_transition(block, BLOCK_WAITING, BLOCK_RUNNABLE)) {

        if (scheduler->worker_count > 0) {
            size_t worker_idx = atomic_fetch_add(&scheduler->next_worker, 1) %
                                scheduler->worker_count;
            worker_enqueue(scheduler->workers[worker_idx], block);
        } else {
            scheduler_enqueue(scheduler, block);
        }
    }
}

/* Block Count */

size_t scheduler_block_count(const Scheduler *scheduler) {
    if (!scheduler) return 0;
    return atomic_load(&scheduler->registry.total_count);
}

/* Process Groups */

ProcessGroupRegistry *scheduler_get_groups(Scheduler *scheduler) {
    if (!scheduler) return NULL;

    if (!scheduler->groups) {
        scheduler->groups = procgroup_registry_new();
    }
    return scheduler->groups;
}

/* Tracing */

Tracer *scheduler_get_tracer(Scheduler *scheduler) {
    return scheduler ? scheduler->tracer : NULL;
}

void scheduler_set_tracer(Scheduler *scheduler, Tracer *tracer) {
    if (scheduler) {
        scheduler->tracer = tracer;
    }
}
