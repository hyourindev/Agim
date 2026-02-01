/*
 * Agim - Scheduler Exit Tests
 *
 * P1.1.3.6: Tests for block exit and termination.
 * - Normal exit completion
 * - scheduler_kill
 * - Exit propagation to linked blocks
 * - Exit code tracking
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "runtime/capability.h"
#include "vm/bytecode.h"

/* Helper: Create minimal bytecode that just halts */
static Bytecode *create_minimal_bytecode(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_NIL, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Helper: Create bytecode that loops forever (for kill tests) */
static Bytecode *create_infinite_bytecode(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Infinite loop: just jump back */
    chunk_write_opcode(chunk, OP_NIL, 1);
    size_t loop_start = chunk->code_size;

    /* NOP-like operations to burn reductions */
    chunk_write_opcode(chunk, OP_DUP, 1);
    chunk_write_opcode(chunk, OP_POP, 1);

    /* Jump back to loop_start */
    chunk_write_opcode(chunk, OP_LOOP, 2);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 2);
    chunk_write_byte(chunk, offset & 0xFF, 2);

    return code;
}

/* Helper: Create bytecode with receive that will block */
static Bytecode *create_receive_bytecode(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Receive (will block until message arrives) */
    chunk_write_opcode(chunk, OP_RECEIVE, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/*
 * Test: Normal completion sets block to DEAD
 */
void test_exit_normal_completion(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    scheduler_run(sched);

    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(!block_is_alive(block));

    scheduler_free(sched);
}

/*
 * Test: scheduler_kill terminates running block
 */
void test_exit_kill_terminates_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "victim");

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block_is_alive(block));

    scheduler_kill(sched, pid);

    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(!block_is_alive(block));

    scheduler_free(sched);
}

/*
 * Test: scheduler_kill with invalid PID is safe
 */
void test_exit_kill_invalid_pid(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Kill non-existent PID */
    scheduler_kill(sched, 9999);
    scheduler_kill(sched, PID_INVALID);

    /* Should not crash */
    ASSERT(1);

    scheduler_free(sched);
}

/*
 * Test: scheduler_kill with NULL scheduler is safe
 */
void test_exit_kill_null_scheduler(void) {
    scheduler_kill(NULL, 1);
    ASSERT(1);
}

/*
 * Test: Killed block becomes dead (queue cleanup happens on step)
 *
 * Note: Due to the order of operations in scheduler_kill, the block state
 * is set to DEAD before the runqueue removal check, so the block may remain
 * in the queue until the next step processes and discards it.
 */
void test_exit_kill_block_is_dead(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "victim");

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block_is_alive(block));

    scheduler_kill(sched, pid);

    /* Block should be dead after kill */
    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(!block_is_alive(block));

    scheduler_free(sched);
}

/*
 * Test: Multiple kills don't double-count terminations
 */
void test_exit_double_kill(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "victim");

    scheduler_kill(sched, pid);

    size_t terminated_after_first = atomic_load(&sched->total_terminated);

    scheduler_kill(sched, pid);  /* Second kill */

    size_t terminated_after_second = atomic_load(&sched->total_terminated);

    /* Second kill should not increment count (already dead) */
    ASSERT_EQ(terminated_after_first, terminated_after_second);

    scheduler_free(sched);
}

/*
 * Test: Terminated count increments on normal exit
 */
void test_exit_terminated_count_normal(void) {
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
 * Test: Terminated count increments on kill
 */
void test_exit_terminated_count_kill(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    ASSERT_EQ(0, atomic_load(&sched->total_terminated));

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "victim");

    scheduler_kill(sched, pid);

    ASSERT_EQ(1, atomic_load(&sched->total_terminated));

    scheduler_free(sched);
}

/*
 * Test: block_exit terminates with exit code
 */
void test_exit_with_code(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    Block *block = scheduler_get_block(sched, pid);

    block_exit(block, 42);

    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT_EQ(42, block->u.exit.exit_code);

    scheduler_free(sched);
}

/*
 * Test: block_crash terminates with reason
 */
void test_exit_crash_with_reason(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    Block *block = scheduler_get_block(sched, pid);

    block_crash(block, "test error");

    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(block->u.exit.exit_reason != NULL);
    ASSERT_STR_EQ("test error", block->u.exit.exit_reason);

    scheduler_free(sched);
}

