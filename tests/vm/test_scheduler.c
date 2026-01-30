/*
 * Agim - Scheduler Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"

/* Helper: create simple bytecode that pushes a value and halts */
static Bytecode *make_simple_code(int64_t value) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(value));
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Helper: create bytecode that loops N times then halts */
static Bytecode *make_loop_code(int iterations) {
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
    chunk_write_opcode(chunk, OP_POP, 5); /* Pop the condition result */
    chunk_write_opcode(chunk, OP_HALT, 5);

    return code;
}

void test_scheduler_create(void) {
    Scheduler *sched = scheduler_new(NULL);

    ASSERT(sched != NULL);
    ASSERT(!sched->running);
    ASSERT(scheduler_queue_empty(sched));

    scheduler_free(sched);
}

void test_scheduler_spawn(void) {
    Scheduler *sched = scheduler_new(NULL);
    Bytecode *code = make_simple_code(42);

    Pid pid = scheduler_spawn(sched, code, "test_block");

    ASSERT(pid != PID_INVALID);
    ASSERT(!scheduler_queue_empty(sched));

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(pid, block->pid);
    ASSERT_STR_EQ("test_block", block->name);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_spawn_multiple(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code1 = make_simple_code(1);
    Bytecode *code2 = make_simple_code(2);
    Bytecode *code3 = make_simple_code(3);

    Pid pid1 = scheduler_spawn(sched, code1, "block1");
    Pid pid2 = scheduler_spawn(sched, code2, "block2");
    Pid pid3 = scheduler_spawn(sched, code3, "block3");

    ASSERT(pid1 != PID_INVALID);
    ASSERT(pid2 != PID_INVALID);
    ASSERT(pid3 != PID_INVALID);
    ASSERT(pid1 != pid2);
    ASSERT(pid2 != pid3);

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(3, stats.blocks_total);
    ASSERT_EQ(3, stats.blocks_runnable);

    scheduler_free(sched);
    bytecode_free(code1);
    bytecode_free(code2);
    bytecode_free(code3);
}

void test_scheduler_run_single(void) {
    Scheduler *sched = scheduler_new(NULL);
    Bytecode *code = make_simple_code(42);

    Pid pid = scheduler_spawn(sched, code, "single");
    scheduler_run(sched);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(!block_is_alive(block));

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(1, stats.blocks_dead);
    ASSERT_EQ(0, stats.blocks_alive);

    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_run_multiple(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code1 = make_simple_code(1);
    Bytecode *code2 = make_simple_code(2);
    Bytecode *code3 = make_simple_code(3);

    scheduler_spawn(sched, code1, "block1");
    scheduler_spawn(sched, code2, "block2");
    scheduler_spawn(sched, code3, "block3");

    scheduler_run(sched);

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(3, stats.blocks_total);
    ASSERT_EQ(3, stats.blocks_dead);
    ASSERT_EQ(0, stats.blocks_alive);

    scheduler_free(sched);
    bytecode_free(code1);
    bytecode_free(code2);
    bytecode_free(code3);
}

void test_scheduler_preemption(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 100; /* Low limit to force preemption */

    Scheduler *sched = scheduler_new(&config);

    /* Create code that loops many times - will need preemption */
    Bytecode *code1 = make_loop_code(50);
    Bytecode *code2 = make_loop_code(50);

    BlockLimits limits = block_limits_default();
    limits.max_reductions = 20; /* Very low to force multiple yields */

    Pid pid1 = scheduler_spawn_ex(sched, code1, "looper1", CAP_ALL, &limits);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "looper2", CAP_ALL, &limits);
    (void)pid1;
    (void)pid2;

    scheduler_run(sched);

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(2, stats.blocks_dead);
    ASSERT(stats.context_switches > 2); /* Should have switched multiple times */

    scheduler_free(sched);
    bytecode_free(code1);
    bytecode_free(code2);
}

void test_scheduler_kill(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create a loop that won't terminate on its own */
    Bytecode *code = make_loop_code(1000000);
    BlockLimits limits = block_limits_default();
    limits.max_reductions = 100;

    Pid pid = scheduler_spawn_ex(sched, code, "infinite", CAP_ALL, &limits);

    /* Run one step to start it */
    scheduler_step(sched);

    /* Kill it */
    scheduler_kill(sched, pid);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(!block_is_alive(block));

    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_step(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = make_simple_code(42);
    scheduler_spawn(sched, code, "stepper");

    /* Step should return true while there are runnable blocks */
    bool has_work = scheduler_step(sched);

    /* After one step, block should have completed */
    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(1, stats.blocks_dead);

    /* Next step should return false (no more work) */
    has_work = scheduler_step(sched);
    ASSERT(!has_work);

    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_stats(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code1 = make_simple_code(1);
    Bytecode *code2 = make_simple_code(2);

    scheduler_spawn(sched, code1, "block1");
    scheduler_spawn(sched, code2, "block2");

    SchedulerStats before = scheduler_stats(sched);
    ASSERT_EQ(2, before.blocks_total);
    ASSERT_EQ(2, before.blocks_alive);
    ASSERT_EQ(2, before.blocks_runnable);
    ASSERT_EQ(0, before.context_switches);

    scheduler_run(sched);

    SchedulerStats after = scheduler_stats(sched);
    ASSERT_EQ(2, after.blocks_total);
    ASSERT_EQ(0, after.blocks_alive);
    ASSERT_EQ(2, after.blocks_dead);
    ASSERT_EQ(2, after.context_switches);
    ASSERT(after.total_reductions > 0);

    scheduler_free(sched);
    bytecode_free(code1);
    bytecode_free(code2);
}

int main(void) {
    RUN_TEST(test_scheduler_create);
    RUN_TEST(test_scheduler_spawn);
    RUN_TEST(test_scheduler_spawn_multiple);
    RUN_TEST(test_scheduler_run_single);
    RUN_TEST(test_scheduler_run_multiple);
    RUN_TEST(test_scheduler_preemption);
    RUN_TEST(test_scheduler_kill);
    RUN_TEST(test_scheduler_step);
    RUN_TEST(test_scheduler_stats);

    return TEST_RESULT();
}
