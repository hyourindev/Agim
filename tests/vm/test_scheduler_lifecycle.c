/*
 * Agim - Scheduler Lifecycle Tests
 *
 * Comprehensive tests for scheduler lifecycle:
 * - scheduler_new with config
 * - scheduler_new default config
 * - scheduler_free cleans up
 * - scheduler_free with active blocks
 * - scheduler_free with workers
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"

/* ============================================================================
 * scheduler_config_default Tests
 * ============================================================================ */

void test_config_default_values(void) {
    SchedulerConfig config = scheduler_config_default();

    ASSERT_EQ(10000, config.max_blocks);
    ASSERT_EQ(10000, config.default_reductions);
    ASSERT_EQ(0, config.num_workers); /* Single-threaded by default */
    ASSERT(config.enable_stealing);
}

void test_config_can_be_customized(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 500;
    config.default_reductions = 5000;
    config.num_workers = 4;
    config.enable_stealing = false;

    ASSERT_EQ(500, config.max_blocks);
    ASSERT_EQ(5000, config.default_reductions);
    ASSERT_EQ(4, config.num_workers);
    ASSERT(!config.enable_stealing);
}

/* ============================================================================
 * scheduler_new with config Tests
 * ============================================================================ */

void test_scheduler_new_with_config(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 100;
    config.default_reductions = 500;

    Scheduler *sched = scheduler_new(&config);

    ASSERT(sched != NULL);
    ASSERT_EQ(100, sched->config.max_blocks);
    ASSERT_EQ(500, sched->config.default_reductions);

    scheduler_free(sched);
}

void test_scheduler_new_with_workers(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 2;

    Scheduler *sched = scheduler_new(&config);

    ASSERT(sched != NULL);
    ASSERT_EQ(2, sched->worker_count);
    ASSERT(sched->workers != NULL);
    ASSERT(scheduler_is_multithreaded(sched));

    scheduler_free(sched);
}

void test_scheduler_new_single_threaded(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 0;

    Scheduler *sched = scheduler_new(&config);

    ASSERT(sched != NULL);
    ASSERT_EQ(0, sched->worker_count);
    ASSERT(sched->workers == NULL);
    ASSERT(!scheduler_is_multithreaded(sched));

    scheduler_free(sched);
}

void test_scheduler_new_initializes_registry(void) {
    SchedulerConfig config = scheduler_config_default();
    Scheduler *sched = scheduler_new(&config);

    ASSERT(sched != NULL);
    /* Registry should be initialized with 0 blocks */
    ASSERT_EQ(0, scheduler_block_count(sched));

    scheduler_free(sched);
}

void test_scheduler_new_initializes_run_queue(void) {
    SchedulerConfig config = scheduler_config_default();
    Scheduler *sched = scheduler_new(&config);

    ASSERT(sched != NULL);
    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

void test_scheduler_new_initializes_counters(void) {
    SchedulerConfig config = scheduler_config_default();
    Scheduler *sched = scheduler_new(&config);

    ASSERT(sched != NULL);

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(0, stats.blocks_total);
    ASSERT_EQ(0, stats.blocks_alive);
    ASSERT_EQ(0, stats.context_switches);
    ASSERT_EQ(0, stats.total_reductions);

    scheduler_free(sched);
}

void test_scheduler_new_next_pid_starts_at_1(void) {
    Scheduler *sched = scheduler_new(NULL);

    ASSERT(sched != NULL);
    /* First spawn should get PID 1 */
    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");
    ASSERT_EQ(1, pid);

    scheduler_free(sched);
    bytecode_free(code);
}

/* ============================================================================
 * scheduler_new default config Tests
 * ============================================================================ */

void test_scheduler_new_null_config(void) {
    Scheduler *sched = scheduler_new(NULL);

    ASSERT(sched != NULL);
    /* Should use default config */
    ASSERT_EQ(10000, sched->config.max_blocks);
    ASSERT_EQ(10000, sched->config.default_reductions);
    ASSERT_EQ(0, sched->config.num_workers);

    scheduler_free(sched);
}

void test_scheduler_new_default_is_single_threaded(void) {
    Scheduler *sched = scheduler_new(NULL);

    ASSERT(sched != NULL);
    ASSERT(!scheduler_is_multithreaded(sched));
    ASSERT_EQ(0, scheduler_worker_count(sched));

    scheduler_free(sched);
}

/* ============================================================================
 * scheduler_free cleans up Tests
 * ============================================================================ */

void test_scheduler_free_null_safe(void) {
    /* Should not crash */
    scheduler_free(NULL);
}

void test_scheduler_free_empty_scheduler(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Free empty scheduler - should not crash */
    scheduler_free(sched);
}

void test_scheduler_free_cleans_run_queue(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Spawn some blocks */
    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);

    scheduler_spawn(sched, code, "block1");
    scheduler_spawn(sched, code, "block2");

    ASSERT(!scheduler_queue_empty(sched));

    /* Free should clean up */
    scheduler_free(sched);
    bytecode_free(code);
    /* No crash means success */
}