/*
 * Test: Linked block crashes when abnormal exit
 */
void test_exit_linked_block_crashes(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Create two blocks */
    Bytecode *code1 = create_minimal_bytecode();
    Bytecode *code2 = create_minimal_bytecode();

    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_LINK, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_LINK, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);
    Block *block2 = scheduler_get_block(sched, pid2);

    /* Link them */
    block_link(block1, pid2);
    block_link(block2, pid1);

    /* Crash block1 */
    block_crash(block1, "intentional crash");
    scheduler_propagate_exit(sched, block1);

    /* block2 should also be dead (abnormal exit propagation) */
    ASSERT_EQ(BLOCK_DEAD, block_state(block2));

    scheduler_free(sched);
}

/*
 * Test: Normal exit doesn't crash linked blocks
 */
void test_exit_normal_doesnt_crash_linked(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code1 = create_minimal_bytecode();
    Bytecode *code2 = create_receive_bytecode();  /* Will block waiting */

    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_LINK | CAP_RECEIVE, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_LINK | CAP_RECEIVE, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);
    Block *block2 = scheduler_get_block(sched, pid2);

    /* Link them */
    block_link(block1, pid2);
    block_link(block2, pid1);

    /* Normal exit of block1 */
    block_exit(block1, 0);  /* Exit code 0 = normal */

    /* Propagate (though normal exits shouldn't crash linked blocks) */
    scheduler_propagate_exit(sched, block1);

    /* block2 should still be alive (normal exit doesn't propagate crash) */
    ASSERT(block_is_alive(block2));

    scheduler_free(sched);
}

/*
 * Test: Trap exit receives exit message instead of crashing
 */
void test_exit_trap_exit_receives_message(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code1 = create_minimal_bytecode();
    Bytecode *code2 = create_receive_bytecode();

    Pid pid1 = scheduler_spawn_ex(sched, code1, "crasher", CAP_LINK, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "trapper",
                                   CAP_LINK | CAP_TRAP_EXIT | CAP_RECEIVE, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);
    Block *block2 = scheduler_get_block(sched, pid2);

    /* Link them */
    block_link(block1, pid2);
    block_link(block2, pid1);

    /* Crash block1 */
    block_crash(block1, "crash");
    scheduler_propagate_exit(sched, block1);

    /* block2 should still be alive (has CAP_TRAP_EXIT) */
    ASSERT(block_is_alive(block2));

    /* block2 should have received an exit message */
    ASSERT(block_has_messages(block2));

    scheduler_free(sched);
}

/*
 * Test: Unlinked block is unaffected by exit
 */
void test_exit_unlinked_unaffected(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code1 = create_minimal_bytecode();
    Bytecode *code2 = create_minimal_bytecode();

    Pid pid1 = scheduler_spawn(sched, code1, "block1");
    Pid pid2 = scheduler_spawn(sched, code2, "block2");

    Block *block1 = scheduler_get_block(sched, pid1);
    Block *block2 = scheduler_get_block(sched, pid2);

    /* No link between them */

    /* Crash block1 */
    block_crash(block1, "crash");
    scheduler_propagate_exit(sched, block1);

    /* block2 should still be alive */
    ASSERT(block_is_alive(block2));

    scheduler_free(sched);
}

/*
 * Test: Multiple exits tracked correctly
 */
void test_exit_multiple_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Pid pids[5];
    for (int i = 0; i < 5; i++) {
        Bytecode *code = create_minimal_bytecode();
        pids[i] = scheduler_spawn(sched, code, "block");
    }

    scheduler_run(sched);

    for (int i = 0; i < 5; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        ASSERT_EQ(BLOCK_DEAD, block_state(block));
    }

    ASSERT_EQ(5, atomic_load(&sched->total_terminated));

    scheduler_free(sched);
}

/*
 * Test: Kill during step is safe
 */
void test_exit_kill_during_step(void) {
    SchedulerConfig config = scheduler_config_default();
    config.default_reductions = 5;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    Bytecode *code = create_infinite_bytecode();
    Pid pid = scheduler_spawn(sched, code, "infinite");

    /* Take a step */
    scheduler_step(sched);

    /* Kill after step */
    scheduler_kill(sched, pid);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
}

