/*
 * Agim - Ring Benchmark
 *
 * Classic Erlang ring benchmark: N processes passing messages in a ring.
 * Tests message passing latency and throughput in a sequential pattern.
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
#include "runtime/mailbox.h"

/* Timing Utilities */

static double get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

static double get_time_ms(void) {
    return get_time_ns() / 1e6;
}

/*
 * Simulated ring benchmark using mailboxes.
 * Each "node" in the ring is represented by a mailbox.
 * We pass a token around the ring M times.
 */
static void bench_ring_mailbox(int ring_size, int rounds) {
    printf("\nRing Benchmark (size=%d, rounds=%d):\n", ring_size, rounds);

    /* Create ring of mailboxes */
    Mailbox *ring = malloc(sizeof(Mailbox) * ring_size);
    if (!ring) {
        printf("  ERROR: Failed to allocate ring\n");
        return;
    }

    for (int i = 0; i < ring_size; i++) {
        mailbox_init(&ring[i]);
    }

    int total_hops = ring_size * rounds;

    double start = get_time_ns();

    /* Initial message */
    int64_t token = 0;
    int current = 0;

    /* Pass token around ring */
    for (int hop = 0; hop < total_hops; hop++) {
        /* Create and send message to current node */
        Value *msg = value_int(token);
        Message *m = malloc(sizeof(Message));
        if (!m || !msg) {
            if (msg) value_free(msg);
            if (m) free(m);
            printf("  ERROR: Allocation failed at hop %d\n", hop);
            break;
        }
        m->value = msg;
        m->sender = (current == 0) ? ring_size - 1 : current - 1;
        m->next = NULL;
        mailbox_push(&ring[current], m, 0);

        /* Receive and forward to next node */
        Message *received = mailbox_pop(&ring[current]);
        if (received) {
            if (received->value && received->value->type == VAL_INT) {
                token = received->value->as.integer + 1;
            }
            value_free(received->value);
            free(received);
        }

        /* Move to next node in ring */
        current = (current + 1) % ring_size;
    }

    double elapsed_ns = get_time_ns() - start;
    double elapsed_ms = elapsed_ns / 1e6;

    printf("  Total hops: %d\n", total_hops);
    printf("  Time: %.2f ms\n", elapsed_ms);
    printf("  Throughput: %.0f hops/sec\n", total_hops / (elapsed_ns / 1e9));
    printf("  Latency: %.1f ns/hop\n", elapsed_ns / total_hops);
    printf("  Final token: %lld (expected: %d)\n",
           (long long)token, total_hops);

    /* Cleanup */
    for (int i = 0; i < ring_size; i++) {
        mailbox_free(&ring[i]);
    }
    free(ring);
}

/*
 * Benchmark raw mailbox push/pop latency (single node).
 * Measures the minimal message passing overhead.
 */
static void bench_single_hop_latency(int iterations) {
    printf("\nSingle-Hop Latency Benchmark (%d iterations):\n", iterations);

    Mailbox mbox;
    mailbox_init(&mbox);

    double total_ns = 0;
    double min_ns = 1e15;
    double max_ns = 0;

    for (int i = 0; i < iterations; i++) {
        Value *msg = value_int(i);
        Message *m = malloc(sizeof(Message));
        if (!m || !msg) {
            if (msg) value_free(msg);
            if (m) free(m);
            break;
        }
        m->value = msg;
        m->sender = 0;
        m->next = NULL;

        double start = get_time_ns();
        mailbox_push(&mbox, m, 0);
        Message *received = mailbox_pop(&mbox);
        double elapsed = get_time_ns() - start;

        total_ns += elapsed;
        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;

        if (received) {
            value_free(received->value);
            free(received);
        }
    }

    double avg_ns = total_ns / iterations;
    printf("  Iterations: %d\n", iterations);
    printf("  Average: %.1f ns/round-trip\n", avg_ns);
    printf("  Min: %.1f ns\n", min_ns);
    printf("  Max: %.1f ns\n", max_ns);
    printf("  Throughput: %.0f round-trips/sec\n", 1e9 / avg_ns);

    mailbox_free(&mbox);
}

/*
 * Benchmark burst message patterns.
 * Push N messages, then pop all - common in batch processing.
 */
static void bench_burst_pattern(int burst_size, int bursts) {
    printf("\nBurst Pattern Benchmark (burst=%d, count=%d):\n", burst_size, bursts);

    Mailbox mbox;
    mailbox_init(&mbox);

    double push_total_ns = 0;
    double pop_total_ns = 0;
    int total_messages = burst_size * bursts;

    for (int b = 0; b < bursts; b++) {
        /* Push burst */
        double start = get_time_ns();
        for (int i = 0; i < burst_size; i++) {
            Value *msg = value_int(b * burst_size + i);
            Message *m = malloc(sizeof(Message));
            if (!m || !msg) {
                if (msg) value_free(msg);
                if (m) free(m);
                goto cleanup;
            }
            m->value = msg;
            m->sender = 0;
            m->next = NULL;
            mailbox_push(&mbox, m, 0);
        }
        push_total_ns += get_time_ns() - start;

        /* Pop burst */
        start = get_time_ns();
        for (int i = 0; i < burst_size; i++) {
            Message *received = mailbox_pop(&mbox);
            if (received) {
                value_free(received->value);
                free(received);
            }
        }
        pop_total_ns += get_time_ns() - start;
    }

cleanup:
    printf("  Total messages: %d\n", total_messages);
    printf("  Push time: %.2f ms (%.0f msg/sec)\n",
           push_total_ns / 1e6, total_messages / (push_total_ns / 1e9));
    printf("  Pop time: %.2f ms (%.0f msg/sec)\n",
           pop_total_ns / 1e6, total_messages / (pop_total_ns / 1e9));
    printf("  Avg push latency: %.1f ns\n", push_total_ns / total_messages);
    printf("  Avg pop latency: %.1f ns\n", pop_total_ns / total_messages);

    mailbox_free(&mbox);
}

int main(void) {
    printf("=== Agim Ring Benchmark ===\n");

    /* Single-hop latency baseline */
    bench_single_hop_latency(100000);

    /* Ring benchmarks with varying sizes */
    bench_ring_mailbox(10, 10000);      /* 100,000 hops */
    bench_ring_mailbox(100, 1000);      /* 100,000 hops */
    bench_ring_mailbox(1000, 100);      /* 100,000 hops */

    /* Burst patterns */
    bench_burst_pattern(100, 1000);     /* 100,000 messages */
    bench_burst_pattern(1000, 100);     /* 100,000 messages */
    bench_burst_pattern(10000, 10);     /* 100,000 messages */

    printf("\n=== Ring Benchmark Complete ===\n");
    return 0;
}
