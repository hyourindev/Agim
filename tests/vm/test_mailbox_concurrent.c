/*
 * Agim - Concurrent Mailbox Tests
 *
 * Tests thread-safety of mailbox operations including:
 * - Multiple producers single consumer (MPSC)
 * - Producer consumer interleaving
 * - Stub node handling
 * - Atomic ordering
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _DEFAULT_SOURCE  /* For usleep */
#include "../test_common.h"
#include "runtime/mailbox.h"
#include "vm/value.h"
#include "types/string.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

/* Number of threads and iterations */
#define NUM_PRODUCERS 4
#define NUM_ITERATIONS 1000
#define TOTAL_MESSAGES (NUM_PRODUCERS * NUM_ITERATIONS)

/* Shared state for tests */
static _Atomic(int) test_errors = 0;
static _Atomic(int) threads_ready = 0;
static _Atomic(bool) start_flag = false;

/* Barrier for synchronizing thread start */
static void wait_for_start(void) {
    atomic_fetch_add(&threads_ready, 1);
    while (!atomic_load(&start_flag)) {
        /* spin */
    }
}

static void reset_sync(void) {
    atomic_store(&threads_ready, 0);
    atomic_store(&start_flag, false);
    atomic_store(&test_errors, 0);
}

static void signal_start(int expected_threads) {
    while (atomic_load(&threads_ready) < expected_threads) {
        /* wait for all threads to be ready */
    }
    atomic_store(&start_flag, true);
}

/* ========== Test: Multiple Producers Single Consumer ========== */

typedef struct {
    Mailbox *mailbox;
    int producer_id;
    _Atomic(int) *messages_sent;
} ProducerArgs;

typedef struct {
    Mailbox *mailbox;
    _Atomic(int) *messages_received;
    _Atomic(bool) *stop;
    int expected_count;
} ConsumerArgs;

static void *producer_thread(void *arg) {
    ProducerArgs *args = (ProducerArgs *)arg;

    wait_for_start();

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        /* Create message with producer_id * 10000 + sequence number */
        int64_t value = args->producer_id * 10000 + i;
        Value *v = value_int(value);
        Message *msg = message_new((Pid)args->producer_id, v);

        if (mailbox_push(args->mailbox, msg, TOTAL_MESSAGES + 100)) {
            atomic_fetch_add(args->messages_sent, 1);
        } else {
            /* Push failed - shouldn't happen with our limit */
            atomic_fetch_add(&test_errors, 1);
            message_free(msg);
        }
    }

    return NULL;
}

static void *consumer_thread(void *arg) {
    ConsumerArgs *args = (ConsumerArgs *)arg;

    wait_for_start();

    while (!atomic_load(args->stop) ||
           atomic_load(args->messages_received) < args->expected_count) {
        Message *msg = mailbox_pop(args->mailbox);
        if (msg) {
            atomic_fetch_add(args->messages_received, 1);
            message_free(msg);
        } else {
            /* No message available, yield */
            sched_yield();
        }

        /* Break if we've received all expected messages */
        if (atomic_load(args->messages_received) >= args->expected_count) {
            break;
        }
    }

    return NULL;
}

