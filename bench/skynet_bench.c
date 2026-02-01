/*
 * Agim - Skynet Benchmark
 *
 * Classic Erlang benchmark: spawn tree of processes, aggregate results.
 * Tests process creation, message passing, and memory efficiency at scale.
 *
 * Skynet creates a 10-ary tree where:
 * - Level 0: 1 root
 * - Level 1: 10 children
 * - Level 2: 100 children
 * ...
 * - Level 6: 1,000,000 leaf processes
 *
 * Each leaf sends its number to parent, parents sum and forward.
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
#include "runtime/block.h"
#include "runtime/mailbox.h"

/* Timing & Memory Utilities */

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static size_t get_memory_usage_kb(void) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return (size_t)usage.ru_maxrss;
    }
    return 0;
}

static void print_memory(const char *label, size_t baseline_kb) {
    size_t mem_kb = get_memory_usage_kb();
    size_t diff_kb = mem_kb > baseline_kb ? mem_kb - baseline_kb : 0;
    if (diff_kb < 1024) {
        printf("  %s: %zu KB (total: %.2f MB)\n", label, diff_kb, mem_kb / 1024.0);
    } else if (diff_kb < 1024 * 1024) {
        printf("  %s: %.2f MB (total: %.2f MB)\n", label, diff_kb / 1024.0, mem_kb / 1024.0);
    } else {
        printf("  %s: %.2f GB (total: %.2f GB)\n", label, diff_kb / (1024.0 * 1024.0), mem_kb / (1024.0 * 1024.0));
    }
}

/* Calculate expected sum: 0 + 1 + 2 + ... + (n-1) = n*(n-1)/2 */
static int64_t expected_sum(int64_t n) {
    return n * (n - 1) / 2;
}

/* Calculate total processes in tree */
static int64_t total_processes(int depth, int fanout) {
    int64_t total = 0;
    int64_t level_count = 1;
    for (int d = 0; d <= depth; d++) {
        total += level_count;
        level_count *= fanout;
    }
    return total;
}

/* Minimal bytecode that halts immediately */
static Bytecode *make_halt_code(void) {
    Bytecode *code = bytecode_new();
    if (!code) return NULL;
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_HALT, 1);
    return code;
}

/*
 * Simplified Skynet benchmark using direct process operations.
 * Instead of actual message passing (which requires full runtime setup),
 * we measure raw spawn throughput in a tree pattern.
 */
static void bench_skynet_spawn(int depth, int fanout) {
    int64_t num_processes = total_processes(depth, fanout);
    printf("\nSkynet Spawn Benchmark (depth=%d, fanout=%d, processes=%lld):\n",
           depth, fanout, (long long)num_processes);

    size_t baseline_mem = get_memory_usage_kb();

    Scheduler *sched = scheduler_new(NULL);
    if (!sched) {
        printf("  ERROR: Failed to create scheduler\n");
        return;
    }

    Bytecode *code = make_halt_code();
    if (!code) {
        printf("  ERROR: Failed to create bytecode\n");
        scheduler_free(sched);
        return;
    }

    double start = get_time_ms();

    /* Spawn all processes in tree pattern */
    int64_t spawned = 0;
    for (int64_t i = 0; i < num_processes; i++) {
        Pid pid = scheduler_spawn(sched, code, NULL);
        if (pid != 0) {
            spawned++;
        } else {
            break;
        }
    }

    double spawn_time = get_time_ms() - start;

    printf("  Spawned: %lld processes\n", (long long)spawned);
    printf("  Spawn time: %.2f ms\n", spawn_time);
    printf("  Spawn rate: %.0f processes/sec\n", spawned / (spawn_time / 1000.0));
    print_memory("Memory used", baseline_mem);

    /* Run to completion */
    start = get_time_ms();
    scheduler_run(sched);
    double run_time = get_time_ms() - start;

    printf("  Run time: %.2f ms\n", run_time);
    printf("  Total time: %.2f ms\n", spawn_time + run_time);

    bytecode_free(code);
    scheduler_free(sched);
}

/*
 * Mailbox throughput with simulated tree aggregation pattern.
 * Simulates the message flow of skynet without full process tree.
 */
static void bench_skynet_messages(int leaf_count) {
    printf("\nSkynet Message Pattern Benchmark (%d simulated leaves):\n", leaf_count);

    Mailbox mbox;
    mailbox_init(&mbox);

    double start = get_time_ms();

    /* Simulate leaf messages */
    for (int i = 0; i < leaf_count; i++) {
        Value *msg = value_int(i);
        Message *m = malloc(sizeof(Message));
        if (!m || !msg) {
            if (msg) value_free(msg);
            if (m) free(m);
            break;
        }
        m->value = msg;
        m->sender = i;
        m->next = NULL;
        mailbox_push(&mbox, m, 0);
    }

    double push_time = get_time_ms() - start;

    /* Aggregate (sum all values) */
    start = get_time_ms();
    int64_t sum = 0;
    for (int i = 0; i < leaf_count; i++) {
        Message *msg = mailbox_pop(&mbox);
        if (msg) {
            if (msg->value && msg->value->type == VAL_INT) {
                sum += msg->value->as.integer;
            }
            value_free(msg->value);
            free(msg);
        }
    }
    double pop_time = get_time_ms() - start;

    int64_t exp = expected_sum(leaf_count);
    printf("  Messages: %d\n", leaf_count);
    printf("  Push time: %.2f ms (%.0f msg/sec)\n",
           push_time, leaf_count / (push_time / 1000.0));
    printf("  Pop+aggregate time: %.2f ms (%.0f msg/sec)\n",
           pop_time, leaf_count / (pop_time / 1000.0));
    printf("  Sum: %lld (expected: %lld) %s\n",
           (long long)sum, (long long)exp, sum == exp ? "✓" : "✗");

    mailbox_free(&mbox);
}

int main(void) {
    printf("=== Agim Skynet Benchmark ===\n");

    /* Small scale for quick testing */
    bench_skynet_spawn(3, 10);    /* 1,111 processes */
    bench_skynet_spawn(4, 10);    /* 11,111 processes */
    bench_skynet_spawn(5, 10);    /* 111,111 processes */

    /* Message aggregation pattern */
    bench_skynet_messages(10000);
    bench_skynet_messages(100000);
    bench_skynet_messages(1000000);

    printf("\n=== Skynet Benchmark Complete ===\n");
    return 0;
}
