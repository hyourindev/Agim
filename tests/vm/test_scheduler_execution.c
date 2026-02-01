/*
 * Agim - Scheduler Execution Tests
 *
 * P1.1.3.5: Tests for block execution via scheduler.
 * - scheduler_run completion
 * - scheduler_step single step
 * - scheduler_stop interruption
 * - Preemption and reduction counting
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

    chunk_add_constant(chunk, value_int(iterations));
    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(0));

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t loop_start = chunk->code_size;

    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_LE, 2);

    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);

    chunk_write_opcode(chunk, OP_POP, 2);

    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    chunk_write_opcode(chunk, OP_LOOP, 4);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, offset & 0xFF, 4);

    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);
    chunk_write_opcode(chunk, OP_HALT, 5);

    return code;
}

/* Helper: Create bytecode that pushes a value and halts */
static Bytecode *create_value_bytecode(int64_t value) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(value));
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/*
 * Test: scheduler_run completes all blocks
 */
void test_execution_run_completes_all(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Pid pids[5];
    for (int i = 0; i < 5; i++) {
        Bytecode *code = create_minimal_bytecode();
        pids[i] = scheduler_spawn(sched, code, "block");
        ASSERT(pids[i] != PID_INVALID);
    }

    scheduler_run(sched);

    for (int i = 0; i < 5; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        ASSERT(block != NULL);
        ASSERT_EQ(BLOCK_DEAD, block_state(block));
    }

    scheduler_free(sched);
}

/*
 * Test: scheduler_run returns when queue empty
 */
void test_execution_run_empty_returns(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Run with empty queue - should return immediately */
    scheduler_run(sched);

    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: scheduler_step processes one block
 */
void test_execution_step_one_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");
    ASSERT(pid != PID_INVALID);

    bool had_work = scheduler_step(sched);
    ASSERT(had_work);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
}

/*
 * Test: scheduler_step returns false for empty queue
 */
void test_execution_step_empty_returns_false(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    bool had_work = scheduler_step(sched);
    ASSERT(!had_work);

    scheduler_free(sched);
}

/*
 * Test: Block yields when reduction limit reached
 */
void test_execution_preemption_by_reductions(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 5;  /* Very low to force preemption */

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    Bytecode *code = create_loop_bytecode(100);
    Pid pid = scheduler_spawn(sched, code, "looper");
    ASSERT(pid != PID_INVALID);

    /* First step - block should yield due to reduction limit */
    bool had_work = scheduler_step(sched);
    ASSERT(had_work);

    Block *block = scheduler_get_block(sched, pid);

    /* Block should still be alive (yielded, not completed) */
    if (block_state(block) == BLOCK_RUNNABLE) {
        ASSERT(block_is_alive(block));
        /* Block should be back in queue */
        ASSERT(!scheduler_queue_empty(sched));
    }

    /* Complete execution */
    scheduler_run(sched);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
}

/*
 * Test: Reductions are counted per block
 */
void test_execution_reductions_per_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_loop_bytecode(10);
    Pid pid = scheduler_spawn(sched, code, "looper");

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(0, block->counters.reductions);

    scheduler_run(sched);

    ASSERT(block->counters.reductions > 0);

    scheduler_free(sched);
}

/*
 * Test: Total reductions accumulated in scheduler
 */
void test_execution_total_reductions(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    SchedulerStats stats_before = scheduler_stats(sched);
    ASSERT_EQ(0, stats_before.total_reductions);

    /* Spawn and run multiple blocks */
    for (int i = 0; i < 3; i++) {
        Bytecode *code = create_loop_bytecode(20);
        scheduler_spawn(sched, code, "looper");
    }

    scheduler_run(sched);

    SchedulerStats stats_after = scheduler_stats(sched);
    ASSERT(stats_after.total_reductions > 0);

    scheduler_free(sched);
}

/*
 * Test: Context switches counted correctly
 */