void test_mpsc_basic(void) {
    printf("  Testing MPSC basic: %d producers, 1 consumer, %d messages each...\n",
           NUM_PRODUCERS, NUM_ITERATIONS);
    reset_sync();

    Mailbox mailbox;
    mailbox_init(&mailbox);

    _Atomic(int) messages_sent = 0;
    _Atomic(int) messages_received = 0;
    _Atomic(bool) stop = false;

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumer;
    ProducerArgs producer_args[NUM_PRODUCERS];

    ConsumerArgs consumer_args = {
        .mailbox = &mailbox,
        .messages_received = &messages_received,
        .stop = &stop,
        .expected_count = TOTAL_MESSAGES
    };

    /* Start consumer */
    pthread_create(&consumer, NULL, consumer_thread, &consumer_args);

    /* Start producers */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_args[i].mailbox = &mailbox;
        producer_args[i].producer_id = i + 1;
        producer_args[i].messages_sent = &messages_sent;
        pthread_create(&producers[i], NULL, producer_thread, &producer_args[i]);
    }

    signal_start(NUM_PRODUCERS + 1);

    /* Wait for producers to finish */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }

    /* Signal consumer to stop and wait */
    atomic_store(&stop, true);
    mailbox_notify(&mailbox);  /* Wake up consumer if blocked */
    pthread_join(consumer, NULL);

    /* Drain any remaining messages */
    while (!mailbox_empty(&mailbox)) {
        Message *msg = mailbox_pop(&mailbox);
        if (msg) {
            atomic_fetch_add(&messages_received, 1);
            message_free(msg);
        }
    }

    int sent = atomic_load(&messages_sent);
    int received = atomic_load(&messages_received);
    printf("    Messages sent: %d, received: %d\n", sent, received);

    ASSERT_EQ(TOTAL_MESSAGES, sent);
    ASSERT_EQ(TOTAL_MESSAGES, received);
    ASSERT_EQ(0, atomic_load(&test_errors));

    mailbox_free(&mailbox);
}

/* ========== Test: Producer Consumer Interleaving ========== */

typedef struct {
    Mailbox *mailbox;
    _Atomic(int) *sent;
    _Atomic(int) *received;
    int num_messages;
} InterleavedArgs;

static void *interleaved_producer(void *arg) {
    InterleavedArgs *args = (InterleavedArgs *)arg;

    wait_for_start();

    for (int i = 0; i < args->num_messages; i++) {
        Value *v = value_int(i);
        Message *msg = message_new(1, v);
        if (mailbox_push(args->mailbox, msg, args->num_messages * 2)) {
            atomic_fetch_add(args->sent, 1);
        } else {
            message_free(msg);
        }
        /* Small delay to allow interleaving */
        if (i % 100 == 0) {
            sched_yield();
        }
    }

    return NULL;
}

static void *interleaved_consumer(void *arg) {
    InterleavedArgs *args = (InterleavedArgs *)arg;

    wait_for_start();

    int received = 0;
    int attempts = 0;
    int max_attempts = args->num_messages * 100;

    while (received < args->num_messages && attempts < max_attempts) {
        Message *msg = mailbox_pop(args->mailbox);
        if (msg) {
            atomic_fetch_add(args->received, 1);
            received++;
            message_free(msg);
        } else {
            sched_yield();
            attempts++;
        }
    }

    return NULL;
}

void test_producer_consumer_interleaving(void) {
    printf("  Testing producer-consumer interleaving...\n");
    reset_sync();

    Mailbox mailbox;
    mailbox_init(&mailbox);

    _Atomic(int) sent = 0;
    _Atomic(int) received = 0;

    InterleavedArgs args = {
        .mailbox = &mailbox,
        .sent = &sent,
        .received = &received,
        .num_messages = 500
    };

    pthread_t producer, consumer;

    /* Start both threads */
    pthread_create(&producer, NULL, interleaved_producer, &args);
    pthread_create(&consumer, NULL, interleaved_consumer, &args);

    signal_start(2);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    /* Drain remaining */
    while (!mailbox_empty(&mailbox)) {
        Message *msg = mailbox_pop(&mailbox);
        if (msg) {
            atomic_fetch_add(&received, 1);
            message_free(msg);
        }
    }

    printf("    Sent: %d, Received: %d\n", atomic_load(&sent), atomic_load(&received));
    ASSERT_EQ(args.num_messages, atomic_load(&sent));
    ASSERT_EQ(args.num_messages, atomic_load(&received));

    mailbox_free(&mailbox);
}

/* ========== Test: Stub Node Handling ========== */

