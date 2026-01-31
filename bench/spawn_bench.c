/*
 * Agim Spawn Benchmark
 *
 * Benchmark for testing agent spawn rate and memory usage at scale.
 * Target: 1 million agents in <30s, <100GB RAM
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

#include "vm/value.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "runtime/scheduler.h"
#include "runtime/worker.h"

/* Timing & Memory */

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static size_t get_memory_usage_kb(void) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return (size_t)usage.ru_maxrss;  /* KB on Linux */
    }
    return 0;
}

static void print_memory(const char *label) {
    size_t mem_kb = get_memory_usage_kb();
    if (mem_kb < 1024) {
        printf("  %s: %zu KB\n", label, mem_kb);
    } else if (mem_kb < 1024 * 1024) {
        printf("  %s: %.2f MB\n", label, mem_kb / 1024.0);
    } else {
        printf("  %s: %.2f GB\n", label, mem_kb / (1024.0 * 1024.0));
    }
}

/* Minimal Agent Bytecode */
static Bytecode *make_minimal_code(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);
    return code;
}

static Bytecode *make_loop_code(int iterations) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(iterations));
    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(0));

    /* Push iteration count */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t loop_start = chunk->code_size;

    /* Check if <= 0 */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_LE, 2);

    /* Exit if done */
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Decrement */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    /* Loop back */
    chunk_write_opcode(chunk, OP_LOOP, 4);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, offset & 0xFF, 4);

    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);
    chunk_write_opcode(chunk, OP_HALT, 5);

    return code;
}

/* Spawn Benchmark */

typedef struct SpawnResult {
    double spawn_time_ms;
    double run_time_ms;
    double total_time_ms;
    size_t memory_before_kb;
    size_t memory_after_kb;
    size_t agents_spawned;
    size_t agents_per_sec;
    double kb_per_agent;
} SpawnResult;

static SpawnResult bench_spawn(int num_agents, int work_per_agent, int num_workers,
                               bool shared_bytecode) {
    SpawnResult result = {0};
    result.memory_before_kb = get_memory_usage_kb();

    SchedulerConfig config = scheduler_config_default();
    config.num_workers = num_workers;
    config.enable_stealing = true;
    config.max_blocks = (size_t)num_agents + 1000;  /* Ensure enough capacity */

    Scheduler *sched = scheduler_new(&config);
    if (!sched) {
        fprintf(stderr, "ERROR: Failed to create scheduler\n");
        return result;
    }

    /*
     * Create bytecode(s).
     *
     * Note on ownership: Blocks don't own their bytecode - they just hold a
     * pointer. So we don't need to use bytecode_retain for shared bytecode.
     * We just need to keep the bytecode alive until after scheduler_free.
     */
    Bytecode *shared_code = NULL;
    Bytecode **codes = NULL;

    if (shared_bytecode) {
        /* All agents share one bytecode */
        shared_code = work_per_agent > 0 ? make_loop_code(work_per_agent)
                                         : make_minimal_code();
    } else {
        /* Each agent gets its own bytecode */
        codes = malloc(sizeof(Bytecode *) * num_agents);
        if (!codes) {
            scheduler_free(sched);
            return result;
        }
    }

    /* Spawn agents */
    double spawn_start = get_time_ms();

    for (int i = 0; i < num_agents; i++) {
        Bytecode *code;
        if (shared_bytecode) {
            /* All agents share the same bytecode pointer */
            code = shared_code;
        } else {
            codes[i] = work_per_agent > 0 ? make_loop_code(work_per_agent)
                                          : make_minimal_code();
            code = codes[i];
        }

        Pid pid = scheduler_spawn(sched, code, NULL);
        if (pid == PID_INVALID) {
            fprintf(stderr, "ERROR: Failed to spawn agent %d\n", i);
            break;
        }
        result.agents_spawned++;

        /* Progress indicator for large runs */
        if (num_agents >= 10000 && (i + 1) % 10000 == 0) {
            fprintf(stderr, "\r  Spawned %d/%d agents...", i + 1, num_agents);
            fflush(stderr);
        }
    }

    double spawn_end = get_time_ms();
    result.spawn_time_ms = spawn_end - spawn_start;

    if (num_agents >= 10000) {
        fprintf(stderr, "\r                                      \r");
    }

    /* Run all agents */
    double run_start = get_time_ms();
    scheduler_run(sched);
    double run_end = get_time_ms();
    result.run_time_ms = run_end - run_start;

    result.memory_after_kb = get_memory_usage_kb();
    result.total_time_ms = result.spawn_time_ms + result.run_time_ms;
    result.agents_per_sec = (size_t)(result.agents_spawned / (result.total_time_ms / 1000.0));

    size_t memory_delta = result.memory_after_kb > result.memory_before_kb
                              ? result.memory_after_kb - result.memory_before_kb
                              : 0;
    result.kb_per_agent = result.agents_spawned > 0
                              ? (double)memory_delta / result.agents_spawned
                              : 0;

    /* Cleanup: free scheduler first (frees blocks), then bytecode */
    scheduler_free(sched);

    if (shared_bytecode) {
        bytecode_free(shared_code);
    } else {
        for (size_t i = 0; i < result.agents_spawned; i++) {
            bytecode_free(codes[i]);
        }
        free(codes);
    }

    return result;
}

