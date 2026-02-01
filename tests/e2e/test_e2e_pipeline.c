/*
 * Agim - End-to-End Pipeline Tests
 *
 * Tests data processing pipeline patterns using actor-based message passing.
 * Validates multi-stage processing, fan-out/fan-in, and backpressure.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "vm/value.h"

#include <string.h>

/* Pipeline Pattern Tests */

void test_pipeline_two_stage(void) {
    /* Pipeline: source -> stage1 -> sink */
    Block *source = block_new(1, "source", NULL);
    Block *stage1 = block_new(2, "stage1", NULL);
    Block *sink = block_new(3, "sink", NULL);

    block_grant(source, CAP_SEND);
    block_grant(stage1, CAP_SEND | CAP_RECEIVE);
    block_grant(sink, CAP_RECEIVE);

    /* Source sends data to stage1 */
    Value *data = value_int(10);
    ASSERT(block_send(stage1, source->pid, data));

    /* Stage1 receives, processes (double it), sends to sink */
    Message *msg = block_receive(stage1);
    ASSERT(msg != NULL);
    int processed = value_to_int(msg->value) * 2;
    message_free(msg);

    Value *result = value_int(processed);
    ASSERT(block_send(sink, stage1->pid, result));

    /* Sink receives final result */
    Message *final = block_receive(sink);
    ASSERT(final != NULL);
    ASSERT_EQ(20, value_to_int(final->value));
    message_free(final);

    block_free(source);
    block_free(stage1);
    block_free(sink);
}

void test_pipeline_three_stage(void) {
    /* Pipeline: source -> add10 -> multiply2 -> sink */
    Block *source = block_new(1, "source", NULL);
    Block *add10 = block_new(2, "add10", NULL);
    Block *multiply2 = block_new(3, "multiply2", NULL);
    Block *sink = block_new(4, "sink", NULL);

    block_grant(source, CAP_SEND);
    block_grant(add10, CAP_SEND | CAP_RECEIVE);
    block_grant(multiply2, CAP_SEND | CAP_RECEIVE);
    block_grant(sink, CAP_RECEIVE);

    /* Source: 5 */
    Value *data = value_int(5);
    ASSERT(block_send(add10, source->pid, data));

    /* add10: 5 + 10 = 15 */
    Message *msg1 = block_receive(add10);
    int v1 = value_to_int(msg1->value) + 10;
    message_free(msg1);
    ASSERT(block_send(multiply2, add10->pid, value_int(v1)));

    /* multiply2: 15 * 2 = 30 */
    Message *msg2 = block_receive(multiply2);
    int v2 = value_to_int(msg2->value) * 2;
    message_free(msg2);
    ASSERT(block_send(sink, multiply2->pid, value_int(v2)));

    /* Sink receives 30 */
    Message *final = block_receive(sink);
    ASSERT(final != NULL);
    ASSERT_EQ(30, value_to_int(final->value));
    message_free(final);

    block_free(source);
    block_free(add10);
    block_free(multiply2);
    block_free(sink);
}

void test_pipeline_batch_processing(void) {
    Block *source = block_new(1, "source", NULL);
    Block *processor = block_new(2, "processor", NULL);
    Block *sink = block_new(3, "sink", NULL);

    block_grant(source, CAP_SEND);
    block_grant(processor, CAP_SEND | CAP_RECEIVE);
    block_grant(sink, CAP_RECEIVE);

    /* Send batch of items */
    for (int i = 0; i < 5; i++) {
        Value *item = value_int(i);
        ASSERT(block_send(processor, source->pid, item));
    }

    /* Process batch */
    for (int i = 0; i < 5; i++) {
        Message *msg = block_receive(processor);
        int processed = value_to_int(msg->value) * 10;
        message_free(msg);
        ASSERT(block_send(sink, processor->pid, value_int(processed)));
    }

    /* Verify results: 0, 10, 20, 30, 40 */
    for (int i = 0; i < 5; i++) {
        Message *result = block_receive(sink);
        ASSERT(result != NULL);
        ASSERT_EQ(i * 10, value_to_int(result->value));
        message_free(result);
    }

    block_free(source);
    block_free(processor);
    block_free(sink);
}

