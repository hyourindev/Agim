/*
 * Agim - Scheduler Run Queue Tests
 *
 * P1.1.3.4: Tests for run queue operations.
 * - Enqueue/dequeue operations
 * - Queue empty checks
 * - FIFO ordering
 * - Queue under various block states
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "vm/bytecode.h"

/* Helper: Create minimal bytecode that just halts */
static Bytecode *create_minimal_bytecode(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_NIL, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Helper: Create bytecode that loops N times then halts */
static Bytecode *create_loop_bytecode(int iterations) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Push counter */
    chunk_add_constant(chunk, value_int(iterations));
    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(0));

    /* counter = iterations */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* loop: if counter <= 0, jump to end */
    size_t loop_start = chunk->code_size;

    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_LE, 2);

    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);

    /* Pop the condition result (false means we continue) */
    chunk_write_opcode(chunk, OP_POP, 2);

    /* counter = counter - 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    /* jump back to loop */
    chunk_write_opcode(chunk, OP_LOOP, 4);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, offset & 0xFF, 4);

    /* end: halt */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);
    chunk_write_opcode(chunk, OP_HALT, 5);

    return code;
}

/*
 * Test: Queue starts empty
 */
void test_runqueue_initially_empty(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: Queue not empty after spawn
 */
void test_runqueue_not_empty_after_spawn(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    ASSERT(scheduler_queue_empty(sched));

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");
    ASSERT(pid != PID_INVALID);

    ASSERT(!scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: Dequeue returns spawned block
 */
void test_runqueue_dequeue_returns_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_dequeue(sched);
    ASSERT(block != NULL);
    ASSERT_EQ(pid, block->pid);

    scheduler_free(sched);
}

/*
 * Test: Dequeue from empty queue returns NULL
 */
void test_runqueue_dequeue_empty_returns_null(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Block *block = scheduler_dequeue(sched);
    ASSERT(block == NULL);

    scheduler_free(sched);
}

/*
 * Test: FIFO ordering - first spawned is first dequeued
 */
void test_runqueue_fifo_ordering(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn multiple blocks */
    Bytecode *code1 = create_minimal_bytecode();
    Bytecode *code2 = create_minimal_bytecode();
    Bytecode *code3 = create_minimal_bytecode();

    Pid pid1 = scheduler_spawn(sched, code1, "first");
    Pid pid2 = scheduler_spawn(sched, code2, "second");
    Pid pid3 = scheduler_spawn(sched, code3, "third");

    /* Dequeue should return in FIFO order */
    Block *block1 = scheduler_dequeue(sched);
    Block *block2 = scheduler_dequeue(sched);
    Block *block3 = scheduler_dequeue(sched);

    ASSERT(block1 != NULL);
    ASSERT(block2 != NULL);
    ASSERT(block3 != NULL);

    ASSERT_EQ(pid1, block1->pid);
    ASSERT_EQ(pid2, block2->pid);
    ASSERT_EQ(pid3, block3->pid);

    /* Queue should now be empty */
    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: Enqueue adds block back to queue
 */
void test_runqueue_enqueue(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");
    ASSERT(pid != PID_INVALID);

    /* Dequeue the block */
    Block *block = scheduler_dequeue(sched);
    ASSERT(block != NULL);
    ASSERT(scheduler_queue_empty(sched));

    /* Enqueue it back */
    scheduler_enqueue(sched, block);
    ASSERT(!scheduler_queue_empty(sched));

    /* Dequeue again */
    Block *block2 = scheduler_dequeue(sched);
    ASSERT(block2 == block);

    scheduler_free(sched);
}

/*
 * Test: Multiple enqueue/dequeue cycles
 */
void test_runqueue_multiple_cycles(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");
    ASSERT(pid != PID_INVALID);

    for (int i = 0; i < 10; i++) {
        Block *block = scheduler_dequeue(sched);
        ASSERT(block != NULL);
        ASSERT_EQ(pid, block->pid);
        ASSERT(scheduler_queue_empty(sched));

        scheduler_enqueue(sched, block);
        ASSERT(!scheduler_queue_empty(sched));
    }

    scheduler_free(sched);
}

/*
 * Test: Queue maintains multiple blocks
 */
void test_runqueue_multiple_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    const int num_blocks = 10;
    Pid pids[10];

    for (int i = 0; i < num_blocks; i++) {
        Bytecode *code = create_minimal_bytecode();
        pids[i] = scheduler_spawn(sched, code, "block");
        ASSERT(pids[i] != PID_INVALID);
    }

    /* Queue should have all blocks */
    for (int i = 0; i < num_blocks; i++) {
        Block *block = scheduler_dequeue(sched);
        ASSERT(block != NULL);
        ASSERT_EQ(pids[i], block->pid);
    }

    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: Queue empty check with NULL scheduler
 */
void test_runqueue_empty_null_scheduler(void) {
    ASSERT(scheduler_queue_empty(NULL));
}

/*
 * Test: Dequeue with NULL scheduler returns NULL
 */
void test_runqueue_dequeue_null_scheduler(void) {
    Block *block = scheduler_dequeue(NULL);
    ASSERT(block == NULL);
}

/*
 * Test: Enqueue with NULL scheduler does nothing
 */
void test_runqueue_enqueue_null_scheduler(void) {
    /* This should not crash */
    scheduler_enqueue(NULL, NULL);
    /* No assertion needed - just verify no crash */
    ASSERT(1);  /* Placeholder to count as a test */
}

/*
 * Test: Enqueue with NULL block does nothing
 */
void test_runqueue_enqueue_null_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* This should not crash */
    scheduler_enqueue(sched, NULL);
    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: scheduler_step dequeues and processes block
 */
void test_runqueue_step_processes_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");
    ASSERT(pid != PID_INVALID);

    ASSERT(!scheduler_queue_empty(sched));

    /* Step processes the block */
    bool had_work = scheduler_step(sched);
    ASSERT(had_work);

    /* Block completed (halted), queue should be empty */
    ASSERT(scheduler_queue_empty(sched));

    /* Block should be dead */
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
}

/*
 * Test: scheduler_step returns false for empty queue
 */
void test_runqueue_step_empty_queue(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    bool had_work = scheduler_step(sched);
    ASSERT(!had_work);

    scheduler_free(sched);
}

/*
 * Test: Block yielding re-enqueues to queue
 */
void test_runqueue_yield_reenqueues(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 10;  /* Very low to force yields */

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Create a block that will need multiple steps to complete */
    Bytecode *code = create_loop_bytecode(100);
    Pid pid = scheduler_spawn(sched, code, "looper");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* First step - block should yield and be re-enqueued */
    bool had_work = scheduler_step(sched);
    ASSERT(had_work);

    /* If block yielded, it should still be runnable and in queue */
    if (block_state(block) == BLOCK_RUNNABLE) {
        ASSERT(!scheduler_queue_empty(sched));
    }

    /* Run to completion */
    scheduler_run(sched);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
}

/*
 * Test: Queue handles interleaved spawn and dequeue
 */
void test_runqueue_interleaved_operations(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn first block */
    Bytecode *code1 = create_minimal_bytecode();
    Pid pid1 = scheduler_spawn(sched, code1, "block1");

    /* Dequeue it */
    Block *b1 = scheduler_dequeue(sched);
    ASSERT(b1 != NULL);
    ASSERT_EQ(pid1, b1->pid);

    /* Spawn second block while first is dequeued */
    Bytecode *code2 = create_minimal_bytecode();
    Pid pid2 = scheduler_spawn(sched, code2, "block2");

    /* Re-enqueue first */
    scheduler_enqueue(sched, b1);

    /* Dequeue should return second (was added while first was out) */
    Block *b2 = scheduler_dequeue(sched);
    ASSERT(b2 != NULL);
    ASSERT_EQ(pid2, b2->pid);

    /* Dequeue should return first */
    Block *b1_again = scheduler_dequeue(sched);
    ASSERT(b1_again == b1);

    scheduler_free(sched);
}

/*
 * Test: Queue count matches expected
 */
void test_runqueue_count(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Initially empty */
    ASSERT(scheduler_queue_empty(sched));

    /* Spawn blocks */
    for (int i = 0; i < 5; i++) {
        Bytecode *code = create_minimal_bytecode();
        scheduler_spawn(sched, code, "block");
    }

    /* Verify not empty */
    ASSERT(!scheduler_queue_empty(sched));

    /* Dequeue all */
    for (int i = 0; i < 5; i++) {
        Block *block = scheduler_dequeue(sched);
        ASSERT(block != NULL);
    }

    /* Now empty */
    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: scheduler_run processes all blocks in queue
 */
void test_runqueue_run_processes_all(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn multiple blocks */
    Pid pids[5];
    for (int i = 0; i < 5; i++) {
        Bytecode *code = create_minimal_bytecode();
        pids[i] = scheduler_spawn(sched, code, "block");
        ASSERT(pids[i] != PID_INVALID);
    }

    ASSERT(!scheduler_queue_empty(sched));

    /* Run all */
    scheduler_run(sched);

    /* Queue should be empty (all completed) */
    ASSERT(scheduler_queue_empty(sched));

    /* All blocks should be dead */
    for (int i = 0; i < 5; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        ASSERT(block != NULL);
        ASSERT_EQ(BLOCK_DEAD, block_state(block));
    }

    scheduler_free(sched);
}

/*
 * Test: Queue handles blocks that complete vs yield
 */
void test_runqueue_mixed_completion_yield(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 50;  /* Allow some work per step */

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Spawn a quick block (completes in one step) */
    Bytecode *quick = create_minimal_bytecode();
    Pid quick_pid = scheduler_spawn(sched, quick, "quick");

    /* Spawn a slow block (needs multiple steps) */
    Bytecode *slow = create_loop_bytecode(1000);
    Pid slow_pid = scheduler_spawn(sched, slow, "slow");

    /* Run to completion */
    scheduler_run(sched);

    /* Both should be dead */
    Block *quick_block = scheduler_get_block(sched, quick_pid);
    Block *slow_block = scheduler_get_block(sched, slow_pid);

    ASSERT(quick_block != NULL);
    ASSERT(slow_block != NULL);
    ASSERT_EQ(BLOCK_DEAD, block_state(quick_block));
    ASSERT_EQ(BLOCK_DEAD, block_state(slow_block));

    /* Queue should be empty */
    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: Context switches are counted
 */
void test_runqueue_context_switches_counted(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 10;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Spawn a block that needs multiple steps */
    Bytecode *code = create_loop_bytecode(100);
    scheduler_spawn(sched, code, "looper");

    SchedulerStats stats_before = scheduler_stats(sched);
    ASSERT_EQ(0, stats_before.context_switches);

    /* Run to completion */
    scheduler_run(sched);

    SchedulerStats stats_after = scheduler_stats(sched);
    ASSERT(stats_after.context_switches > 0);

    scheduler_free(sched);
}

/*
 * Test: Reductions are accumulated
 */
void test_runqueue_reductions_accumulated(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_loop_bytecode(50);
    scheduler_spawn(sched, code, "looper");

    SchedulerStats stats_before = scheduler_stats(sched);
    ASSERT_EQ(0, stats_before.total_reductions);

    scheduler_run(sched);

    SchedulerStats stats_after = scheduler_stats(sched);
    ASSERT(stats_after.total_reductions > 0);

    scheduler_free(sched);
}

int main(void) {
    printf("Running scheduler run queue tests...\n");

    printf("\nBasic queue operations:\n");
    RUN_TEST(test_runqueue_initially_empty);
    RUN_TEST(test_runqueue_not_empty_after_spawn);
    RUN_TEST(test_runqueue_dequeue_returns_block);
    RUN_TEST(test_runqueue_dequeue_empty_returns_null);

    printf("\nFIFO ordering:\n");
    RUN_TEST(test_runqueue_fifo_ordering);

    printf("\nEnqueue operations:\n");
    RUN_TEST(test_runqueue_enqueue);
    RUN_TEST(test_runqueue_multiple_cycles);
    RUN_TEST(test_runqueue_multiple_blocks);

    printf("\nNull safety:\n");
    RUN_TEST(test_runqueue_empty_null_scheduler);
    RUN_TEST(test_runqueue_dequeue_null_scheduler);
    RUN_TEST(test_runqueue_enqueue_null_scheduler);
    RUN_TEST(test_runqueue_enqueue_null_block);

    printf("\nStep operations:\n");
    RUN_TEST(test_runqueue_step_processes_block);
    RUN_TEST(test_runqueue_step_empty_queue);

    printf("\nYield and re-enqueue:\n");
    RUN_TEST(test_runqueue_yield_reenqueues);
    RUN_TEST(test_runqueue_interleaved_operations);

    printf("\nRun queue counting:\n");
    RUN_TEST(test_runqueue_count);

    printf("\nRun operations:\n");
    RUN_TEST(test_runqueue_run_processes_all);
    RUN_TEST(test_runqueue_mixed_completion_yield);

    printf("\nStatistics:\n");
    RUN_TEST(test_runqueue_context_switches_counted);
    RUN_TEST(test_runqueue_reductions_accumulated);

    return TEST_RESULT();
}