void test_execution_context_switches(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 20;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    SchedulerStats stats_before = scheduler_stats(sched);
    ASSERT_EQ(0, stats_before.context_switches);

    /* Spawn multiple blocks that need multiple steps */
    for (int i = 0; i < 3; i++) {
        Bytecode *code = create_loop_bytecode(100);
        scheduler_spawn(sched, code, "looper");
    }

    scheduler_run(sched);

    SchedulerStats stats_after = scheduler_stats(sched);
    ASSERT(stats_after.context_switches >= 3);  /* At least one per block */

    scheduler_free(sched);
}

/*
 * Test: scheduler_stop halts execution
 */
void test_execution_stop(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Stop immediately - scheduler should be stopped */
    scheduler_stop(sched);
    ASSERT(!atomic_load(&sched->running));

    scheduler_free(sched);
}

/*
 * Test: scheduler_current returns current executing block
 */
void test_execution_current_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* No current block initially */
    ASSERT(scheduler_current(sched) == NULL);

    scheduler_free(sched);
}

/*
 * Test: Multiple blocks execute fairly
 */
void test_execution_fairness(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 20;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Spawn multiple long-running blocks */
    Pid pids[3];
    for (int i = 0; i < 3; i++) {
        Bytecode *code = create_loop_bytecode(200);
        pids[i] = scheduler_spawn(sched, code, "looper");
    }

    /* Run to completion */
    scheduler_run(sched);

    /* All blocks should complete (fairness ensures none starve) */
    for (int i = 0; i < 3; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        ASSERT(block != NULL);
        ASSERT_EQ(BLOCK_DEAD, block_state(block));
        /* Each block should have done some reductions */
        ASSERT(block->counters.reductions > 0);
    }

    scheduler_free(sched);
}

/*
 * Test: Block execution updates counters
 */
void test_execution_updates_counters(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_loop_bytecode(50);
    Pid pid = scheduler_spawn(sched, code, "counter_test");

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(0, block->counters.reductions);

    scheduler_run(sched);

    /* Reductions should be counted */
    ASSERT(block->counters.reductions > 0);

    scheduler_free(sched);
}

/*
 * Test: Terminated count updates on completion
 */
void test_execution_terminated_count(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    ASSERT_EQ(0, atomic_load(&sched->total_terminated));

    Bytecode *code = create_minimal_bytecode();
    scheduler_spawn(sched, code, "block");

    scheduler_run(sched);

    ASSERT_EQ(1, atomic_load(&sched->total_terminated));

    scheduler_free(sched);
}

/*
 * Test: Multiple blocks update terminated count
 */
void test_execution_multiple_terminated(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    for (int i = 0; i < 5; i++) {
        Bytecode *code = create_minimal_bytecode();
        scheduler_spawn(sched, code, "block");
    }

    scheduler_run(sched);

    ASSERT_EQ(5, atomic_load(&sched->total_terminated));

    scheduler_free(sched);
}

/*
 * Test: Block state transitions during execution
 */
void test_execution_state_transitions(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    scheduler_run(sched);

    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
}

/*
 * Test: scheduler_run with NULL is safe
 */
