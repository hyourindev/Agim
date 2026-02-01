/*
 * Agim - File I/O Benchmark
 *
 * Measures file read/write throughput and latency.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Timing Utilities */

static double get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

static double _bench_start_time;

#define BENCH_START() _bench_start_time = get_time_ns()
#define BENCH_END(name, ops) do { \
    double _bench_end = get_time_ns(); \
    double _bench_time_ns = _bench_end - _bench_start_time; \
    double _bench_time_ms = _bench_time_ns / 1e6; \
    double _ops_per_sec = (ops) / (_bench_time_ns / 1e9); \
    double _ns_per_op = _bench_time_ns / (ops); \
    printf("  %-35s %8.2f ms  %12.0f ops/sec  %8.1f ns/op\n", \
           name, _bench_time_ms, _ops_per_sec, _ns_per_op); \
} while(0)

/* Benchmark: Small file writes */
static void bench_small_writes(int iterations, const char *tmpdir) {
    printf("\nSmall File Writes (%d files, 100 bytes each):\n", iterations);

    char path[256];
    const char *data = "0123456789012345678901234567890123456789"
                       "0123456789012345678901234567890123456789"
                       "01234567890123456789";  /* 100 bytes */

    BENCH_START();
    for (int i = 0; i < iterations; i++) {
        snprintf(path, sizeof(path), "%s/test_%d.tmp", tmpdir, i);
        FILE *f = fopen(path, "w");
        if (f) {
            fwrite(data, 1, 100, f);
            fclose(f);
        }
    }
    BENCH_END("write 100 bytes", iterations);

    /* Cleanup */
    for (int i = 0; i < iterations; i++) {
        snprintf(path, sizeof(path), "%s/test_%d.tmp", tmpdir, i);
        unlink(path);
    }
}

/* Benchmark: Small file reads */
static void bench_small_reads(int iterations, const char *tmpdir) {
    printf("\nSmall File Reads (%d files, 100 bytes each):\n", iterations);

    char path[256];
    const char *data = "0123456789012345678901234567890123456789"
                       "0123456789012345678901234567890123456789"
                       "01234567890123456789";
    char buffer[128];

    /* Create test files */
    for (int i = 0; i < iterations; i++) {
        snprintf(path, sizeof(path), "%s/test_%d.tmp", tmpdir, i);
        FILE *f = fopen(path, "w");
        if (f) {
            fwrite(data, 1, 100, f);
            fclose(f);
        }
    }

    BENCH_START();
    for (int i = 0; i < iterations; i++) {
        snprintf(path, sizeof(path), "%s/test_%d.tmp", tmpdir, i);
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(buffer, 1, 100, f);
            (void)n;
            fclose(f);
        }
    }
    BENCH_END("read 100 bytes", iterations);

    /* Cleanup */
    for (int i = 0; i < iterations; i++) {
        snprintf(path, sizeof(path), "%s/test_%d.tmp", tmpdir, i);
        unlink(path);
    }
}

/* Benchmark: Large file write */
static void bench_large_write(size_t size_mb, const char *tmpdir) {
    printf("\nLarge File Write (%zu MB):\n", size_mb);

    size_t size = size_mb * 1024 * 1024;
    char *data = malloc(size);
    if (!data) {
        printf("  ERROR: Failed to allocate %zu bytes\n", size);
        return;
    }
    memset(data, 'X', size);

    char path[256];
    snprintf(path, sizeof(path), "%s/large_test.tmp", tmpdir);

    BENCH_START();
    FILE *f = fopen(path, "w");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
    double elapsed_ns = get_time_ns() - _bench_start_time;
    double elapsed_ms = elapsed_ns / 1e6;
    double throughput_mbs = (size / (1024.0 * 1024.0)) / (elapsed_ns / 1e9);

    printf("  %-35s %8.2f ms  %8.2f MB/s\n", "write throughput", elapsed_ms, throughput_mbs);

    free(data);
    unlink(path);
}

/* Benchmark: Large file read */
static void bench_large_read(size_t size_mb, const char *tmpdir) {
    printf("\nLarge File Read (%zu MB):\n", size_mb);

    size_t size = size_mb * 1024 * 1024;
    char *data = malloc(size);
    if (!data) {
        printf("  ERROR: Failed to allocate %zu bytes\n", size);
        return;
    }
    memset(data, 'X', size);

    char path[256];
    snprintf(path, sizeof(path), "%s/large_test.tmp", tmpdir);

    /* Write test file */
    FILE *f = fopen(path, "w");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }

    /* Read benchmark */
    char *buffer = malloc(size);
    if (!buffer) {
        printf("  ERROR: Failed to allocate read buffer\n");
        free(data);
        unlink(path);
        return;
    }

    BENCH_START();
    f = fopen(path, "r");
    if (f) {
        size_t n = fread(buffer, 1, size, f);
        (void)n;
        fclose(f);
    }
    double elapsed_ns = get_time_ns() - _bench_start_time;
    double elapsed_ms = elapsed_ns / 1e6;
    double throughput_mbs = (size / (1024.0 * 1024.0)) / (elapsed_ns / 1e9);

    printf("  %-35s %8.2f ms  %8.2f MB/s\n", "read throughput", elapsed_ms, throughput_mbs);

    free(data);
    free(buffer);
    unlink(path);
}

/* Benchmark: Sequential writes to single file */
static void bench_sequential_writes(int iterations, const char *tmpdir) {
    printf("\nSequential Writes (%d x 1KB to single file):\n", iterations);

    char data[1024];
    memset(data, 'Y', sizeof(data));

    char path[256];
    snprintf(path, sizeof(path), "%s/seq_test.tmp", tmpdir);

    BENCH_START();
    FILE *f = fopen(path, "w");
    if (f) {
        for (int i = 0; i < iterations; i++) {
            fwrite(data, 1, sizeof(data), f);
        }
        fclose(f);
    }
    double elapsed_ns = get_time_ns() - _bench_start_time;
    double throughput_mbs = ((iterations * 1024.0) / (1024.0 * 1024.0)) / (elapsed_ns / 1e9);

    printf("  %-35s %8.2f ms  %8.2f MB/s\n",
           "sequential write", elapsed_ns / 1e6, throughput_mbs);

    unlink(path);
}

int main(void) {
    printf("=== Agim File I/O Benchmark ===\n");

    /* Use /tmp for benchmarks */
    const char *tmpdir = "/tmp/agim_io_bench";
    char mkdir_cmd[256];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", tmpdir);
    if (system(mkdir_cmd) != 0) {
        printf("ERROR: Failed to create temp directory\n");
        return 1;
    }

    bench_small_writes(1000, tmpdir);
    bench_small_reads(1000, tmpdir);
    bench_large_write(10, tmpdir);
    bench_large_read(10, tmpdir);
    bench_sequential_writes(10000, tmpdir);

    /* Cleanup */
    char rmdir_cmd[256];
    snprintf(rmdir_cmd, sizeof(rmdir_cmd), "rm -rf %s", tmpdir);
    system(rmdir_cmd);

    printf("\n=== I/O Benchmark Complete ===\n");
    return 0;
}