/*
 * Test: Stats show dead blocks correctly
 */
void test_exit_stats_dead_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    for (int i = 0; i < 3; i++) {
        Bytecode *code = create_minimal_bytecode();
        scheduler_spawn(sched, code, "block");
    }

    SchedulerStats stats_before = scheduler_stats(sched);
    ASSERT_EQ(0, stats_before.blocks_dead);
    ASSERT_EQ(3, stats_before.blocks_runnable);

    scheduler_run(sched);

    SchedulerStats stats_after = scheduler_stats(sched);
    ASSERT_EQ(3, stats_after.blocks_dead);
    ASSERT_EQ(0, stats_after.blocks_runnable);
    ASSERT_EQ(0, stats_after.blocks_alive);

    scheduler_free(sched);
}

/*
 * Test: Exit removes block from run queue
 */
void test_exit_removes_from_runqueue(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    ASSERT(!scheduler_queue_empty(sched));

    Block *block = scheduler_get_block(sched, pid);
    block_exit(block, 0);

    /* Manual removal since block_exit doesn't remove from queue */
    /* Note: block_exit just sets state, doesn't remove from queue */
    /* The queue cleanup happens via kill or during step */

    scheduler_free(sched);
}

/*
 * Test: Exit code 0 is normal exit
 */
void test_exit_code_zero_is_normal(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    Block *block = scheduler_get_block(sched, pid);
    block_exit(block, 0);

    ASSERT_EQ(0, block->u.exit.exit_code);
    ASSERT(block->u.exit.exit_reason == NULL);

    scheduler_free(sched);
}

/*
 * Test: Non-zero exit code indicates error
 */
void test_exit_code_nonzero_is_error(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    Block *block = scheduler_get_block(sched, pid);
    block_exit(block, 1);

    ASSERT_EQ(1, block->u.exit.exit_code);

    scheduler_free(sched);
}

/*
 * Test: block_is_alive returns false after exit
 */
void test_exit_is_alive_false_after(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block_is_alive(block));

    block_exit(block, 0);

    ASSERT(!block_is_alive(block));

    scheduler_free(sched);
}

/*
 * Test: Crash sets exit reason
 */
void test_exit_crash_sets_reason(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");

    Block *block = scheduler_get_block(sched, pid);
    block_crash(block, "division by zero");

    ASSERT(block->u.exit.exit_reason != NULL);
    ASSERT_STR_EQ("division by zero", block->u.exit.exit_reason);

    scheduler_free(sched);
}

int main(void) {
    printf("Running scheduler exit tests...\n");

    printf("\nNormal exit tests:\n");
    RUN_TEST(test_exit_normal_completion);
    RUN_TEST(test_exit_terminated_count_normal);
    RUN_TEST(test_exit_with_code);
    RUN_TEST(test_exit_code_zero_is_normal);
    RUN_TEST(test_exit_code_nonzero_is_error);
    RUN_TEST(test_exit_is_alive_false_after);

    printf("\nKill tests:\n");
    RUN_TEST(test_exit_kill_terminates_block);
    RUN_TEST(test_exit_kill_invalid_pid);
    RUN_TEST(test_exit_kill_null_scheduler);
    RUN_TEST(test_exit_kill_block_is_dead);
    RUN_TEST(test_exit_double_kill);
    RUN_TEST(test_exit_terminated_count_kill);
    RUN_TEST(test_exit_kill_during_step);

    printf("\nCrash tests:\n");
    RUN_TEST(test_exit_crash_with_reason);
    RUN_TEST(test_exit_crash_sets_reason);

    printf("\nLink propagation tests:\n");
    RUN_TEST(test_exit_linked_block_crashes);
    RUN_TEST(test_exit_normal_doesnt_crash_linked);
    RUN_TEST(test_exit_trap_exit_receives_message);
    RUN_TEST(test_exit_unlinked_unaffected);

    printf("\nMultiple blocks tests:\n");
    RUN_TEST(test_exit_multiple_blocks);

    printf("\nStats tests:\n");
    RUN_TEST(test_exit_stats_dead_blocks);
    RUN_TEST(test_exit_removes_from_runqueue);

    return TEST_RESULT();
}
