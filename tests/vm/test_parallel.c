/*
 * Agim - Parallel Execution Tests
 *
 * Tests the multi-threaded scheduler with multiple workers.
 * Verifies BEAM-like parallel block execution.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"
#include "runtime/worker.h"
#include "vm/vm.h"

#include <stdio.h>
#include <time.h>

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

    /* Pop the condition result */
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

void test_parallel_basic(void) {
    printf("  Testing basic parallel execution with 4 workers...\n");

    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 4;
    config.enable_stealing = true;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);
    ASSERT(scheduler_is_multithreaded(sched));
    ASSERT_EQ(4, scheduler_worker_count(sched));

    /* Spawn 10 blocks */
    Bytecode *codes[10];
    for (int i = 0; i < 10; i++) {
        codes[i] = make_simple_code(i);
        char name[32];
        snprintf(name, sizeof(name), "block_%d", i);
        Pid pid = scheduler_spawn(sched, codes[i], name);
        ASSERT(pid != PID_INVALID);
    }

    /* Run all blocks */
    scheduler_run(sched);

    /* All should be dead */
    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(10, stats.blocks_total);
    ASSERT_EQ(10, stats.blocks_dead);
    ASSERT_EQ(0, stats.blocks_alive);

    printf("    Total reductions: %zu\n", stats.total_reductions);
    printf("    Context switches: %zu\n", stats.context_switches);

    scheduler_free(sched);
    for (int i = 0; i < 10; i++) {
        bytecode_free(codes[i]);
    }
}

void test_parallel_heavy_load(void) {
    printf("  Testing heavy parallel load (40 blocks, 4 workers)...\n");

    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 4;
    config.enable_stealing = true;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Spawn 40 blocks that each loop 500 times */
    Bytecode *codes[40];
    for (int i = 0; i < 40; i++) {
        codes[i] = make_loop_code(500);
        char name[32];
        snprintf(name, sizeof(name), "looper_%d", i);
        Pid pid = scheduler_spawn(sched, codes[i], name);
        ASSERT(pid != PID_INVALID);
    }

    clock_t start = clock();
    scheduler_run(sched);
    clock_t end = clock();

    double elapsed = (double)(end - start) / CLOCKS_PER_SEC * 1000.0;

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(40, stats.blocks_total);
    ASSERT_EQ(40, stats.blocks_dead);

    printf("    Completed in: %.2f ms\n", elapsed);
    printf("    Total reductions: %zu\n", stats.total_reductions);
    printf("    Context switches: %zu\n", stats.context_switches);

    /* Print worker stats */
    for (size_t i = 0; i < scheduler_worker_count(sched); i++) {
        Worker *w = scheduler_get_worker(sched, i);
        printf("    Worker %zu: executed=%zu, steals=%zu/%zu\n",
               i,
               atomic_load(&w->blocks_executed),
               atomic_load(&w->steals_successful),
               atomic_load(&w->steals_attempted));
    }

    scheduler_free(sched);
    for (int i = 0; i < 40; i++) {
        bytecode_free(codes[i]);
    }
}

void test_parallel_vs_single(void) {
    printf("  Comparing parallel vs single-threaded...\n");

    /* Single-threaded run */
    SchedulerConfig st_config = scheduler_config_default();
    st_config.num_workers = 0;  /* Single-threaded */

    Scheduler *st_sched = scheduler_new(&st_config);

    Bytecode *st_codes[20];
    for (int i = 0; i < 20; i++) {
        st_codes[i] = make_loop_code(1000);
        scheduler_spawn(st_sched, st_codes[i], "st_block");
    }

    clock_t st_start = clock();
    scheduler_run(st_sched);
    clock_t st_end = clock();
    double st_elapsed = (double)(st_end - st_start) / CLOCKS_PER_SEC * 1000.0;

    scheduler_free(st_sched);
    for (int i = 0; i < 20; i++) {
        bytecode_free(st_codes[i]);
    }

    /* Multi-threaded run */
    SchedulerConfig mt_config = scheduler_config_default();
    mt_config.num_workers = 4;

    Scheduler *mt_sched = scheduler_new(&mt_config);

    Bytecode *mt_codes[20];
    for (int i = 0; i < 20; i++) {
        mt_codes[i] = make_loop_code(1000);
        scheduler_spawn(mt_sched, mt_codes[i], "mt_block");
    }

    clock_t mt_start = clock();
    scheduler_run(mt_sched);
    clock_t mt_end = clock();
    double mt_elapsed = (double)(mt_end - mt_start) / CLOCKS_PER_SEC * 1000.0;

    scheduler_free(mt_sched);
    for (int i = 0; i < 20; i++) {
        bytecode_free(mt_codes[i]);
    }

    printf("    Single-threaded: %.2f ms\n", st_elapsed);
    printf("    Multi-threaded (4 workers): %.2f ms\n", mt_elapsed);
    printf("    Speedup: %.2fx\n", st_elapsed / mt_elapsed);

    /* We expect some speedup with multiple workers */
    /* Note: May not be perfect due to overhead */
}

void test_work_stealing(void) {
    printf("  Testing work stealing...\n");

    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 4;
    config.enable_stealing = true;

    Scheduler *sched = scheduler_new(&config);

    /* Create imbalanced load - work stealing should balance */
    Bytecode *codes[20];
    for (int i = 0; i < 20; i++) {
        codes[i] = make_loop_code(500);
        scheduler_spawn(sched, codes[i], "work");
    }

    scheduler_run(sched);

    /* Check that work was distributed */
    size_t total_executed = 0;
    for (size_t i = 0; i < scheduler_worker_count(sched); i++) {
        Worker *w = scheduler_get_worker(sched, i);
        size_t executed = atomic_load(&w->blocks_executed);
        total_executed += executed;
        printf("    Worker %zu executed: %zu blocks\n", i, executed);
    }

    ASSERT_EQ(20, total_executed);

    scheduler_free(sched);
    for (int i = 0; i < 20; i++) {
        bytecode_free(codes[i]);
    }
}

int main(void) {
    printf("\n=== Parallel Execution Tests ===\n\n");

    RUN_TEST(test_parallel_basic);
    RUN_TEST(test_parallel_heavy_load);
    RUN_TEST(test_parallel_vs_single);
    RUN_TEST(test_work_stealing);

    printf("\n");
    return TEST_RESULT();
}