/* ============================================================================
 * scheduler_free with active blocks Tests
 * ============================================================================ */

void test_scheduler_free_with_runnable_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);

    /* Spawn blocks but don't run them */
    scheduler_spawn(sched, code, "block1");
    scheduler_spawn(sched, code, "block2");
    scheduler_spawn(sched, code, "block3");

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(3, stats.blocks_runnable);

    /* Free with active blocks */
    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_free_with_dead_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);

    scheduler_spawn(sched, code, "block1");
    scheduler_spawn(sched, code, "block2");

    /* Run to completion */
    scheduler_run(sched);

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(2, stats.blocks_dead);

    /* Free with dead blocks */
    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_free_with_waiting_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create a block that waits for a message */
    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_RECEIVE, 1);
    chunk_write_opcode(code->main, OP_HALT, 1);

    scheduler_spawn_ex(sched, code, "waiter", CAP_RECEIVE, NULL);

    /* Step to put block in waiting state */
    scheduler_step(sched);

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(1, stats.blocks_waiting);

    /* Free with waiting block */
    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_free_mixed_block_states(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create various block states */
    Bytecode *halt_code = bytecode_new();
    chunk_write_opcode(halt_code->main, OP_HALT, 1);

    Bytecode *recv_code = bytecode_new();
    chunk_write_opcode(recv_code->main, OP_RECEIVE, 1);
    chunk_write_opcode(recv_code->main, OP_HALT, 1);

    /* Dead block */
    scheduler_spawn(sched, halt_code, "dead");
    scheduler_step(sched);

    /* Waiting block */
    scheduler_spawn_ex(sched, recv_code, "waiting", CAP_RECEIVE, NULL);
    scheduler_step(sched);

    /* Runnable block */
    scheduler_spawn(sched, halt_code, "runnable");

    /* Free with mixed states */
    scheduler_free(sched);
    bytecode_free(halt_code);
    bytecode_free(recv_code);
}

/* ============================================================================
 * scheduler_free with workers Tests
 * ============================================================================ */

void test_scheduler_free_stops_workers(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 2;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);
    ASSERT_EQ(2, sched->worker_count);

    /* Free should stop and clean up workers */
    scheduler_free(sched);
    /* No crash means success */
}

void test_scheduler_free_workers_with_blocks(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 2;

    Scheduler *sched = scheduler_new(&config);

    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);

    /* Spawn blocks */
    scheduler_spawn(sched, code, "block1");
    scheduler_spawn(sched, code, "block2");

    /* Free with workers and blocks */
    scheduler_free(sched);
    bytecode_free(code);
}

/* ============================================================================
 * Scheduler State Tests
 * ============================================================================ */

void test_scheduler_not_running_initially(void) {
    Scheduler *sched = scheduler_new(NULL);

    ASSERT(!sched->running);

    scheduler_free(sched);
}

void test_scheduler_current_null_initially(void) {
    Scheduler *sched = scheduler_new(NULL);

    ASSERT(scheduler_current(sched) == NULL);

    scheduler_free(sched);
}

void test_scheduler_primitives_null_initially(void) {
    Scheduler *sched = scheduler_new(NULL);

    ASSERT(scheduler_get_primitives(sched) == NULL);

    scheduler_free(sched);
}

void test_scheduler_tracer_null_initially(void) {
    Scheduler *sched = scheduler_new(NULL);

    ASSERT(scheduler_get_tracer(sched) == NULL);

    scheduler_free(sched);
}

/* ============================================================================
 * Multiple Scheduler Instances Tests
 * ============================================================================ */

void test_multiple_schedulers_independent(void) {
    Scheduler *sched1 = scheduler_new(NULL);
    Scheduler *sched2 = scheduler_new(NULL);

    ASSERT(sched1 != sched2);

    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);

    /* Spawn in first scheduler */
    Pid pid1 = scheduler_spawn(sched1, code, "block1");

    /* Spawn in second scheduler */
    Pid pid2 = scheduler_spawn(sched2, code, "block2");

    /* Both should work independently */
    ASSERT(pid1 != PID_INVALID);
    ASSERT(pid2 != PID_INVALID);

    /* Block from sched1 should not be found in sched2 */
    ASSERT(scheduler_get_block(sched1, pid1) != NULL);
    ASSERT(scheduler_get_block(sched2, pid2) != NULL);

    scheduler_free(sched1);
    scheduler_free(sched2);
    bytecode_free(code);
}