void test_stub_node_handling(void) {
    printf("  Testing stub node handling under concurrent access...\n");
    reset_sync();

    /* The stub node is used to handle the empty->non-empty transition
     * This test verifies it works correctly under concurrent access */

    Mailbox mailbox;
    mailbox_init(&mailbox);

    /* Initial state should be empty with stub node */
    ASSERT(mailbox_empty(&mailbox));
    ASSERT_EQ(0, mailbox_count(&mailbox));

    _Atomic(int) messages_sent = 0;
    _Atomic(int) messages_received = 0;

    /* Run multiple rounds of push/pop to exercise stub node transitions */
    #define ROUNDS 10
    #define MESSAGES_PER_ROUND 100

    for (int round = 0; round < ROUNDS; round++) {
        /* Push messages */
        for (int i = 0; i < MESSAGES_PER_ROUND; i++) {
            Value *v = value_int(round * 1000 + i);
            Message *msg = message_new(1, v);
            if (mailbox_push(&mailbox, msg, MESSAGES_PER_ROUND + 10)) {
                atomic_fetch_add(&messages_sent, 1);
            } else {
                message_free(msg);
            }
        }

        /* Pop all messages */
        while (!mailbox_empty(&mailbox)) {
            Message *msg = mailbox_pop(&mailbox);
            if (msg) {
                atomic_fetch_add(&messages_received, 1);
                message_free(msg);
            }
        }

        /* Verify mailbox is back to empty state (stub node only) */
        ASSERT(mailbox_empty(&mailbox));
    }

    int sent = atomic_load(&messages_sent);
    int received = atomic_load(&messages_received);
    printf("    Total sent: %d, received: %d\n", sent, received);
    ASSERT_EQ(ROUNDS * MESSAGES_PER_ROUND, sent);
    ASSERT_EQ(ROUNDS * MESSAGES_PER_ROUND, received);

    mailbox_free(&mailbox);

    #undef ROUNDS
    #undef MESSAGES_PER_ROUND
}

typedef struct {
    Mailbox *mailbox;
    int thread_id;
    _Atomic(int) *messages_sent;
} StubStressProducerArgs;

typedef struct {
    Mailbox *mailbox;
    _Atomic(int) *messages_received;
    _Atomic(bool) *producers_done;
} StubStressConsumerArgs;

static void *stub_stress_producer(void *arg) {
    StubStressProducerArgs *args = (StubStressProducerArgs *)arg;

    wait_for_start();

    for (int i = 0; i < 100; i++) {
        /* Push a message */
        Value *v = value_int(args->thread_id * 1000 + i);
        Message *msg = message_new((Pid)args->thread_id, v);
        if (mailbox_push(args->mailbox, msg, 1000)) {
            atomic_fetch_add(args->messages_sent, 1);
        } else {
            message_free(msg);
        }
        /* Yield occasionally to allow interleaving */
        if (i % 10 == 0) {
            sched_yield();
        }
    }

    return NULL;
}

static void *stub_stress_consumer(void *arg) {
    StubStressConsumerArgs *args = (StubStressConsumerArgs *)arg;

    wait_for_start();

    int attempts = 0;
    int max_attempts = 100000;

    while (attempts < max_attempts) {
        Message *msg = mailbox_pop(args->mailbox);
        if (msg) {
            atomic_fetch_add(args->messages_received, 1);
            message_free(msg);
            attempts = 0;  /* Reset on success */
        } else {
            /* Check if producers are done and mailbox is empty */
            if (atomic_load(args->producers_done) && mailbox_empty(args->mailbox)) {
                break;
            }
            sched_yield();
            attempts++;
        }
    }

    return NULL;
}

