/*
 * Agim - Mailbox Benchmark
 *
 * Measures mailbox throughput for message passing.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#include "runtime/mailbox.h"
#include "vm/value.h"

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

/* Benchmark: Direct mailbox push/pop */

static void bench_mailbox_direct(int iterations) {
    Mailbox mbox;
    mailbox_init(&mbox);

    printf("Direct Mailbox Operations:\n");

    /* Push throughput */
    BENCH_START();
    for (int i = 0; i < iterations; i++) {
        Value *msg = value_int(i);
        Message *m = malloc(sizeof(Message));
        m->value = msg;
        m->sender = 1;
        m->next = NULL;
        mailbox_push(&mbox, m, 0);  /* 0 = unlimited */
    }
    BENCH_END("mailbox_push", iterations);

    /* Pop throughput */
    BENCH_START();
    for (int i = 0; i < iterations; i++) {
        Message *msg = mailbox_pop(&mbox);
        if (msg) {
            value_free(msg->value);
            free(msg);
        }
    }
    BENCH_END("mailbox_pop", iterations);

    mailbox_free(&mbox);
}

/* Benchmark: Value creation overhead */

static void bench_value_creation(int iterations) {
    printf("\nValue Creation Overhead:\n");

    /* Integer */
    BENCH_START();
    for (int i = 0; i < iterations; i++) {
        Value *v = value_int(i);
        value_free(v);
    }
    BENCH_END("value_int create/free", iterations);

    /* String */
    BENCH_START();
    for (int i = 0; i < iterations; i++) {
        Value *v = value_string("hello");
        value_free(v);
    }
    BENCH_END("value_string create/free", iterations);

    /* Array with push */
    BENCH_START();
    for (int i = 0; i < iterations / 10; i++) {
        Value *arr = value_array();
        for (int j = 0; j < 10; j++) {
            arr = array_push(arr, value_int(j));
        }
        value_free(arr);
    }
    BENCH_END("array (10 elem) create/free", iterations / 10);
}

/* Multi-producer benchmark data */

typedef struct {
    Mailbox *mbox;
    int messages_per_thread;
    int thread_id;
    _Atomic(int) *start_flag;
} ProducerArgs;

static void *producer_thread(void *arg) {
    ProducerArgs *args = (ProducerArgs *)arg;

    /* Wait for start signal */
    while (!atomic_load(args->start_flag)) {
        /* spin */
    }

    for (int i = 0; i < args->messages_per_thread; i++) {
        Value *msg = value_int(args->thread_id * args->messages_per_thread + i);
        Message *m = malloc(sizeof(Message));
        if (!m || !msg) {
            if (msg) value_free(msg);
            if (m) free(m);
            break;
        }
        m->value = msg;
        m->sender = args->thread_id;
        m->next = NULL;
        mailbox_push(args->mbox, m, 0);
    }

    return NULL;
}

static void bench_mailbox_mpsc(int num_producers, int messages_per_producer) {
    Mailbox mbox;
    mailbox_init(&mbox);

    int total_messages = num_producers * messages_per_producer;
    printf("\nMulti-Producer Single-Consumer (%d producers x %d msgs = %d total):\n",
           num_producers, messages_per_producer, total_messages);

    pthread_t *threads = malloc(sizeof(pthread_t) * num_producers);
    ProducerArgs *args = malloc(sizeof(ProducerArgs) * num_producers);
    _Atomic(int) start_flag = 0;

    /* Create producer threads */
    for (int i = 0; i < num_producers; i++) {
        args[i].mbox = &mbox;
        args[i].messages_per_thread = messages_per_producer;
        args[i].thread_id = i;
        args[i].start_flag = &start_flag;
        pthread_create(&threads[i], NULL, producer_thread, &args[i]);
    }

    /* Start all producers simultaneously */
    BENCH_START();
    atomic_store(&start_flag, 1);

    /* Wait for all producers to finish */
    for (int i = 0; i < num_producers; i++) {
        pthread_join(threads[i], NULL);
    }
    BENCH_END("multi-producer push", total_messages);

    /* Consumer: pop all messages */
    BENCH_START();
    int consumed = 0;
    Message *msg;
    while ((msg = mailbox_pop(&mbox)) != NULL) {
        value_free(msg->value);
        free(msg);
        consumed++;
    }
    BENCH_END("single-consumer pop", consumed);

    printf("    Messages sent: %d, received: %d\n", total_messages, consumed);

    free(threads);
    free(args);
    mailbox_free(&mbox);
}