void test_pipeline_fan_out(void) {
    /* Fan-out: source -> [worker1, worker2, worker3] */
    Block *source = block_new(1, "source", NULL);
    Block *worker1 = block_new(10, "worker1", NULL);
    Block *worker2 = block_new(11, "worker2", NULL);
    Block *worker3 = block_new(12, "worker3", NULL);

    block_grant(source, CAP_SEND);
    block_grant(worker1, CAP_RECEIVE);
    block_grant(worker2, CAP_RECEIVE);
    block_grant(worker3, CAP_RECEIVE);

    Block *workers[] = {worker1, worker2, worker3};

    /* Distribute work round-robin */
    for (int i = 0; i < 9; i++) {
        int worker_idx = i % 3;
        Value *work = value_int(i);
        ASSERT(block_send(workers[worker_idx], source->pid, work));
    }

    /* Each worker should have 3 items */
    for (int w = 0; w < 3; w++) {
        int count = 0;
        while (block_has_messages(workers[w])) {
            Message *msg = block_receive(workers[w]);
            count++;
            message_free(msg);
        }
        ASSERT_EQ(3, count);
    }

    block_free(source);
    block_free(worker1);
    block_free(worker2);
    block_free(worker3);
}

void test_pipeline_fan_in(void) {
    /* Fan-in: [producer1, producer2] -> aggregator */
    Block *producer1 = block_new(1, "producer1", NULL);
    Block *producer2 = block_new(2, "producer2", NULL);
    Block *aggregator = block_new(10, "aggregator", NULL);

    block_grant(producer1, CAP_SEND);
    block_grant(producer2, CAP_SEND);
    block_grant(aggregator, CAP_RECEIVE);

    /* Both producers send to aggregator */
    Value *v1 = value_int(100);
    Value *v2 = value_int(200);
    ASSERT(block_send(aggregator, producer1->pid, v1));
    ASSERT(block_send(aggregator, producer2->pid, v2));

    /* Aggregator receives from both */
    int sum = 0;
    while (block_has_messages(aggregator)) {
        Message *msg = block_receive(aggregator);
        sum += value_to_int(msg->value);
        message_free(msg);
    }

    ASSERT_EQ(300, sum);

    block_free(producer1);
    block_free(producer2);
    block_free(aggregator);
}

void test_pipeline_filter(void) {
    /* Filter pipeline: source -> filter(even only) -> sink */
    Block *source = block_new(1, "source", NULL);
    Block *filter = block_new(2, "filter", NULL);
    Block *sink = block_new(3, "sink", NULL);

    block_grant(source, CAP_SEND);
    block_grant(filter, CAP_SEND | CAP_RECEIVE);
    block_grant(sink, CAP_RECEIVE);

    /* Send 0-9 */
    for (int i = 0; i < 10; i++) {
        Value *item = value_int(i);
        ASSERT(block_send(filter, source->pid, item));
    }

    /* Filter passes only even numbers */
    for (int i = 0; i < 10; i++) {
        Message *msg = block_receive(filter);
        int val = value_to_int(msg->value);
        if (val % 2 == 0) {
            ASSERT(block_send(sink, filter->pid, value_int(val)));
        }
        message_free(msg);
    }

    /* Sink receives 0, 2, 4, 6, 8 */
    int expected[] = {0, 2, 4, 6, 8};
    for (int i = 0; i < 5; i++) {
        Message *result = block_receive(sink);
        ASSERT(result != NULL);
        ASSERT_EQ(expected[i], value_to_int(result->value));
        message_free(result);
    }

    ASSERT(!block_has_messages(sink));

    block_free(source);
    block_free(filter);
    block_free(sink);
}