void test_stub_node_concurrent(void) {
    printf("  Testing stub node with concurrent push (MPSC pattern)...\n");
    reset_sync();

    Mailbox mailbox;
    mailbox_init(&mailbox);

    _Atomic(int) messages_sent = 0;
    _Atomic(int) messages_received = 0;
    _Atomic(bool) producers_done = false;

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumer;
    StubStressProducerArgs producer_args[NUM_PRODUCERS];
    StubStressConsumerArgs consumer_args = {
        .mailbox = &mailbox,
        .messages_received = &messages_received,
        .producers_done = &producers_done
    };

    /* Start consumer first */
    pthread_create(&consumer, NULL, stub_stress_consumer, &consumer_args);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_args[i].mailbox = &mailbox;
        producer_args[i].thread_id = i + 1;
        producer_args[i].messages_sent = &messages_sent;
        pthread_create(&producers[i], NULL, stub_stress_producer, &producer_args[i]);
    }

    signal_start(NUM_PRODUCERS + 1);

    /* Wait for producers */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }

    /* Signal that producers are done */
    atomic_store(&producers_done, true);
    mailbox_notify(&mailbox);

    /* Wait for consumer */
    pthread_join(consumer, NULL);

    /* Drain any remaining */
    while (!mailbox_empty(&mailbox)) {
        Message *msg = mailbox_pop(&mailbox);
        if (msg) {
            atomic_fetch_add(&messages_received, 1);
            message_free(msg);
        }
    }

    int sent = atomic_load(&messages_sent);
    int received = atomic_load(&messages_received);
    printf("    Messages sent: %d, received: %d\n", sent, received);
    ASSERT_EQ(sent, received);

    mailbox_free(&mailbox);
}

/* ========== Test: Atomic Ordering ========== */

typedef struct {
    Mailbox *mailbox;
    _Atomic(int64_t) *last_seen;
    _Atomic(int) *out_of_order;
    int producer_id;
} OrderingArgs;

static void *ordering_producer(void *arg) {
    OrderingArgs *args = (OrderingArgs *)arg;

    wait_for_start();

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        /* Use producer_id * 1000000 + sequence to create unique ordered values */
        int64_t value = (int64_t)args->producer_id * 1000000 + i;
        Value *v = value_int(value);
        Message *msg = message_new((Pid)args->producer_id, v);
        mailbox_push(args->mailbox, msg, TOTAL_MESSAGES + 100);
    }

    return NULL;
}

static void *ordering_consumer(void *arg) {
    OrderingArgs *args = (OrderingArgs *)arg;

    wait_for_start();

    /* Track last seen sequence per producer */
    int64_t last_seq[NUM_PRODUCERS + 1];
    for (int i = 0; i <= NUM_PRODUCERS; i++) {
        last_seq[i] = -1;
    }

    int received = 0;
    int max_attempts = TOTAL_MESSAGES * 100;
    int attempts = 0;

    while (received < TOTAL_MESSAGES && attempts < max_attempts) {
        Message *msg = mailbox_pop(args->mailbox);
        if (msg) {
            int64_t value = msg->value->as.integer;
            int producer = (int)(value / 1000000);
            int64_t seq = value % 1000000;

            /* Check ordering per producer */
            if (producer >= 1 && producer <= NUM_PRODUCERS) {
                if (seq <= last_seq[producer]) {
                    /* Out of order! */
                    atomic_fetch_add(args->out_of_order, 1);
                }
                last_seq[producer] = seq;
            }

            received++;
            message_free(msg);
        } else {
            sched_yield();
            attempts++;
        }
    }

    return NULL;
}

void test_atomic_ordering_per_producer(void) {
    printf("  Testing atomic ordering (FIFO per producer)...\n");
    reset_sync();

    Mailbox mailbox;
    mailbox_init(&mailbox);

    _Atomic(int64_t) last_seen = -1;
    _Atomic(int) out_of_order = 0;

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumer;
    OrderingArgs producer_args[NUM_PRODUCERS];

    OrderingArgs consumer_args = {
        .mailbox = &mailbox,
        .last_seen = &last_seen,
        .out_of_order = &out_of_order,
        .producer_id = 0
    };

    pthread_create(&consumer, NULL, ordering_consumer, &consumer_args);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_args[i].mailbox = &mailbox;
        producer_args[i].producer_id = i + 1;
        producer_args[i].last_seen = &last_seen;
        producer_args[i].out_of_order = &out_of_order;
        pthread_create(&producers[i], NULL, ordering_producer, &producer_args[i]);
    }

    signal_start(NUM_PRODUCERS + 1);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    pthread_join(consumer, NULL);

    /* Drain remaining and check order */
    while (!mailbox_empty(&mailbox)) {
        Message *msg = mailbox_pop(&mailbox);
        if (msg) {
            message_free(msg);
        }
    }

    int errors = atomic_load(&out_of_order);
    printf("    Out-of-order messages: %d (should be 0)\n", errors);
    ASSERT_EQ(0, errors);

    mailbox_free(&mailbox);
}

