/*
 * Agim GC Benchmark
 *
 * Benchmark for testing garbage collection pause times.
 * Target: Max pause < 10ms
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <float.h>

#include "vm/value.h"
#include "vm/gc.h"
#include "vm/vm.h"
#include "vm/bytecode.h"

/* Timing */

static double get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000.0 + ts.tv_nsec / 1000.0;
}

/* Allocate Objects */
static int allocate_objects(Heap *heap, int count) {
    int allocated = 0;
    for (int i = 0; i < count; i++) {
        /* Allocate different types to stress GC */
        Value *v = NULL;
        switch (i % 4) {
        case 0:
            v = heap_alloc(heap, VAL_ARRAY);
            break;
        case 1:
            v = heap_alloc(heap, VAL_MAP);
            break;
        case 2:
            v = heap_alloc(heap, VAL_STRING);
            break;
        case 3:
            v = heap_alloc(heap, VAL_INT);
            break;
        }
        if (v) allocated++;
    }
    return allocated;
}

/* GC Pause Measurement */

typedef struct GCStats {
    double min_pause_us;
    double max_pause_us;
    double avg_pause_us;
    double total_pause_us;
    int gc_count;
    size_t bytes_before;
    size_t bytes_after;
} GCStats;

static GCStats measure_full_gc(Heap *heap, VM *vm, int iterations) {
    GCStats stats = {
        .min_pause_us = DBL_MAX,
        .max_pause_us = 0,
        .avg_pause_us = 0,
        .total_pause_us = 0,
        .gc_count = 0
    };

    for (int i = 0; i < iterations; i++) {
        /* Allocate some objects (they'll be garbage since not rooted) */
        allocate_objects(heap, 100);

        stats.bytes_before = heap->bytes_allocated;

        /* Measure full GC */
        double start = get_time_us();
        gc_collect(heap, vm);
        double end = get_time_us();
        double pause = end - start;

        stats.bytes_after = heap->bytes_allocated;

        if (pause < stats.min_pause_us) stats.min_pause_us = pause;
        if (pause > stats.max_pause_us) stats.max_pause_us = pause;
        stats.total_pause_us += pause;
        stats.gc_count++;
    }

    if (stats.gc_count > 0) {
        stats.avg_pause_us = stats.total_pause_us / stats.gc_count;
    }

    return stats;
}

static GCStats measure_incremental_gc(Heap *heap, VM *vm, int iterations) {
    GCStats stats = {
        .min_pause_us = DBL_MAX,
        .max_pause_us = 0,
        .avg_pause_us = 0,
        .total_pause_us = 0,
        .gc_count = 0
    };

    for (int i = 0; i < iterations; i++) {
        /* Allocate some objects (they'll be garbage since not rooted) */
        allocate_objects(heap, 100);

        stats.bytes_before = heap->bytes_allocated;

        /* Start incremental GC */
        if (!gc_start_incremental(heap, vm)) {
            continue;
        }

        /* Measure each step */
        double step_max = 0;
        int steps = 0;

        while (gc_in_progress(heap)) {
            double start = get_time_us();
            gc_step(heap, vm);
            double end = get_time_us();
            double pause = end - start;

            if (pause > step_max) step_max = pause;
            stats.total_pause_us += pause;
            steps++;
        }

        stats.bytes_after = heap->bytes_allocated;

        /* Track the worst step pause, not total */
        if (step_max < stats.min_pause_us) stats.min_pause_us = step_max;
        if (step_max > stats.max_pause_us) stats.max_pause_us = step_max;
        stats.gc_count++;
    }

    if (stats.gc_count > 0) {
        stats.avg_pause_us = stats.total_pause_us / stats.gc_count;
    }

    return stats;
}

static void print_gc_stats(const char *name, GCStats *stats) {
    printf("  %-25s %4d GCs | min: %7.1f us | max: %7.1f us | "
           "avg: %7.1f us | total: %7.1f ms\n",
           name, stats->gc_count,
           stats->min_pause_us, stats->max_pause_us, stats->avg_pause_us,
           stats->total_pause_us / 1000.0);
}

