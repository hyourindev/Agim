/*
 * Agim - End-to-End Worker Pool Tests
 *
 * Tests worker pool patterns using actor-based concurrency.
 * Validates work distribution and result aggregation.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "runtime/scheduler.h"
#include "vm/value.h"

/* Worker Pool Pattern Tests */

void test_worker_pool_basic(void) {
    /* Create pool manager and workers */
    Block *manager = block_new(1, "manager", NULL);
    Block *worker1 = block_new(2, "worker1", NULL);
    Block *worker2 = block_new(3, "worker2", NULL);
    Block *worker3 = block_new(4, "worker3", NULL);

    block_grant(manager, CAP_SEND | CAP_RECEIVE);
    block_grant(worker1, CAP_SEND | CAP_RECEIVE);
    block_grant(worker2, CAP_SEND | CAP_RECEIVE);
    block_grant(worker3, CAP_SEND | CAP_RECEIVE);

    ASSERT(manager != NULL);
    ASSERT(worker1 != NULL);
    ASSERT(worker2 != NULL);
    ASSERT(worker3 != NULL);

    block_free(manager);
    block_free(worker1);
    block_free(worker2);
    block_free(worker3);
}

void test_worker_pool_work_distribution(void) {
    Block *manager = block_new(1, "manager", NULL);
    const int POOL_SIZE = 3;
    Block *workers[3];
    workers[0] = block_new(10, "worker0", NULL);
    workers[1] = block_new(11, "worker1", NULL);
    workers[2] = block_new(12, "worker2", NULL);

    block_grant(manager, CAP_SEND);
    for (int i = 0; i < POOL_SIZE; i++) {
        block_grant(workers[i], CAP_RECEIVE);
    }

    /* Distribute work round-robin */
    for (int i = 0; i < 9; i++) {
        int worker_idx = i % POOL_SIZE;
        Value *work = value_int(i);
        ASSERT(block_send(workers[worker_idx], manager->pid, work));
    }

    /* Each worker should have 3 items */
    for (int i = 0; i < POOL_SIZE; i++) {
        int count = 0;
        while (block_has_messages(workers[i])) {
            Message *msg = block_receive(workers[i]);
            message_free(msg);
            count++;
        }
        ASSERT_EQ(3, count);
    }

    block_free(manager);
    for (int i = 0; i < POOL_SIZE; i++) {
        block_free(workers[i]);
    }
}

void test_worker_pool_result_aggregation(void) {
    Block *aggregator = block_new(1, "aggregator", NULL);
    Block *worker1 = block_new(10, "worker1", NULL);
    Block *worker2 = block_new(11, "worker2", NULL);
    Block *worker3 = block_new(12, "worker3", NULL);

    block_grant(aggregator, CAP_RECEIVE);
    block_grant(worker1, CAP_SEND);
    block_grant(worker2, CAP_SEND);
    block_grant(worker3, CAP_SEND);

    /* Workers send results */
    Value *r1 = value_int(10);
    Value *r2 = value_int(20);
    Value *r3 = value_int(30);
    ASSERT(block_send(aggregator, worker1->pid, r1));
    ASSERT(block_send(aggregator, worker2->pid, r2));
    ASSERT(block_send(aggregator, worker3->pid, r3));

    /* Aggregator collects results */
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        Message *msg = block_receive(aggregator);
        ASSERT(msg != NULL);
        sum += value_to_int(msg->value);
        message_free(msg);
    }

    ASSERT_EQ(60, sum);

    block_free(aggregator);
    block_free(worker1);
    block_free(worker2);
    block_free(worker3);
}

void test_worker_pool_fifo_ordering(void) {
    Block *worker = block_new(1, "worker", NULL);
    block_grant(worker, CAP_RECEIVE);

    Mailbox *mb = &worker->mailbox;

    /* Send work in order */
    Value *first_val = value_string("first");
    Message *first_msg = message_new(0, first_val);
    mailbox_push(mb, first_msg, 100);

    Value *second_val = value_string("second");
    Message *second_msg = message_new(0, second_val);
    mailbox_push(mb, second_msg, 100);

    /* Messages received in FIFO order */
    Message *first = block_receive(worker);
    ASSERT(first != NULL);
    ASSERT_STR_EQ("first", value_to_string(first->value));
    message_free(first);

    Message *second = block_receive(worker);
    ASSERT(second != NULL);
    ASSERT_STR_EQ("second", value_to_string(second->value));
    message_free(second);

    block_free(worker);
}

