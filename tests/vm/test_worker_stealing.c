/*
 * Agim - Worker Stealing Tests
 *
 * P1.1.5.3: Tests for work stealing between workers.
 * - worker_steal steals from other workers
 * - Stealing requires multiple workers
 * - Random victim selection
 * - Statistics tracking
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/worker.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"

#include <stdlib.h>

/* Helper: Create scheduler with default config */
static Scheduler *create_test_scheduler(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 0;  /* Single-threaded for unit tests */
    return scheduler_new(&config);
}

/*
 * Test: worker_steal returns NULL with NULL worker
 */
void test_steal_null_worker(void) {
    Block *stolen = worker_steal(NULL);
    ASSERT(stolen == NULL);
}

/*
 * Test: worker_steal returns NULL with NULL scheduler
 */
void test_steal_null_scheduler(void) {
    Worker *worker = worker_new(0, NULL);
    ASSERT(worker != NULL);

    Block *stolen = worker_steal(worker);
    ASSERT(stolen == NULL);

    worker_free(worker);
}

/*
 * Test: worker_steal returns NULL with single worker
 */
void test_steal_single_worker(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    scheduler->worker_count = 1;
    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    Block *block = block_new(1, "test", NULL);
    worker_enqueue(worker, block);

    /* Can't steal from self */
    Block *stolen = worker_steal(worker);
    ASSERT(stolen == NULL);

    /* Block should still be in our deque */
    ASSERT(!deque_empty(&worker->runq));

    deque_pop(&worker->runq);
    block_free(block);
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: worker_steal can steal from another worker
 */
void test_steal_from_other_worker(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    /* Set up two workers */
    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(2 * sizeof(Worker *));
    ASSERT(scheduler->workers != NULL);
    scheduler->workers[0] = w0;
    scheduler->workers[1] = w1;
    scheduler->worker_count = 2;

    /* Add work to w1 */
    Block *block = block_new(1, "test", NULL);
    worker_enqueue(w1, block);

    /* w0 should be able to steal from w1 */
    Block *stolen = worker_steal(w0);
    ASSERT_EQ(block, stolen);

    /* w1's deque should be empty now */
    ASSERT(deque_empty(&w1->runq));

    block_free(block);
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: worker_steal returns NULL when all deques empty
 */
void test_steal_all_empty(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(2 * sizeof(Worker *));
    scheduler->workers[0] = w0;
    scheduler->workers[1] = w1;
    scheduler->worker_count = 2;

    /* Both deques are empty */
    Block *stolen = worker_steal(w0);
    ASSERT(stolen == NULL);

    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: worker_steal doesn't steal from self
 */
void test_steal_skips_self(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(2 * sizeof(Worker *));
    scheduler->workers[0] = w0;
    scheduler->workers[1] = w1;
    scheduler->worker_count = 2;

    /* Add work only to w0 */
    Block *block = block_new(1, "test", NULL);
    worker_enqueue(w0, block);

    /* w0 tries to steal - should not steal from self */
    Block *stolen = worker_steal(w0);
    ASSERT(stolen == NULL);

    /* Block should still be in w0's deque */
    ASSERT(!deque_empty(&w0->runq));

    deque_pop(&w0->runq);
    block_free(block);
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: Steal increments steals_attempted counter
 */
void test_steal_increments_attempted(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(2 * sizeof(Worker *));
    scheduler->workers[0] = w0;
    scheduler->workers[1] = w1;
    scheduler->worker_count = 2;

    ASSERT_EQ(0, atomic_load(&w0->steals_attempted));

    worker_steal(w0);
    ASSERT_EQ(1, atomic_load(&w0->steals_attempted));

    worker_steal(w0);
    ASSERT_EQ(2, atomic_load(&w0->steals_attempted));

    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: Multiple steals from same victim
 */
void test_steal_multiple_from_same(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(2 * sizeof(Worker *));
    scheduler->workers[0] = w0;
    scheduler->workers[1] = w1;
    scheduler->worker_count = 2;

    /* Add multiple blocks to w1 */
    Block *blocks[5];
    for (int i = 0; i < 5; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        worker_enqueue(w1, blocks[i]);
    }

    /* Steal all from w1 */
    for (int i = 0; i < 5; i++) {
        Block *stolen = worker_steal(w0);
        ASSERT_EQ(blocks[i], stolen);  /* FIFO order for steal */
    }

    /* No more to steal */
    ASSERT(worker_steal(w0) == NULL);

    for (int i = 0; i < 5; i++) {
        block_free(blocks[i]);
    }
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: Steal from multiple victims
 */
void test_steal_from_multiple_victims(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *workers[4];
    for (int i = 0; i < 4; i++) {
        workers[i] = worker_new(i, scheduler);
        ASSERT(workers[i] != NULL);
    }

    scheduler->workers = malloc(4 * sizeof(Worker *));
    for (int i = 0; i < 4; i++) {
        scheduler->workers[i] = workers[i];
    }
    scheduler->worker_count = 4;

    /* Add work to workers 1, 2, 3 (not 0) */
    Block *blocks[3];
    for (int i = 0; i < 3; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        worker_enqueue(workers[i + 1], blocks[i]);
    }

    /* Worker 0 steals - should get all 3 eventually */
    int stolen_count = 0;
    for (int attempt = 0; attempt < 10 && stolen_count < 3; attempt++) {
        Block *stolen = worker_steal(workers[0]);
        if (stolen) {
            stolen_count++;
        }
    }

    ASSERT_EQ(3, stolen_count);

    for (int i = 0; i < 3; i++) {
        block_free(blocks[i]);
    }
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    for (int i = 0; i < 4; i++) {
        worker_free(workers[i]);
    }
    scheduler_free(scheduler);
}

/*
 * Test: Steal with NULL victim in array
 */
void test_steal_null_victim_in_array(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(3 * sizeof(Worker *));
    scheduler->workers[0] = w0;
    scheduler->workers[1] = NULL;  /* NULL victim */
    scheduler->workers[2] = w1;
    scheduler->worker_count = 3;

    /* Add work to w1 */
    Block *block = block_new(1, "test", NULL);
    worker_enqueue(w1, block);

    /* w0 should skip NULL victim and find w1 */
    Block *stolen = NULL;
    for (int i = 0; i < 10 && !stolen; i++) {
        stolen = worker_steal(w0);
    }
    ASSERT_EQ(block, stolen);

    block_free(block);
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: Random victim selection covers all workers
 */
void test_steal_random_victim_selection(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    const int NUM_WORKERS = 4;
    Worker *workers[4];
    for (int i = 0; i < NUM_WORKERS; i++) {
        workers[i] = worker_new(i, scheduler);
        ASSERT(workers[i] != NULL);
    }

    scheduler->workers = malloc(NUM_WORKERS * sizeof(Worker *));
    for (int i = 0; i < NUM_WORKERS; i++) {
        scheduler->workers[i] = workers[i];
    }
    scheduler->worker_count = NUM_WORKERS;

    /* Add unique blocks to each worker except 0 */
    Block *blocks[3][10];
    for (int w = 1; w < NUM_WORKERS; w++) {
        for (int i = 0; i < 10; i++) {
            blocks[w - 1][i] = block_new((Pid)(w * 100 + i), "test", NULL);
            worker_enqueue(workers[w], blocks[w - 1][i]);
        }
    }

    /* Track which workers we stole from (by PID range) */
    int stolen_from[4] = {0, 0, 0, 0};

    /* Steal many times and track sources */
    for (int attempt = 0; attempt < 100; attempt++) {
        Block *stolen = worker_steal(workers[0]);
        if (stolen) {
            int source = stolen->pid / 100;
            if (source >= 1 && source < NUM_WORKERS) {
                stolen_from[source]++;
            }
        }
    }

    /* Should have stolen from all non-self workers */
    ASSERT(stolen_from[1] > 0);
    ASSERT(stolen_from[2] > 0);
    ASSERT(stolen_from[3] > 0);

    /* Cleanup */
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < 10; i++) {
            block_free(blocks[w][i]);
        }
    }
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    for (int i = 0; i < NUM_WORKERS; i++) {
        worker_free(workers[i]);
    }
    scheduler_free(scheduler);
}

/*
 * Test: Large scale stealing
 */
void test_steal_large_scale(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(2 * sizeof(Worker *));
    scheduler->workers[0] = w0;
    scheduler->workers[1] = w1;
    scheduler->worker_count = 2;

    /* Add 100 blocks to w1 */
    const int COUNT = 100;
    Block **blocks = malloc(COUNT * sizeof(Block *));
    ASSERT(blocks != NULL);

    for (int i = 0; i < COUNT; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        worker_enqueue(w1, blocks[i]);
    }

    /* Steal all */
    int stolen_count = 0;
    for (int i = 0; i < COUNT; i++) {
        Block *stolen = worker_steal(w0);
        ASSERT(stolen != NULL);
        ASSERT_EQ(blocks[i], stolen);  /* FIFO order */
        stolen_count++;
    }

    ASSERT_EQ(COUNT, stolen_count);
    ASSERT(worker_steal(w0) == NULL);

    for (int i = 0; i < COUNT; i++) {
        block_free(blocks[i]);
    }
    free(blocks);
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: Interleaved push and steal
 */
void test_steal_interleaved_with_push(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(2 * sizeof(Worker *));
    scheduler->workers[0] = w0;
    scheduler->workers[1] = w1;
    scheduler->worker_count = 2;

    Block *b1 = block_new(1, "test", NULL);
    Block *b2 = block_new(2, "test", NULL);
    Block *b3 = block_new(3, "test", NULL);

    /* Push, steal, push, steal */
    worker_enqueue(w1, b1);
    ASSERT_EQ(b1, worker_steal(w0));

    worker_enqueue(w1, b2);
    worker_enqueue(w1, b3);
    ASSERT_EQ(b2, worker_steal(w0));
    ASSERT_EQ(b3, worker_steal(w0));

    block_free(b1);
    block_free(b2);
    block_free(b3);
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

/*
 * Test: Steal vs pop contention (sequential test)
 */
void test_steal_vs_pop_sequential(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *w0 = worker_new(0, scheduler);
    Worker *w1 = worker_new(1, scheduler);
    ASSERT(w0 != NULL);
    ASSERT(w1 != NULL);

    scheduler->workers = malloc(2 * sizeof(Worker *));
    scheduler->workers[0] = w0;
    scheduler->workers[1] = w1;
    scheduler->worker_count = 2;

    Block *blocks[10];
    for (int i = 0; i < 10; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        worker_enqueue(w1, blocks[i]);
    }

    /* Alternate between pop (from w1) and steal (from w0) */
    Block *popped = deque_pop(&w1->runq);  /* Gets blocks[9] */
    ASSERT_EQ(blocks[9], popped);

    Block *stolen = worker_steal(w0);  /* Gets blocks[0] */
    ASSERT_EQ(blocks[0], stolen);

    popped = deque_pop(&w1->runq);  /* Gets blocks[8] */
    ASSERT_EQ(blocks[8], popped);

    stolen = worker_steal(w0);  /* Gets blocks[1] */
    ASSERT_EQ(blocks[1], stolen);

    /* Remaining: 2, 3, 4, 5, 6, 7 */
    ASSERT_EQ(6, deque_size(&w1->runq));

    for (int i = 0; i < 10; i++) {
        block_free(blocks[i]);
    }
    free(scheduler->workers);
    scheduler->workers = NULL;
    scheduler->worker_count = 0;
    worker_free(w0);
    worker_free(w1);
    scheduler_free(scheduler);
}

int main(void) {
    printf("Running worker stealing tests...\n");

    printf("\nNull/edge case tests:\n");
    RUN_TEST(test_steal_null_worker);
    RUN_TEST(test_steal_null_scheduler);
    RUN_TEST(test_steal_single_worker);
    RUN_TEST(test_steal_null_victim_in_array);

    printf("\nBasic stealing tests:\n");
    RUN_TEST(test_steal_from_other_worker);
    RUN_TEST(test_steal_all_empty);
    RUN_TEST(test_steal_skips_self);

    printf("\nCounter tests:\n");
    RUN_TEST(test_steal_increments_attempted);

    printf("\nMultiple steal tests:\n");
    RUN_TEST(test_steal_multiple_from_same);
    RUN_TEST(test_steal_from_multiple_victims);

    printf("\nRandom selection tests:\n");
    RUN_TEST(test_steal_random_victim_selection);

    printf("\nLarge scale tests:\n");
    RUN_TEST(test_steal_large_scale);

    printf("\nInterleaved tests:\n");
    RUN_TEST(test_steal_interleaved_with_push);
    RUN_TEST(test_steal_vs_pop_sequential);

    return TEST_RESULT();
}