/* Comparison function for qsort */
static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/* Latency percentiles benchmark */
static void bench_latency_percentiles(int iterations) {
    printf("\nLatency Percentiles (%d samples):\n", iterations);

    Mailbox mbox;
    mailbox_init(&mbox);

    double *push_latencies = malloc(sizeof(double) * iterations);
    double *pop_latencies = malloc(sizeof(double) * iterations);
    double *roundtrip_latencies = malloc(sizeof(double) * iterations);

    if (!push_latencies || !pop_latencies || !roundtrip_latencies) {
        printf("  ERROR: Failed to allocate latency arrays\n");
        free(push_latencies);
        free(pop_latencies);
        free(roundtrip_latencies);
        return;
    }

    /* Measure push latencies */
    for (int i = 0; i < iterations; i++) {
        Value *msg = value_int(i);
        Message *m = malloc(sizeof(Message));
        m->value = msg;
        m->sender = 1;
        m->next = NULL;

        double start = get_time_ns();
        mailbox_push(&mbox, m, 0);
        push_latencies[i] = get_time_ns() - start;
    }

    /* Measure pop latencies */
    for (int i = 0; i < iterations; i++) {
        double start = get_time_ns();
        Message *msg = mailbox_pop(&mbox);
        pop_latencies[i] = get_time_ns() - start;

        if (msg) {
            value_free(msg->value);
            free(msg);
        }
    }

    /* Measure round-trip latencies (push then immediate pop) */
    for (int i = 0; i < iterations; i++) {
        Value *msg = value_int(i);
        Message *m = malloc(sizeof(Message));
        m->value = msg;
        m->sender = 1;
        m->next = NULL;

        double start = get_time_ns();
        mailbox_push(&mbox, m, 0);
        Message *received = mailbox_pop(&mbox);
        roundtrip_latencies[i] = get_time_ns() - start;

        if (received) {
            value_free(received->value);
            free(received);
        }
    }

    /* Sort latencies for percentile calculation */
    qsort(push_latencies, iterations, sizeof(double), compare_double);
    qsort(pop_latencies, iterations, sizeof(double), compare_double);
    qsort(roundtrip_latencies, iterations, sizeof(double), compare_double);

    /* Calculate percentile indices */
    int p50_idx = iterations / 2;
    int p95_idx = (int)(iterations * 0.95);
    int p99_idx = (int)(iterations * 0.99);
    int p999_idx = (int)(iterations * 0.999);

    printf("  Push latency (ns):\n");
    printf("    p50: %.1f  p95: %.1f  p99: %.1f  p99.9: %.1f\n",
           push_latencies[p50_idx], push_latencies[p95_idx],
           push_latencies[p99_idx], push_latencies[p999_idx]);

    printf("  Pop latency (ns):\n");
    printf("    p50: %.1f  p95: %.1f  p99: %.1f  p99.9: %.1f\n",
           pop_latencies[p50_idx], pop_latencies[p95_idx],
           pop_latencies[p99_idx], pop_latencies[p999_idx]);

    printf("  Round-trip latency (ns):\n");
    printf("    p50: %.1f  p95: %.1f  p99: %.1f  p99.9: %.1f\n",
           roundtrip_latencies[p50_idx], roundtrip_latencies[p95_idx],
           roundtrip_latencies[p99_idx], roundtrip_latencies[p999_idx]);

    free(push_latencies);
    free(pop_latencies);
    free(roundtrip_latencies);
    mailbox_free(&mbox);
}

/* Main */

int main(void) {
    printf("=== Agim Mailbox Benchmark ===\n\n");

    bench_mailbox_direct(100000);
    bench_value_creation(100000);

    /* Multi-producer benchmarks */
    bench_mailbox_mpsc(2, 50000);   /* 2 producers, 100k total */
    bench_mailbox_mpsc(4, 25000);   /* 4 producers, 100k total */
    bench_mailbox_mpsc(8, 12500);   /* 8 producers, 100k total */

    /* Latency percentiles */
    bench_latency_percentiles(100000);

    printf("\n=== Benchmark Complete ===\n");
    return 0;
}