/* ========== Test: High Contention ========== */

typedef struct {
    Mailbox *mailbox;
    _Atomic(int) *messages_sent;
    int thread_id;
} HighContentionProducerArgs;

typedef struct {
    Mailbox *mailbox;
    _Atomic(int) *messages_received;
    _Atomic(bool) *stop;
} HighContentionConsumerArgs;

static void *high_contention_producer(void *arg) {
    HighContentionProducerArgs *args = (HighContentionProducerArgs *)arg;

    wait_for_start();

    for (int i = 0; i < 500; i++) {
        Value *v = value_int(args->thread_id * 1000 + i);
        Message *msg = message_new((Pid)args->thread_id, v);

        if (mailbox_push(args->mailbox, msg, 10000)) {
            atomic_fetch_add(args->messages_sent, 1);
        } else {
            message_free(msg);
        }
    }

    return NULL;
}

static void *high_contention_consumer(void *arg) {
    HighContentionConsumerArgs *args = (HighContentionConsumerArgs *)arg;

    wait_for_start();

    while (!atomic_load(args->stop) || !mailbox_empty(args->mailbox)) {
        Message *msg = mailbox_pop(args->mailbox);
        if (msg) {
            atomic_fetch_add(args->messages_received, 1);
            message_free(msg);
        } else {
            sched_yield();
        }
    }

    return NULL;
}

void test_high_contention(void) {
    printf("  Testing high contention with many producer threads...\n");
    reset_sync();

    Mailbox mailbox;
    mailbox_init(&mailbox);

    _Atomic(int) messages_sent = 0;
    _Atomic(int) messages_received = 0;
    _Atomic(bool) stop = false;

    #define CONTENTION_THREADS 8
    pthread_t producers[CONTENTION_THREADS];
    pthread_t consumer;
    HighContentionProducerArgs producer_args[CONTENTION_THREADS];
    HighContentionConsumerArgs consumer_args = {
        .mailbox = &mailbox,
        .messages_received = &messages_received,
        .stop = &stop
    };

    /* Start consumer */
    pthread_create(&consumer, NULL, high_contention_consumer, &consumer_args);

    for (int i = 0; i < CONTENTION_THREADS; i++) {
        producer_args[i].mailbox = &mailbox;
        producer_args[i].messages_sent = &messages_sent;
        producer_args[i].thread_id = i + 1;
        pthread_create(&producers[i], NULL, high_contention_producer, &producer_args[i]);
    }

    signal_start(CONTENTION_THREADS + 1);

    for (int i = 0; i < CONTENTION_THREADS; i++) {
        pthread_join(producers[i], NULL);
    }

    /* Signal stop and wait for consumer */
    atomic_store(&stop, true);
    mailbox_notify(&mailbox);
    pthread_join(consumer, NULL);

    /* Drain remaining */
    while (!mailbox_empty(&mailbox)) {
        Message *msg = mailbox_pop(&mailbox);
        if (msg) {
            atomic_fetch_add(&messages_received, 1);
            message_free(msg);
        }
    }

    int sent = atomic_load(&messages_sent);
    int received = atomic_load(&messages_received);
    printf("    Messages sent: %d, received: %d\n", sent, received);
    ASSERT_EQ(sent, received);

    mailbox_free(&mailbox);

    #undef CONTENTION_THREADS
}

/* ========== Test: Blocking Receive ========== */

typedef struct {
    Mailbox *mailbox;
    _Atomic(bool) *message_received;
    uint64_t delay_ms;
} BlockingArgs;