void test_pipeline_transform_string(void) {
    Block *source = block_new(1, "source", NULL);
    Block *transform = block_new(2, "transform", NULL);
    Block *sink = block_new(3, "sink", NULL);

    block_grant(source, CAP_SEND);
    block_grant(transform, CAP_SEND | CAP_RECEIVE);
    block_grant(sink, CAP_RECEIVE);

    /* Source sends string */
    Value *data = value_string("hello");
    ASSERT(block_send(transform, source->pid, data));

    /* Transform receives string (in real code, would transform) */
    Message *msg = block_receive(transform);
    ASSERT(msg != NULL);
    (void)value_to_string(msg->value);  /* Would use for transformation */

    /* Create transformed result (simulated uppercase) */
    Value *result = value_string("HELLO");
    ASSERT(block_send(sink, transform->pid, result));
    message_free(msg);

    /* Sink receives transformed string */
    Message *final = block_receive(sink);
    ASSERT(final != NULL);
    ASSERT_STR_EQ("HELLO", value_to_string(final->value));
    message_free(final);

    block_free(source);
    block_free(transform);
    block_free(sink);
}

void test_pipeline_passthrough(void) {
    /* Simple passthrough: data goes through unchanged */
    Block *stage1 = block_new(1, "stage1", NULL);
    Block *stage2 = block_new(2, "stage2", NULL);
    Block *stage3 = block_new(3, "stage3", NULL);

    block_grant(stage1, CAP_SEND | CAP_RECEIVE);
    block_grant(stage2, CAP_SEND | CAP_RECEIVE);
    block_grant(stage3, CAP_RECEIVE);

    /* Push through pipeline */
    Value *data = value_int(42);
    Message *msg = message_new(0, data);
    mailbox_push(&stage1->mailbox, msg, 100);

    Message *m1 = block_receive(stage1);
    Value *v1 = value_int(value_to_int(m1->value));
    Message *forward1 = message_new(stage1->pid, v1);
    mailbox_push(&stage2->mailbox, forward1, 100);
    message_free(m1);

    Message *m2 = block_receive(stage2);
    Value *v2 = value_int(value_to_int(m2->value));
    Message *forward2 = message_new(stage2->pid, v2);
    mailbox_push(&stage3->mailbox, forward2, 100);
    message_free(m2);

    Message *final = block_receive(stage3);
    ASSERT(final != NULL);
    ASSERT_EQ(42, value_to_int(final->value));
    message_free(final);

    block_free(stage1);
    block_free(stage2);
    block_free(stage3);
}

void test_pipeline_error_handling(void) {
    Block *source = block_new(1, "source", NULL);
    Block *processor = block_new(2, "processor", NULL);
    Block *error_handler = block_new(3, "error_handler", NULL);
    Block *sink = block_new(4, "sink", NULL);

    block_grant(source, CAP_SEND);
    block_grant(processor, CAP_SEND | CAP_RECEIVE);
    block_grant(error_handler, CAP_RECEIVE);
    block_grant(sink, CAP_RECEIVE);

    /* Send data that will "fail" */
    Value *bad_data = value_int(-1);
    ASSERT(block_send(processor, source->pid, bad_data));

    /* Processor routes errors to error_handler */
    Message *msg = block_receive(processor);
    int val = value_to_int(msg->value);

    if (val < 0) {
        /* Route to error handler */
        ASSERT(block_send(error_handler, processor->pid, value_int(val)));
    } else {
        /* Route to sink */
        ASSERT(block_send(sink, processor->pid, value_int(val)));
    }
    message_free(msg);

    /* Error handler receives the error */
    Message *error = block_receive(error_handler);
    ASSERT(error != NULL);
    ASSERT_EQ(-1, value_to_int(error->value));
    message_free(error);

    /* Sink should be empty */
    ASSERT(!block_has_messages(sink));

    block_free(source);
    block_free(processor);
    block_free(error_handler);
    block_free(sink);
}

/* Main */

int main(void) {
    printf("Running pipeline end-to-end tests...\n\n");

    printf("Pipeline Tests:\n");
    RUN_TEST(test_pipeline_two_stage);
    RUN_TEST(test_pipeline_three_stage);
    RUN_TEST(test_pipeline_batch_processing);
    RUN_TEST(test_pipeline_fan_out);
    RUN_TEST(test_pipeline_fan_in);
    RUN_TEST(test_pipeline_filter);
    RUN_TEST(test_pipeline_transform_string);
    RUN_TEST(test_pipeline_passthrough);
    RUN_TEST(test_pipeline_error_handling);

    return TEST_RESULT();
}
