/*
 * Agim - Worker Lifecycle Tests
 *
 * P1.1.5.2: Tests for worker thread lifecycle.
 * - worker_new creates worker
 * - worker_free cleans up
 * - worker_start begins execution
 * - worker_stop signals stop
 * - worker_join waits for thread
 * - worker_enqueue adds blocks
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/worker.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "vm/bytecode.h"

/* Helper: Create scheduler with default config */
static Scheduler *create_test_scheduler(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 0;  /* Single-threaded for unit tests */
    return scheduler_new(&config);
}

/*
 * Test: worker_new creates worker with valid fields
 */
void test_worker_new(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);
    ASSERT_EQ(0, worker->id);
    ASSERT_EQ(scheduler, worker->scheduler);
    ASSERT(worker->vm != NULL);
    ASSERT_EQ(WORKER_IDLE, atomic_load(&worker->state));
    ASSERT(!atomic_load(&worker->thread_started));
    ASSERT_EQ(0, atomic_load(&worker->blocks_executed));
    ASSERT_EQ(0, atomic_load(&worker->steals_attempted));
    ASSERT_EQ(0, atomic_load(&worker->steals_successful));
    ASSERT_EQ(0, atomic_load(&worker->total_reductions));

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_new with different IDs
 */
void test_worker_new_multiple_ids(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    Worker *w2 = worker_new(42, scheduler);

    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);
    ASSERT(w2 != NULL);

    ASSERT_EQ(0, w0->id);
    ASSERT_EQ(1, w1->id);
    ASSERT_EQ(42, w2->id);

    worker_free(w0);
    worker_free(w1);
    worker_free(w2);
    scheduler_free(scheduler);
}

/*
 * Test: worker_new with NULL scheduler
 * Worker should still be created (scheduler can be NULL)
 */
void test_worker_new_null_scheduler(void) {
    Worker *worker = worker_new(0, NULL);
    ASSERT(worker != NULL);
    ASSERT(worker->scheduler == NULL);

    worker_free(worker);
}

/*
 * Test: worker_free handles NULL
 */