void test_scheduler_free_order_independent(void) {
    Scheduler *sched1 = scheduler_new(NULL);
    Scheduler *sched2 = scheduler_new(NULL);
    Scheduler *sched3 = scheduler_new(NULL);

    /* Free in different order than creation */
    scheduler_free(sched2);
    scheduler_free(sched1);
    scheduler_free(sched3);
    /* No crash means success */
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

void test_scheduler_very_small_max_blocks(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 1;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);

    /* First spawn should succeed */
    Pid pid1 = scheduler_spawn(sched, code, "block1");
    ASSERT(pid1 != PID_INVALID);

    /* Second spawn should fail (max reached) */
    Pid pid2 = scheduler_spawn(sched, code, "block2");
    ASSERT(pid2 == PID_INVALID);

    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_very_small_reductions(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 1;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Create a loop that needs multiple steps */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Push counter - large enough to force multiple preemptions */
    chunk_add_constant(chunk, value_int(100));
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* loop: decrement and check */
    chunk_add_constant(chunk, value_int(1));
    size_t loop_start = chunk->code_size;

    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 1, 2);
    chunk_write_opcode(chunk, OP_SUB, 2);
    chunk_write_opcode(chunk, OP_DUP, 2);

    chunk_add_constant(chunk, value_int(0));
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 2, 3);
    chunk_write_opcode(chunk, OP_GT, 3);

    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 3);
    chunk_write_opcode(chunk, OP_POP, 3);

    chunk_write_opcode(chunk, OP_LOOP, 4);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, offset & 0xFF, 4);

    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_HALT, 5);

    BlockLimits limits = block_limits_default();
    limits.max_reductions = 5; /* Very low but not 1 */

    scheduler_spawn_ex(sched, code, "looper", CAP_ALL, &limits);

    /* Run - should complete eventually despite frequent preemption */
    scheduler_run(sched);

    SchedulerStats stats = scheduler_stats(sched);
    /* Block should have completed */
    ASSERT_EQ(1, stats.blocks_dead);
    /* With such a low reduction limit and 100 iterations, should have been preempted */
    ASSERT(stats.context_switches >= 1);

    scheduler_free(sched);
    bytecode_free(code);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    /* scheduler_config_default */
    RUN_TEST(test_config_default_values);
    RUN_TEST(test_config_can_be_customized);

    /* scheduler_new with config */
    RUN_TEST(test_scheduler_new_with_config);
    RUN_TEST(test_scheduler_new_with_workers);
    RUN_TEST(test_scheduler_new_single_threaded);
    RUN_TEST(test_scheduler_new_initializes_registry);
    RUN_TEST(test_scheduler_new_initializes_run_queue);
    RUN_TEST(test_scheduler_new_initializes_counters);
    RUN_TEST(test_scheduler_new_next_pid_starts_at_1);

    /* scheduler_new default config */
    RUN_TEST(test_scheduler_new_null_config);
    RUN_TEST(test_scheduler_new_default_is_single_threaded);

    /* scheduler_free cleans up */
    RUN_TEST(test_scheduler_free_null_safe);
    RUN_TEST(test_scheduler_free_empty_scheduler);
    RUN_TEST(test_scheduler_free_cleans_run_queue);

    /* scheduler_free with active blocks */
    RUN_TEST(test_scheduler_free_with_runnable_blocks);
    RUN_TEST(test_scheduler_free_with_dead_blocks);
    RUN_TEST(test_scheduler_free_with_waiting_blocks);
    RUN_TEST(test_scheduler_free_mixed_block_states);

    /* scheduler_free with workers */
    RUN_TEST(test_scheduler_free_stops_workers);
    RUN_TEST(test_scheduler_free_workers_with_blocks);

    /* Scheduler state */
    RUN_TEST(test_scheduler_not_running_initially);
    RUN_TEST(test_scheduler_current_null_initially);
    RUN_TEST(test_scheduler_primitives_null_initially);
    RUN_TEST(test_scheduler_tracer_null_initially);

    /* Multiple instances */
    RUN_TEST(test_multiple_schedulers_independent);
    RUN_TEST(test_scheduler_free_order_independent);

    /* Edge cases */
    RUN_TEST(test_scheduler_very_small_max_blocks);
    RUN_TEST(test_scheduler_very_small_reductions);

    return TEST_RESULT();
}