static void print_result(const char *name, SpawnResult *r) {
    printf("  %-30s %8zu agents | spawn: %8.2f ms | run: %8.2f ms | "
           "%8zu agents/sec | %.2f KB/agent\n",
           name, r->agents_spawned, r->spawn_time_ms, r->run_time_ms,
           r->agents_per_sec, r->kb_per_agent);
}

/* Main */

int main(int argc, char **argv) {
    int target_agents = 100000;  /* Default: 100K agents */
    int num_workers = 4;

    if (argc > 1) {
        target_agents = atoi(argv[1]);
        if (target_agents <= 0) target_agents = 100000;
    }
    if (argc > 2) {
        num_workers = atoi(argv[2]);
        if (num_workers < 0) num_workers = 4;
    }

    printf("================================================================\n");
    printf("    AGIM SPAWN BENCHMARK\n");
    printf("    Target: %d agents with %d workers\n", target_agents, num_workers);
    printf("================================================================\n\n");

    /* Note: Memory pools are auto-initialized on first use */
    (void)0;

    print_memory("Initial memory");
    printf("\n");

    /* Test 1: Separate bytecode */
    printf("--- Test 1: Separate bytecode (%d workers) ---\n", num_workers);
    fflush(stdout);
    {
        SpawnResult r1 = bench_spawn(target_agents / 10, 10, num_workers, false);
        print_result("Result:", &r1);
    }
    printf("\n");

    /* Test 2: Shared bytecode */
    printf("--- Test 2: Shared bytecode (%d workers) ---\n", num_workers);
    fflush(stdout);
    {
        SpawnResult r2 = bench_spawn(target_agents / 10, 10, num_workers, true);
        print_result("Result:", &r2);
    }
    printf("\n");

    print_memory("Peak memory");

    /* Summary */
    printf("\n================================================================\n");
    printf("    SUMMARY\n");
    printf("================================================================\n");

    SpawnResult final = bench_spawn(target_agents, 10, num_workers, false);
    printf("  Agents:        %zu\n", final.agents_spawned);
    printf("  Total time:    %.2f ms (%.2f s)\n",
           final.total_time_ms, final.total_time_ms / 1000.0);
    printf("  Spawn rate:    %zu agents/sec\n", final.agents_per_sec);
    printf("  Memory/agent:  %.2f KB\n", final.kb_per_agent);

    double projected_1m_time = (1000000.0 / final.agents_per_sec);
    double projected_1m_mem = final.kb_per_agent * 1000000.0 / (1024.0 * 1024.0);
    printf("\n  Projected for 1M agents:\n");
    printf("    Time:   %.1f seconds\n", projected_1m_time);
    printf("    Memory: %.1f GB\n", projected_1m_mem);

    if (projected_1m_time < 30 && projected_1m_mem < 100) {
        printf("\n  [PASS] Target achievable: 1M agents in <30s and <100GB\n");
    } else {
        printf("\n  [INFO] Current projections exceed targets\n");
    }

    printf("================================================================\n");

    /* Note: pools cleanup is handled at exit */
    return 0;
}
