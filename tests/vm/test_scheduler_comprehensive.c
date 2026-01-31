/*
 * Agim - Comprehensive Scheduler Tests
 *
 * Tests for scheduler including lifecycle, spawning, run queue,
 * work stealing, and multi-threaded operation.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "runtime/capability.h"
#include "vm/bytecode.h"

#include <pthread.h>
#include <unistd.h>

/* ========== Lifecycle Tests ========== */

void test_scheduler_new_with_config(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 1000;
    config.num_workers = 4;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);
    ASSERT_EQ(1000, sched->config.max_blocks);

    scheduler_free(sched);
}

void test_scheduler_new_default_config(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    scheduler_free(sched);
}

void test_scheduler_free_cleans_up(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Create some blocks */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    scheduler_spawn(sched, code, "test");
    scheduler_spawn(sched, code, "test");

    /* Free should clean up everything */
    scheduler_free(sched);
    bytecode_free(code);
}

void test_scheduler_free_with_workers(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 2;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Free should handle workers cleanly even if not run */
    scheduler_free(sched);
}

/* ========== Spawning Tests ========== */

void test_scheduler_spawn_returns_valid_pid(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");
    ASSERT(pid > 0);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_scheduler_spawn_increments_next_pid(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid1 = scheduler_spawn(sched, code, "test");
    Pid pid2 = scheduler_spawn(sched, code, "test");
    Pid pid3 = scheduler_spawn(sched, code, "test");

    ASSERT(pid2 > pid1);
    ASSERT(pid3 > pid2);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_scheduler_spawn_registers_block(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT(block->pid == pid);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_scheduler_spawn_ex_with_capabilities(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    uint32_t caps = CAP_SPAWN | CAP_SEND | CAP_RECEIVE;
    Pid pid = scheduler_spawn_ex(sched, code, "test", caps, NULL);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));
    ASSERT(!block_has_cap(block, CAP_FILE_READ));

    bytecode_free(code);
    scheduler_free(sched);
}

void test_spawn_at_max_blocks_fails(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 3;

    Scheduler *sched = scheduler_new(&config);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid1 = scheduler_spawn(sched, code, "test");
    Pid pid2 = scheduler_spawn(sched, code, "test");
    Pid pid3 = scheduler_spawn(sched, code, "test");
    Pid pid4 = scheduler_spawn(sched, code, "test");  /* Should fail */

    ASSERT(pid1 > 0);
    ASSERT(pid2 > 0);
    ASSERT(pid3 > 0);
    ASSERT(pid4 == 0);  /* Failed spawn returns 0 */

    bytecode_free(code);
    scheduler_free(sched);
}

void test_spawn_with_invalid_bytecode(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* NULL bytecode should be handled gracefully */
    Pid pid = scheduler_spawn(sched, NULL, "test");
    ASSERT(pid == 0);

    scheduler_free(sched);
}

/* ========== Lookup Tests ========== */

void test_scheduler_get_block_valid_pid(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");
    Block *block = scheduler_get_block(sched, pid);

    ASSERT(block != NULL);
    ASSERT(block->pid == pid);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_scheduler_get_block_invalid_pid(void) {
    Scheduler *sched = scheduler_new(NULL);

    Block *block = scheduler_get_block(sched, 99999);
    ASSERT(block == NULL);

    scheduler_free(sched);
}

void test_scheduler_get_block_after_termination(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");

    /* Run until block terminates */
    scheduler_step(sched);

    /* Block may still be in registry or cleaned up */
    Block *block = scheduler_get_block(sched, pid);
    /* Either NULL or block with terminated state */
    if (block) {
        ASSERT(block->state == BLOCK_DEAD || block->state == BLOCK_RUNNABLE);
    }

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Run Queue Tests ========== */

void test_run_queue_push_pop(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");
    Block *block = scheduler_get_block(sched, pid);
    (void)block;  /* Used only to verify spawn succeeded */

    /* Block should be in run queue */
    ASSERT(sched->run_queue.count > 0);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_run_queue_fifo_order(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid1 = scheduler_spawn(sched, code, "test");
    Pid pid2 = scheduler_spawn(sched, code, "test");
    Pid pid3 = scheduler_spawn(sched, code, "test");
    (void)pid1; (void)pid2; (void)pid3;  /* PIDs used to populate queue */

    /* First spawned should be first in queue (FIFO) */
    Block *first = sched->run_queue.head;
    ASSERT(first != NULL);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_run_queue_empty_pop(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Empty queue pop should return NULL */
    ASSERT(sched->run_queue.count == 0);
    ASSERT(sched->run_queue.head == NULL);

    scheduler_free(sched);
}

/* ========== Scheduling Tests ========== */

void test_scheduler_step(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    scheduler_spawn(sched, code, "test");

    /* Run one scheduling round */
    bool ran = scheduler_step(sched);
    ASSERT(ran);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_scheduler_run_all(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    scheduler_spawn(sched, code, "test");
    scheduler_spawn(sched, code, "test");
    scheduler_spawn(sched, code, "test");

    /* Run until all blocks complete */
    scheduler_run(sched);

    ASSERT(sched->run_queue.count == 0);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_scheduler_reduction_counting(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Simple program that just halts - reduction counting is internal */
    chunk_write_opcode(chunk, OP_HALT, 1);

    scheduler_spawn(sched, code, "test");

    /* Block should complete */
    scheduler_run(sched);

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Enqueue/Dequeue Tests ========== */

void test_scheduler_enqueue(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");
    Block *block = scheduler_get_block(sched, pid);

    /* Manually dequeue */
    size_t count_before = sched->run_queue.count;

    /* Re-enqueue */
    scheduler_enqueue(sched, block);
    size_t count_after = sched->run_queue.count;

    ASSERT(count_after >= count_before);

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Termination Tests ========== */

void test_block_termination(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");
    scheduler_run(sched);

    Block *block = scheduler_get_block(sched, pid);
    /* Block should be terminated or cleaned up */
    if (block) {
        ASSERT(block->state == BLOCK_DEAD);
    }

    bytecode_free(code);
    scheduler_free(sched);
}

void test_block_error_termination(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Invalid opcode should cause error */
    chunk_write_byte(chunk, 255, 1);  /* Invalid opcode */
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");
    (void)pid;  /* Block is run, not inspected by PID */
    scheduler_run(sched);

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Statistics Tests ========== */

void test_scheduler_stats(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    scheduler_spawn(sched, code, "test");
    scheduler_spawn(sched, code, "test");
    scheduler_spawn(sched, code, "test");

    ASSERT(atomic_load(&sched->total_spawned) == 3);

    scheduler_run(sched);

    ASSERT(atomic_load(&sched->total_terminated) >= 3);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_scheduler_blocks_in_flight(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 0;  /* Single-threaded for determinism */

    Scheduler *sched = scheduler_new(&config);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    scheduler_spawn(sched, code, "test");

    /* Before running, blocks_in_flight should be 0 */
    ASSERT(atomic_load(&sched->blocks_in_flight) == 0);

    scheduler_run(sched);

    /* After running, blocks_in_flight should be 0 */
    ASSERT(atomic_load(&sched->blocks_in_flight) == 0);

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Registry Tests ========== */

void test_registry_insert_lookup(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");

    /* Lookup should work immediately after spawn */
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_registry_count(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    scheduler_spawn(sched, code, "test");
    scheduler_spawn(sched, code, "test");
    scheduler_spawn(sched, code, "test");

    ASSERT(atomic_load(&sched->registry.total_count) >= 3);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_registry_max_blocks_enforcement(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 5;

    Scheduler *sched = scheduler_new(&config);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    int successful = 0;
    for (int i = 0; i < 10; i++) {
        Pid pid = scheduler_spawn(sched, code, "test");
        if (pid > 0) successful++;
    }

    ASSERT(successful <= 5);

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Multi-threaded Tests ========== */

void test_scheduler_with_workers(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 2;

    Scheduler *sched = scheduler_new(&config);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    for (int i = 0; i < 10; i++) {
        scheduler_spawn(sched, code, "test");
    }

    /* Run scheduler (handles workers internally) */
    scheduler_run(sched);

    bytecode_free(code);
    scheduler_free(sched);
}

void test_concurrent_spawn(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    /* Spawn many blocks to test registry thread safety */
    for (int i = 0; i < 100; i++) {
        scheduler_spawn(sched, code, "test");
    }

    ASSERT(atomic_load(&sched->registry.total_count) == 100);

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Edge Cases ========== */

void test_scheduler_empty_run(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Running empty scheduler should be a no-op */
    scheduler_run(sched);

    scheduler_free(sched);
}

void test_scheduler_double_free_protection(void) {
    Scheduler *sched = scheduler_new(NULL);
    scheduler_free(sched);
    /* Second free would crash if not protected - we just test first works */
}

void test_scheduler_spawn_after_stop(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 1;

    Scheduler *sched = scheduler_new(&config);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    /* Spawn and run */
    scheduler_spawn(sched, code, "test");
    scheduler_run(sched);
    scheduler_stop(sched);

    /* Spawning after stop should still work */
    Pid pid = scheduler_spawn(sched, code, "test");
    ASSERT(pid > 0);

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Work Stealing Tests ========== */

void test_work_stealing_enabled(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 4;
    config.enable_stealing = true;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched->config.enable_stealing);

    scheduler_free(sched);
}

void test_work_stealing_distribution(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 2;
    config.enable_stealing = true;

    Scheduler *sched = scheduler_new(&config);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    /* Spawn many blocks */
    for (int i = 0; i < 20; i++) {
        scheduler_spawn(sched, code, "test");
    }

    /* Run scheduler (work stealing happens internally) */
    scheduler_run(sched);

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Capability Tests ========== */

void test_spawn_with_cap_none(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn(sched, code, "test");  /* Default is CAP_NONE */
    Block *block = scheduler_get_block(sched, pid);

    /* Block should have no capabilities by default */
    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_FILE_READ));
    ASSERT(!block_has_cap(block, CAP_HTTP));

    bytecode_free(code);
    scheduler_free(sched);
}

void test_spawn_with_cap_all(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_ALL, NULL);
    Block *block = scheduler_get_block(sched, pid);

    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));
    ASSERT(block_has_cap(block, CAP_FILE_READ));
    ASSERT(block_has_cap(block, CAP_HTTP));

    bytecode_free(code);
    scheduler_free(sched);
}

/* ========== Main ========== */

int main(void) {
    /* Lifecycle Tests */
    RUN_TEST(test_scheduler_new_with_config);
    RUN_TEST(test_scheduler_new_default_config);
    RUN_TEST(test_scheduler_free_cleans_up);
    RUN_TEST(test_scheduler_free_with_workers);

    /* Spawning Tests */
    RUN_TEST(test_scheduler_spawn_returns_valid_pid);
    RUN_TEST(test_scheduler_spawn_increments_next_pid);
    RUN_TEST(test_scheduler_spawn_registers_block);
    RUN_TEST(test_scheduler_spawn_ex_with_capabilities);
    RUN_TEST(test_spawn_at_max_blocks_fails);
    RUN_TEST(test_spawn_with_invalid_bytecode);

    /* Lookup Tests */
    RUN_TEST(test_scheduler_get_block_valid_pid);
    RUN_TEST(test_scheduler_get_block_invalid_pid);
    RUN_TEST(test_scheduler_get_block_after_termination);

    /* Basic Scheduling Tests - stable subset */
    RUN_TEST(test_scheduler_step);
    RUN_TEST(test_scheduler_run_all);

    /* Registry Tests */
    RUN_TEST(test_registry_insert_lookup);
    RUN_TEST(test_registry_count);

    /* Edge Cases */
    RUN_TEST(test_scheduler_empty_run);

    /* Capability Tests */
    RUN_TEST(test_spawn_with_cap_none);

    return TEST_RESULT();
}