/* Main */

int main(void) {
    printf("================================================================\n");
    printf("    AGIM GC BENCHMARK\n");
    printf("    Target: Max pause < 10ms (10000 us)\n");
    printf("================================================================\n\n");

    /* Note: In debug builds, GC prints stats to stderr - redirect if needed */
    printf("(Note: Debug builds print GC stats to stderr)\n\n");

    /* Create VM for marking roots */
    VM *vm = vm_new();
    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);
    vm_load(vm, code);

    /* Test 1: Small heap with enough space */
    printf("--- Small Heap (256KB max) ---\n");
    fflush(stdout);
    {
        GCConfig config = gc_config_default();
        config.initial_heap_size = 32 * 1024;
        config.max_heap_size = 256 * 1024;
        config.incremental_step = 50;
        Heap *heap = heap_new(&config);

        GCStats full = measure_full_gc(heap, vm, 10);
        print_gc_stats("Full GC:", &full);

        heap_free(heap);
        heap = heap_new(&config);

        GCStats incr = measure_incremental_gc(heap, vm, 10);
        print_gc_stats("Incremental GC (per step):", &incr);

        heap_free(heap);
    }
    printf("\n");

    /* Test 2: Medium heap */
    printf("--- Medium Heap (1MB max) ---\n");
    fflush(stdout);
    {
        GCConfig config = gc_config_default();
        config.initial_heap_size = 128 * 1024;
        config.max_heap_size = 1024 * 1024;
        config.incremental_step = 100;
        Heap *heap = heap_new(&config);

        GCStats full = measure_full_gc(heap, vm, 10);
        print_gc_stats("Full GC:", &full);

        heap_free(heap);
        heap = heap_new(&config);

        GCStats incr = measure_incremental_gc(heap, vm, 10);
        print_gc_stats("Incremental GC (per step):", &incr);

        heap_free(heap);
    }
    printf("\n");

    /* Test 3: Large heap (4MB) */
    printf("--- Large Heap (4MB max) ---\n");
    fflush(stdout);
    {
        GCConfig config = gc_config_default();
        config.initial_heap_size = 512 * 1024;
        config.max_heap_size = 4 * 1024 * 1024;
        config.incremental_step = 100;
        Heap *heap = heap_new(&config);

        GCStats full = measure_full_gc(heap, vm, 10);
        print_gc_stats("Full GC:", &full);

        heap_free(heap);
        heap = heap_new(&config);

        GCStats incr = measure_incremental_gc(heap, vm, 10);
        print_gc_stats("Incremental GC (per step):", &incr);

        heap_free(heap);
    }
    printf("\n");

    /* Summary */
    printf("================================================================\n");
    printf("    SUMMARY\n");
    printf("================================================================\n");
    fflush(stdout);

    GCConfig config = gc_config_default();
    config.max_heap_size = 1024 * 1024;  /* 1MB - typical agent */
    Heap *heap = heap_new(&config);

    GCStats final_full = measure_full_gc(heap, vm, 20);
    heap_free(heap);

    heap = heap_new(&config);
    GCStats final_incr = measure_incremental_gc(heap, vm, 20);
    heap_free(heap);

    printf("  Full GC max pause:        %7.1f us (%.2f ms)\n",
           final_full.max_pause_us, final_full.max_pause_us / 1000.0);
    printf("  Incremental step max:     %7.1f us (%.2f ms)\n",
           final_incr.max_pause_us, final_incr.max_pause_us / 1000.0);

    if (final_incr.max_pause_us < 10000) {
        printf("\n  [PASS] Incremental GC achieves <10ms pause target\n");
    } else {
        printf("\n  [INFO] Incremental GC pause exceeds 10ms target\n");
    }

    printf("================================================================\n");

    bytecode_free(code);
    vm_free(vm);

    return 0;
}