void test_worker_free_null(void) {
    worker_free(NULL);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: worker_free cleans up properly
 */
void test_worker_free_cleanup(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    /* Add some items to the deque */
    Block *block = block_new(1, "test", NULL);
    worker_enqueue(worker, block);

    worker_free(worker);
    block_free(block);
    scheduler_free(scheduler);
    ASSERT(1);  /* No crash = success */
}

/*
 * Test: worker_start changes state
 */
void test_worker_start_state(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    ASSERT_EQ(WORKER_IDLE, atomic_load(&worker->state));

    bool started = worker_start(worker);
    ASSERT(started);
    ASSERT_EQ(WORKER_RUNNING, atomic_load(&worker->state));
    ASSERT(atomic_load(&worker->thread_started));

    worker_stop(worker);
    worker_join(worker);
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_start returns false if already started
 */
void test_worker_start_already_started(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    bool first = worker_start(worker);
    ASSERT(first);

    bool second = worker_start(worker);
    ASSERT(!second);  /* Already running */

    worker_stop(worker);
    worker_join(worker);
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_start with NULL returns false
 */
void test_worker_start_null(void) {
    bool result = worker_start(NULL);
    ASSERT(!result);
}

/*
 * Test: worker_stop changes state
 */
void test_worker_stop_state(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    worker_start(worker);
    ASSERT_EQ(WORKER_RUNNING, atomic_load(&worker->state));

    worker_stop(worker);
    ASSERT_EQ(WORKER_STOPPED, atomic_load(&worker->state));

    worker_join(worker);
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_stop handles NULL
 */
void test_worker_stop_null(void) {
    worker_stop(NULL);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: worker_stop on non-started worker
 */
void test_worker_stop_not_started(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    worker_stop(worker);
    ASSERT_EQ(WORKER_STOPPED, atomic_load(&worker->state));

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_join waits for thread
 */
void test_worker_join(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    worker_start(worker);
    worker_stop(worker);
    worker_join(worker);

    /* After join, thread_started should be false */
    ASSERT(!atomic_load(&worker->thread_started));

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_join handles NULL
 */
void test_worker_join_null(void) {
    worker_join(NULL);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: worker_join on non-started worker
 */
void test_worker_join_not_started(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    worker_join(worker);  /* Should be no-op */
    ASSERT(1);

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_enqueue adds block to deque
 */
void test_worker_enqueue(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    ASSERT(deque_empty(&worker->runq));

    Block *block = block_new(1, "test", NULL);
    worker_enqueue(worker, block);

    ASSERT(!deque_empty(&worker->runq));
    ASSERT_EQ(1, deque_size(&worker->runq));

    /* Cleanup */
    deque_pop(&worker->runq);
    block_free(block);
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_enqueue multiple blocks
 */
void test_worker_enqueue_multiple(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    Block *blocks[5];
    for (int i = 0; i < 5; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        worker_enqueue(worker, blocks[i]);
    }

    ASSERT_EQ(5, deque_size(&worker->runq));

    /* Cleanup */
    for (int i = 0; i < 5; i++) {
        deque_pop(&worker->runq);
        block_free(blocks[i]);
    }
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_enqueue handles NULL worker
 */
void test_worker_enqueue_null_worker(void) {
    Block *block = block_new(1, "test", NULL);
    worker_enqueue(NULL, block);  /* Should not crash */
    block_free(block);
    ASSERT(1);
}

/*
 * Test: worker_enqueue handles NULL block
 */
void test_worker_enqueue_null_block(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    worker_enqueue(worker, NULL);  /* Should not crash */
    ASSERT(deque_empty(&worker->runq));

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Worker state transitions
 */
void test_worker_state_transitions(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    /* IDLE -> RUNNING (via start) */
    ASSERT_EQ(WORKER_IDLE, atomic_load(&worker->state));
    worker_start(worker);
    ASSERT_EQ(WORKER_RUNNING, atomic_load(&worker->state));

    /* RUNNING -> STOPPED (via stop) */
    worker_stop(worker);
    ASSERT_EQ(WORKER_STOPPED, atomic_load(&worker->state));

    worker_join(worker);

    /* After join, state goes back to IDLE */
    ASSERT_EQ(WORKER_IDLE, atomic_load(&worker->state));

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Worker RNG state is initialized
 */
void test_worker_rng_initialized(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);

    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    /* RNG states should be non-zero and different */
    ASSERT(w0->rng_state != 0);
    ASSERT(w1->rng_state != 0);
    ASSERT(w0->rng_state != w1->rng_state);

    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: Worker VM is independent
 */
void test_worker_vm_independent(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);

    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);
    ASSERT(w0->vm != w1->vm);

    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: Worker counters are atomic
 */
void test_worker_counters_atomic(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    /* Test that counters can be atomically updated */
    atomic_fetch_add(&worker->blocks_executed, 10);
    ASSERT_EQ(10, atomic_load(&worker->blocks_executed));

    atomic_fetch_add(&worker->steals_attempted, 5);
    ASSERT_EQ(5, atomic_load(&worker->steals_attempted));

    atomic_fetch_add(&worker->steals_successful, 3);
    ASSERT_EQ(3, atomic_load(&worker->steals_successful));

    atomic_fetch_add(&worker->total_reductions, 1000);
    ASSERT_EQ(1000, atomic_load(&worker->total_reductions));

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Multiple workers can be created
 */
void test_worker_multiple_creation(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *workers[10];
    for (int i = 0; i < 10; i++) {
        workers[i] = worker_new(i, scheduler);
        ASSERT(workers[i] != NULL);
    }

    /* Verify all are distinct */
    for (int i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            ASSERT(workers[i] != workers[j]);
            ASSERT(workers[i]->vm != workers[j]->vm);
        }
    }

    for (int i = 0; i < 10; i++) {
        worker_free(workers[i]);
    }
    scheduler_free(scheduler);
}

/*
 * Test: Worker start/stop cycle
 */
void test_worker_start_stop_cycle(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    /* Cycle: start -> stop -> join */
    for (int cycle = 0; cycle < 3; cycle++) {
        ASSERT_EQ(WORKER_IDLE, atomic_load(&worker->state));

        bool started = worker_start(worker);
        ASSERT(started);

        worker_stop(worker);
        worker_join(worker);
    }

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: mt_scheduler_config_default returns valid config
 */
void test_mt_scheduler_config_default(void) {
    MTSchedulerConfig config = mt_scheduler_config_default();

    ASSERT(config.num_workers >= 1);
    ASSERT(config.max_blocks > 0);
    ASSERT(config.default_reductions > 0);
    /* enable_stealing can be true or false */
}

int main(void) {
    printf("Running worker lifecycle tests...\n");

    printf("\nworker_new tests:\n");
    RUN_TEST(test_worker_new);
    RUN_TEST(test_worker_new_multiple_ids);
    RUN_TEST(test_worker_new_null_scheduler);

    printf("\nworker_free tests:\n");
    RUN_TEST(test_worker_free_null);
    RUN_TEST(test_worker_free_cleanup);

    printf("\nworker_start tests:\n");
    RUN_TEST(test_worker_start_state);
    RUN_TEST(test_worker_start_already_started);
    RUN_TEST(test_worker_start_null);

    printf("\nworker_stop tests:\n");
    RUN_TEST(test_worker_stop_state);
    RUN_TEST(test_worker_stop_null);
    RUN_TEST(test_worker_stop_not_started);

    printf("\nworker_join tests:\n");
    RUN_TEST(test_worker_join);
    RUN_TEST(test_worker_join_null);
    RUN_TEST(test_worker_join_not_started);

    printf("\nworker_enqueue tests:\n");
    RUN_TEST(test_worker_enqueue);
    RUN_TEST(test_worker_enqueue_multiple);
    RUN_TEST(test_worker_enqueue_null_worker);
    RUN_TEST(test_worker_enqueue_null_block);

    printf("\nState transition tests:\n");
    RUN_TEST(test_worker_state_transitions);

    printf("\nInitialization tests:\n");
    RUN_TEST(test_worker_rng_initialized);
    RUN_TEST(test_worker_vm_independent);
    RUN_TEST(test_worker_counters_atomic);

    printf("\nMultiple workers tests:\n");
    RUN_TEST(test_worker_multiple_creation);

    printf("\nCycle tests:\n");
    RUN_TEST(test_worker_start_stop_cycle);

    printf("\nConfig tests:\n");
    RUN_TEST(test_mt_scheduler_config_default);

    return TEST_RESULT();
}