void test_worker_pool_bounded_queue(void) {
    Block *worker = block_new(1, "worker", NULL);
    block_grant(worker, CAP_RECEIVE);

    Mailbox *mb = &worker->mailbox;
    mailbox_set_limits(mb, 3, 1024);
    mailbox_set_overflow_policy(mb, OVERFLOW_DROP_NEW);

    /* Fill queue */
    for (int i = 0; i < 3; i++) {
        Value *val = value_int(i);
        Message *msg = message_new(0, val);
        SendResult result = mailbox_push_ex(mb, msg);
        ASSERT_EQ(SEND_OK, result);
    }

    ASSERT_EQ(3, mailbox_count(mb));

    /* Fourth should be rejected */
    Value *extra = value_int(999);
    Message *extra_msg = message_new(0, extra);
    SendResult result = mailbox_push_ex(mb, extra_msg);
    ASSERT_EQ(SEND_FULL, result);
    message_free(extra_msg);

    /* Drain */
    while (block_has_messages(worker)) {
        Message *msg = block_receive(worker);
        message_free(msg);
    }

    block_free(worker);
}

void test_worker_pool_idle_detection(void) {
    Block *worker = block_new(1, "worker", NULL);
    block_grant(worker, CAP_RECEIVE);

    /* Worker is idle when mailbox empty */
    ASSERT(!block_has_messages(worker));

    /* Send work */
    Value *work = value_int(1);
    Message *msg = message_new(0, work);
    mailbox_push(&worker->mailbox, msg, 100);

    /* Worker is not idle */
    ASSERT(block_has_messages(worker));

    /* Process work */
    Message *received = block_receive(worker);
    message_free(received);

    /* Worker is idle again */
    ASSERT(!block_has_messages(worker));

    block_free(worker);
}

void test_worker_pool_multiple_tasks(void) {
    Block *worker = block_new(1, "worker", NULL);
    block_grant(worker, CAP_RECEIVE);

    /* Send batch of tasks */
    for (int i = 0; i < 10; i++) {
        Value *task = value_int(i);
        Message *msg = message_new(0, task);
        mailbox_push(&worker->mailbox, msg, 100);
    }

    /* Process all in order */
    for (int i = 0; i < 10; i++) {
        Message *msg = block_receive(worker);
        ASSERT(msg != NULL);
        ASSERT_EQ(i, value_to_int(msg->value));
        message_free(msg);
    }

    ASSERT(!block_has_messages(worker));

    block_free(worker);
}

void test_worker_pool_result_tracking(void) {
    Block *manager = block_new(1, "manager", NULL);
    Block *worker = block_new(2, "worker", NULL);

    block_grant(manager, CAP_SEND | CAP_RECEIVE);
    block_grant(worker, CAP_SEND | CAP_RECEIVE);

    /* Manager sends task */
    Value *task = value_int(42);
    ASSERT(block_send(worker, manager->pid, task));

    /* Worker receives and processes */
    Message *received = block_receive(worker);
    ASSERT(received != NULL);
    int result = value_to_int(received->value) * 2; /* Process: double it */
    message_free(received);

    /* Worker sends result back */
    Value *result_val = value_int(result);
    ASSERT(block_send(manager, worker->pid, result_val));

    /* Manager receives result */
    Message *result_msg = block_receive(manager);
    ASSERT(result_msg != NULL);
    ASSERT_EQ(84, value_to_int(result_msg->value));
    message_free(result_msg);

    block_free(manager);
    block_free(worker);
}

/* Main */

int main(void) {
    printf("Running worker pool end-to-end tests...\n\n");

    printf("Worker Pool Tests:\n");
    RUN_TEST(test_worker_pool_basic);
    RUN_TEST(test_worker_pool_work_distribution);
    RUN_TEST(test_worker_pool_result_aggregation);
    RUN_TEST(test_worker_pool_fifo_ordering);
    RUN_TEST(test_worker_pool_bounded_queue);
    RUN_TEST(test_worker_pool_idle_detection);
    RUN_TEST(test_worker_pool_multiple_tasks);
    RUN_TEST(test_worker_pool_result_tracking);

    return TEST_RESULT();
}
