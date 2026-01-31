/*
 * Agim Parallel Execution Benchmark
 *
 * Demonstrates BEAM-like parallelism with thousands of agents.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "vm/value.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "runtime/scheduler.h"
#include "runtime/worker.h"

/* Timing */

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* Create Worker Bytecode */

static Bytecode *make_loop_code(int iterations) {
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

/* Benchmark */

static void bench_parallel(int num_agents, int work_per_agent, int num_workers) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = num_workers;
    config.enable_stealing = true;

    Scheduler *sched = scheduler_new(&config);
    if (!sched) {
        printf("  ERROR: Failed to create scheduler\n");
        return;
    }

    Bytecode **codes = malloc(sizeof(Bytecode *) * num_agents);

    for (int i = 0; i < num_agents; i++) {
        codes[i] = make_loop_code(work_per_agent);
        char name[32];
        snprintf(name, sizeof(name), "agent_%d", i);
        scheduler_spawn(sched, codes[i], name);
    }

    double start = get_time_ms();
    scheduler_run(sched);
    double end = get_time_ms();
    double elapsed = end - start;

    SchedulerStats stats = scheduler_stats(sched);
    double agents_per_sec = num_agents / (elapsed / 1000.0);

    printf("  %d workers: %8.2f ms | %10.0f agents/sec | switches: %zu\n",
           num_workers, elapsed, agents_per_sec, stats.context_switches);

    scheduler_free(sched);
    for (int i = 0; i < num_agents; i++) {
        bytecode_free(codes[i]);
    }
    free(codes);
}

/* Main */

int main(void) {
    printf("================================================\n");
    fflush(stdout);
    printf("    AGIM PARALLEL EXECUTION BENCHMARK\n");
    fflush(stdout);
    printf("    (BEAM-like Lightweight Agents)\n");
    fflush(stdout);
    printf("================================================\n\n");
    fflush(stdout);

    /* Test 1: 100 agents */
    printf("--- 100 agents x 1000 iterations ---\n");
    fflush(stdout);
    printf("Starting single-threaded test...\n");
    fflush(stdout);
    bench_parallel(100, 1000, 0);  /* single-threaded */
    bench_parallel(100, 1000, 2);
    bench_parallel(100, 1000, 4);

    /* Test 2: 1000 agents */
    printf("\n--- 1000 agents x 500 iterations ---\n");
    bench_parallel(1000, 500, 0);
    bench_parallel(1000, 500, 2);
    bench_parallel(1000, 500, 4);

    /* Test 3: 5000 agents (lightweight) */
    printf("\n--- 5000 agents x 100 iterations ---\n");
    bench_parallel(5000, 100, 0);
    bench_parallel(5000, 100, 2);
    bench_parallel(5000, 100, 4);

    printf("\n================================================\n");
    printf("Benchmark complete!\n");
    printf("================================================\n");

    return 0;
}