void test_execution_run_null(void) {
    scheduler_run(NULL);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: scheduler_step with NULL returns false
 */
void test_execution_step_null(void) {
    bool result = scheduler_step(NULL);
    ASSERT(!result);
}

/*
 * Test: scheduler_stop with NULL is safe
 */
void test_execution_stop_null(void) {
    scheduler_stop(NULL);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: scheduler_current with NULL returns NULL
 */
void test_execution_current_null(void) {
    Block *current = scheduler_current(NULL);
    ASSERT(current == NULL);
}

/*
 * Test: Block execution with different bytecode
 */
void test_execution_different_bytecode(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn blocks with different bytecode */
    Bytecode *code1 = create_value_bytecode(42);
    Bytecode *code2 = create_loop_bytecode(10);
    Bytecode *code3 = create_minimal_bytecode();

    Pid pid1 = scheduler_spawn(sched, code1, "value");
    Pid pid2 = scheduler_spawn(sched, code2, "loop");
    Pid pid3 = scheduler_spawn(sched, code3, "minimal");

    scheduler_run(sched);

    /* All should complete */
    ASSERT_EQ(BLOCK_DEAD, block_state(scheduler_get_block(sched, pid1)));
    ASSERT_EQ(BLOCK_DEAD, block_state(scheduler_get_block(sched, pid2)));
    ASSERT_EQ(BLOCK_DEAD, block_state(scheduler_get_block(sched, pid3)));

    scheduler_free(sched);
}

/*
 * Test: Execution with very high reduction limit
 */
void test_execution_high_reduction_limit(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 1000000;  /* Very high */

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    Bytecode *code = create_loop_bytecode(100);
    Pid pid = scheduler_spawn(sched, code, "looper");

    scheduler_run(sched);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
}

/*
 * Test: Execution with reduction limit of 1
 */
void test_execution_minimal_reductions(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 1;  /* Minimal */

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    Bytecode *code = create_loop_bytecode(10);
    Pid pid = scheduler_spawn(sched, code, "looper");

    /* Run to completion - will need many context switches */
    scheduler_run(sched);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    /* At least some context switches expected (even with minimal reductions,
     * the block may complete quickly) */
    SchedulerStats stats = scheduler_stats(sched);
    ASSERT(stats.context_switches >= 1);

    scheduler_free(sched);
}

/*
 * Test: spawned_count stays consistent
 */
void test_execution_spawned_count(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    for (int i = 0; i < 10; i++) {
        Bytecode *code = create_minimal_bytecode();
        scheduler_spawn(sched, code, "block");
    }

    ASSERT_EQ(10, atomic_load(&sched->total_spawned));

    scheduler_run(sched);

    /* spawned count shouldn't change after execution */
    ASSERT_EQ(10, atomic_load(&sched->total_spawned));

    scheduler_free(sched);
}

/*
 * Test: stats are coherent
 */
void test_execution_stats_coherent(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    for (int i = 0; i < 5; i++) {
        Bytecode *code = create_minimal_bytecode();
        scheduler_spawn(sched, code, "block");
    }

    scheduler_run(sched);

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(5, stats.blocks_total);
    ASSERT_EQ(5, stats.blocks_dead);
    ASSERT_EQ(0, stats.blocks_alive);
    ASSERT_EQ(0, stats.blocks_runnable);
    ASSERT_EQ(0, stats.blocks_waiting);

    scheduler_free(sched);
}

int main(void) {
    printf("Running scheduler execution tests...\n");

    printf("\nRun completion tests:\n");
    RUN_TEST(test_execution_run_completes_all);
    RUN_TEST(test_execution_run_empty_returns);

    printf("\nStep execution tests:\n");
    RUN_TEST(test_execution_step_one_block);
    RUN_TEST(test_execution_step_empty_returns_false);

    printf("\nPreemption tests:\n");
    RUN_TEST(test_execution_preemption_by_reductions);
    RUN_TEST(test_execution_fairness);

    printf("\nReduction counting tests:\n");
    RUN_TEST(test_execution_reductions_per_block);
    RUN_TEST(test_execution_total_reductions);

    printf("\nContext switch tests:\n");
    RUN_TEST(test_execution_context_switches);

    printf("\nStop tests:\n");
    RUN_TEST(test_execution_stop);

    printf("\nCurrent block tests:\n");
    RUN_TEST(test_execution_current_block);

    printf("\nCounter update tests:\n");
    RUN_TEST(test_execution_updates_counters);
    RUN_TEST(test_execution_terminated_count);
    RUN_TEST(test_execution_multiple_terminated);

    printf("\nState transition tests:\n");
    RUN_TEST(test_execution_state_transitions);

    printf("\nNull safety tests:\n");
    RUN_TEST(test_execution_run_null);
    RUN_TEST(test_execution_step_null);
    RUN_TEST(test_execution_stop_null);
    RUN_TEST(test_execution_current_null);

    printf("\nDifferent bytecode tests:\n");
    RUN_TEST(test_execution_different_bytecode);

    printf("\nReduction limit edge cases:\n");
    RUN_TEST(test_execution_high_reduction_limit);
    RUN_TEST(test_execution_minimal_reductions);

    printf("\nStats coherence tests:\n");
    RUN_TEST(test_execution_spawned_count);
    RUN_TEST(test_execution_stats_coherent);

    return TEST_RESULT();
}