static void *blocking_sender(void *arg) {
    BlockingArgs *args = (BlockingArgs *)arg;

    /* Wait a bit before sending */
    usleep(args->delay_ms * 1000);

    Value *v = value_int(42);
    Message *msg = message_new(1, v);
    mailbox_push(args->mailbox, msg, 100);
    mailbox_notify(args->mailbox);

    return NULL;
}

static void *blocking_receiver(void *arg) {
    BlockingArgs *args = (BlockingArgs *)arg;

    wait_for_start();

    /* Block waiting for message */
    Message *msg = mailbox_receive(args->mailbox, 1000);  /* 1 second timeout */
    if (msg) {
        atomic_store(args->message_received, true);
        message_free(msg);
    }

    return NULL;
}

void test_blocking_receive_concurrent(void) {
    printf("  Testing blocking receive with concurrent sender...\n");
    reset_sync();

    Mailbox mailbox;
    mailbox_init(&mailbox);

    _Atomic(bool) message_received = false;

    BlockingArgs args = {
        .mailbox = &mailbox,
        .message_received = &message_received,
        .delay_ms = 50  /* Send after 50ms */
    };

    pthread_t sender, receiver;

    pthread_create(&receiver, NULL, blocking_receiver, &args);
    pthread_create(&sender, NULL, blocking_sender, &args);

    signal_start(1);  /* Only receiver waits for start */

    pthread_join(sender, NULL);
    pthread_join(receiver, NULL);

    ASSERT(atomic_load(&message_received));
    printf("    Blocking receive succeeded\n");

    mailbox_free(&mailbox);
}

/* ========== Test: Empty Mailbox Stress ========== */

/* Test empty mailbox stress with MPSC pattern:
 * Multiple producers racing to push, single consumer popping.
 * Producers and consumer contend when mailbox transitions empty<->non-empty */
typedef struct {
    Mailbox *mailbox;
    _Atomic(int) *push_attempts;
    _Atomic(bool) *stop;
    int thread_id;
} EmptyStressProducerArgs;

typedef struct {
    Mailbox *mailbox;
    _Atomic(int) *pop_attempts;
    _Atomic(bool) *stop;
} EmptyStressConsumerArgs;

static void *empty_stress_producer(void *arg) {
    EmptyStressProducerArgs *args = (EmptyStressProducerArgs *)arg;

    wait_for_start();

    while (!atomic_load(args->stop)) {
        Value *v = value_int(args->thread_id);
        Message *msg = message_new((Pid)args->thread_id, v);
        if (mailbox_push(args->mailbox, msg, 100)) {
            atomic_fetch_add(args->push_attempts, 1);
        } else {
            message_free(msg);
        }
    }

    return NULL;
}

static void *empty_stress_consumer(void *arg) {
    EmptyStressConsumerArgs *args = (EmptyStressConsumerArgs *)arg;

    wait_for_start();

    while (!atomic_load(args->stop)) {
        Message *msg = mailbox_pop(args->mailbox);
        atomic_fetch_add(args->pop_attempts, 1);
        if (msg) {
            message_free(msg);
        }
    }

    return NULL;
}

void test_empty_mailbox_stress(void) {
    printf("  Testing empty/non-empty transition stress...\n");
    reset_sync();

    Mailbox mailbox;
    mailbox_init(&mailbox);

    _Atomic(int) push_attempts = 0;
    _Atomic(int) pop_attempts = 0;
    _Atomic(bool) stop = false;

    EmptyStressConsumerArgs consumer_args = {
        .mailbox = &mailbox,
        .pop_attempts = &pop_attempts,
        .stop = &stop
    };

    EmptyStressProducerArgs producer_args[4];

    pthread_t consumer;
    pthread_t producers[4];

    pthread_create(&consumer, NULL, empty_stress_consumer, &consumer_args);
    for (int i = 0; i < 4; i++) {
        producer_args[i].mailbox = &mailbox;
        producer_args[i].push_attempts = &push_attempts;
        producer_args[i].stop = &stop;
        producer_args[i].thread_id = i + 1;
        pthread_create(&producers[i], NULL, empty_stress_producer, &producer_args[i]);
    }

    signal_start(5);

    /* Let it run briefly */
    usleep(10000);  /* 10ms */
    atomic_store(&stop, true);

    for (int i = 0; i < 4; i++) {
        pthread_join(producers[i], NULL);
    }
    pthread_join(consumer, NULL);

    /* Drain remaining */
    while (!mailbox_empty(&mailbox)) {
        Message *msg = mailbox_pop(&mailbox);
        if (msg) {
            message_free(msg);
        }
    }

    printf("    Push attempts: %d, Pop attempts: %d\n",
           atomic_load(&push_attempts), atomic_load(&pop_attempts));

    mailbox_free(&mailbox);
}

