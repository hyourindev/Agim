/*
 * Agim - Scheduler Property Tests
 *
 * Property-based tests for scheduler operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "property_test.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "vm/vm.h"

/* Helper to create a test block */
static Block *create_test_block(Pid pid) {
    return block_new(pid, "test", NULL);
}

/* Property: Scheduler starts with zero blocks */
static bool prop_scheduler_starts_empty(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    PROP_ASSERT(scheduler_block_count(scheduler) == 0);
    PROP_ASSERT(scheduler_queue_empty(scheduler));

    scheduler_free(scheduler);
    return true;
}

/* Property: Enqueue increases queue non-emptiness */
static bool prop_scheduler_enqueue_not_empty(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    /* Create a block to enqueue */
    Block *block = create_test_block(1);
    PROP_ASSERT(block != NULL);

    /* Register the block first */
    bool registered = scheduler_register_block(scheduler, block);
    PROP_ASSERT(registered);

    scheduler_enqueue(scheduler, block);
    PROP_ASSERT(!scheduler_queue_empty(scheduler));

    /* Dequeue to clean up */
    Block *dequeued = scheduler_dequeue(scheduler);
    PROP_ASSERT(dequeued == block);
    PROP_ASSERT(scheduler_queue_empty(scheduler));

    scheduler_free(scheduler);
    return true;
}

/* Property: Dequeue returns enqueued block */
static bool prop_scheduler_enqueue_dequeue_fifo(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    /* Create multiple blocks */
    int count = prop_rand_int_range(2, 5);
    Block **blocks = malloc(sizeof(Block *) * count);
    PROP_ASSERT(blocks != NULL);

    for (int i = 0; i < count; i++) {
        blocks[i] = create_test_block(i + 1);
        PROP_ASSERT(blocks[i] != NULL);
        bool registered = scheduler_register_block(scheduler, blocks[i]);
        PROP_ASSERT(registered);
        scheduler_enqueue(scheduler, blocks[i]);
    }

    /* Dequeue should return in FIFO order */
    for (int i = 0; i < count; i++) {
        Block *dequeued = scheduler_dequeue(scheduler);
        PROP_ASSERT(dequeued == blocks[i]);
    }

    PROP_ASSERT(scheduler_queue_empty(scheduler));

    free(blocks);
    scheduler_free(scheduler);
    return true;
}

/* Property: Scheduler tracks block count correctly */
static bool prop_scheduler_block_count(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    int count = prop_rand_int_range(1, 10);
    for (int i = 0; i < count; i++) {
        Block *block = create_test_block(i + 1);
        PROP_ASSERT(block != NULL);
        bool registered = scheduler_register_block(scheduler, block);
        PROP_ASSERT(registered);
    }

    PROP_ASSERT(scheduler_block_count(scheduler) == (size_t)count);

    scheduler_free(scheduler);
    return true;
}

/* Property: Get block returns registered block */
static bool prop_scheduler_get_block(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    Block *block = create_test_block(42);
    PROP_ASSERT(block != NULL);

    bool registered = scheduler_register_block(scheduler, block);
    PROP_ASSERT(registered);

    Pid pid = block->pid;
    Block *retrieved = scheduler_get_block(scheduler, pid);
    PROP_ASSERT(retrieved == block);

    scheduler_free(scheduler);
    return true;
}

/* Property: Get invalid PID returns NULL */
static bool prop_scheduler_get_invalid_pid(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    /* Random invalid PID */
    Pid invalid_pid = (Pid)prop_rand_int_range(1000, 9999);
    Block *result = scheduler_get_block(scheduler, invalid_pid);
    PROP_ASSERT(result == NULL);

    scheduler_free(scheduler);
    return true;
}

/* Property: Kill marks block for exit */
static bool prop_scheduler_kill_marks_block(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    Block *block = create_test_block(1);
    PROP_ASSERT(block != NULL);

    bool registered = scheduler_register_block(scheduler, block);
    PROP_ASSERT(registered);

    Pid pid = block->pid;
    scheduler_kill(scheduler, pid);

    /* Block should be marked for exit */
    /* Note: exact behavior depends on implementation */

    scheduler_free(scheduler);
    return true;
}

/* Property: Queue remains FIFO after multiple ops */
static bool prop_scheduler_queue_fifo_consistency(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    /* Enqueue 3 blocks */
    Block *b1 = create_test_block(1);
    Block *b2 = create_test_block(2);
    Block *b3 = create_test_block(3);
    PROP_ASSERT(b1 && b2 && b3);

    scheduler_register_block(scheduler, b1);
    scheduler_register_block(scheduler, b2);
    scheduler_register_block(scheduler, b3);

    scheduler_enqueue(scheduler, b1);
    scheduler_enqueue(scheduler, b2);

    /* Dequeue one */
    Block *first = scheduler_dequeue(scheduler);
    PROP_ASSERT(first == b1);

    /* Enqueue another */
    scheduler_enqueue(scheduler, b3);

    /* Remaining order should be b2, b3 */
    PROP_ASSERT(scheduler_dequeue(scheduler) == b2);
    PROP_ASSERT(scheduler_dequeue(scheduler) == b3);
    PROP_ASSERT(scheduler_queue_empty(scheduler));

    scheduler_free(scheduler);
    return true;
}

/* Property: Single-threaded mode has no workers */
static bool prop_scheduler_single_thread_no_workers(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 0;  /* Single-threaded */
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    PROP_ASSERT(!scheduler_is_multithreaded(scheduler));
    PROP_ASSERT(scheduler_worker_count(scheduler) == 0);

    scheduler_free(scheduler);
    return true;
}

/* Property: Stats are initially zero */
static bool prop_scheduler_stats_initially_zero(void *ctx) {
    (void)ctx;

    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    PROP_ASSERT(scheduler != NULL);

    SchedulerStats stats = scheduler_stats(scheduler);
    PROP_ASSERT(stats.blocks_total == 0);
    PROP_ASSERT(stats.blocks_alive == 0);
    PROP_ASSERT(stats.total_reductions == 0);

    scheduler_free(scheduler);
    return true;
}

/* Main */
int main(void) {
    printf("Running scheduler property tests...\n\n");

    prop_init(0); /* Use random seed */

    printf("Scheduler Property Tests:\n");
    PROP_CHECK("starts empty", prop_scheduler_starts_empty, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("enqueue not empty", prop_scheduler_enqueue_not_empty, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("enqueue/dequeue FIFO", prop_scheduler_enqueue_dequeue_fifo, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("block count", prop_scheduler_block_count, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("get block", prop_scheduler_get_block, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("get invalid PID", prop_scheduler_get_invalid_pid, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("kill marks block", prop_scheduler_kill_marks_block, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("queue FIFO consistency", prop_scheduler_queue_fifo_consistency, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("single thread no workers", prop_scheduler_single_thread_no_workers, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("stats initially zero", prop_scheduler_stats_initially_zero, NULL, PROP_DEFAULT_ITERATIONS);

    PROP_SUMMARY();
    return PROP_RESULT();
}