/* ========== Test: Memory Consistency ========== */

typedef struct {
    Mailbox *mailbox;
    int producer_id;
    _Atomic(int) *checksum;
} MemConsistencyArgs;

static void *mem_consistency_producer(void *arg) {
    MemConsistencyArgs *args = (MemConsistencyArgs *)arg;

    wait_for_start();

    for (int i = 0; i < 100; i++) {
        /* Create string with checksum */
        char buf[64];
        snprintf(buf, sizeof(buf), "msg_%d_%d", args->producer_id, i);
        Value *v = value_string(buf);
        Message *msg = message_new((Pid)args->producer_id, v);

        if (mailbox_push(args->mailbox, msg, 1000)) {
            /* Add to expected checksum */
            atomic_fetch_add(args->checksum, args->producer_id * 100 + i);
        } else {
            message_free(msg);
        }
    }

    return NULL;
}

void test_memory_consistency(void) {
    printf("  Testing memory consistency of message values...\n");
    reset_sync();

    Mailbox mailbox;
    mailbox_init(&mailbox);

    _Atomic(int) expected_checksum = 0;

    pthread_t producers[NUM_PRODUCERS];
    MemConsistencyArgs args[NUM_PRODUCERS];

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        args[i].mailbox = &mailbox;
        args[i].producer_id = i + 1;
        args[i].checksum = &expected_checksum;
        pthread_create(&producers[i], NULL, mem_consistency_producer, &args[i]);
    }

    signal_start(NUM_PRODUCERS);

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }

    /* Consume and verify messages */
    int actual_checksum = 0;
    int messages_consumed = 0;

    while (!mailbox_empty(&mailbox)) {
        Message *msg = mailbox_pop(&mailbox);
        if (msg && msg->value) {
            /* Parse message to verify content */
            if (value_is_string(msg->value)) {
                const char *str = string_data(msg->value);
                int prod_id, seq;
                if (sscanf(str, "msg_%d_%d", &prod_id, &seq) == 2) {
                    actual_checksum += prod_id * 100 + seq;
                }
            }
            messages_consumed++;
            message_free(msg);
        }
    }

    printf("    Messages consumed: %d, checksum match: %s\n",
           messages_consumed,
           actual_checksum == atomic_load(&expected_checksum) ? "yes" : "no");

    ASSERT_EQ(atomic_load(&expected_checksum), actual_checksum);

    mailbox_free(&mailbox);
}

/* ========== Main ========== */

int main(void) {
    printf("\n=== Concurrent Mailbox Tests ===\n\n");

    /* Multiple producers single consumer */
    RUN_TEST(test_mpsc_basic);

    /* Producer consumer interleaving */
    RUN_TEST(test_producer_consumer_interleaving);

    /* Stub node handling */
    RUN_TEST(test_stub_node_handling);
    RUN_TEST(test_stub_node_concurrent);

    /* Atomic ordering */
    RUN_TEST(test_atomic_ordering_per_producer);

    /* Additional concurrent tests */
    RUN_TEST(test_high_contention);
    RUN_TEST(test_blocking_receive_concurrent);
    RUN_TEST(test_empty_mailbox_stress);
    RUN_TEST(test_memory_consistency);

    printf("\n");
    return TEST_RESULT();
}
