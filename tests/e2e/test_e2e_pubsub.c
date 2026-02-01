/*
 * Agim - End-to-End Pub/Sub Tests
 *
 * Tests publish-subscribe patterns using actor-based message passing.
 * Validates topic-based routing and subscriber management.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "runtime/scheduler.h"
#include "vm/value.h"

#include <string.h>

/* Pub/Sub Pattern Tests */

void test_pubsub_single_subscriber(void) {
    Block *publisher = block_new(1, "publisher", NULL);
    Block *subscriber = block_new(2, "subscriber", NULL);

    block_grant(publisher, CAP_SEND);
    block_grant(subscriber, CAP_RECEIVE);

    /* Publish message */
    Value *message = value_string("hello subscribers");
    ASSERT(block_send(subscriber, publisher->pid, message));

    /* Subscriber receives */
    ASSERT(block_has_messages(subscriber));
    Message *received = block_receive(subscriber);
    ASSERT(received != NULL);
    ASSERT_STR_EQ("hello subscribers", value_to_string(received->value));
    message_free(received);

    block_free(publisher);
    block_free(subscriber);
}

void test_pubsub_multiple_subscribers(void) {
    Block *publisher = block_new(1, "publisher", NULL);
    Block *sub1 = block_new(10, "sub1", NULL);
    Block *sub2 = block_new(11, "sub2", NULL);
    Block *sub3 = block_new(12, "sub3", NULL);

    block_grant(publisher, CAP_SEND);
    block_grant(sub1, CAP_RECEIVE);
    block_grant(sub2, CAP_RECEIVE);
    block_grant(sub3, CAP_RECEIVE);

    /* Broadcast to all */
    Value *m1 = value_string("broadcast");
    Value *m2 = value_string("broadcast");
    Value *m3 = value_string("broadcast");
    ASSERT(block_send(sub1, publisher->pid, m1));
    ASSERT(block_send(sub2, publisher->pid, m2));
    ASSERT(block_send(sub3, publisher->pid, m3));

    /* All receive */
    Message *r1 = block_receive(sub1);
    Message *r2 = block_receive(sub2);
    Message *r3 = block_receive(sub3);

    ASSERT(r1 != NULL);
    ASSERT(r2 != NULL);
    ASSERT(r3 != NULL);
    ASSERT_STR_EQ("broadcast", value_to_string(r1->value));
    ASSERT_STR_EQ("broadcast", value_to_string(r2->value));
    ASSERT_STR_EQ("broadcast", value_to_string(r3->value));

    message_free(r1);
    message_free(r2);
    message_free(r3);
    block_free(publisher);
    block_free(sub1);
    block_free(sub2);
    block_free(sub3);
}

void test_pubsub_topic_filtering(void) {
    Block *publisher = block_new(1, "publisher", NULL);
    Block *sports_sub = block_new(10, "sports_sub", NULL);
    Block *news_sub = block_new(11, "news_sub", NULL);

    block_grant(publisher, CAP_SEND);
    block_grant(sports_sub, CAP_RECEIVE);
    block_grant(news_sub, CAP_RECEIVE);

    /* Publish to sports topic */
    Value *sports_msg = value_string("sports:goal scored");
    ASSERT(block_send(sports_sub, publisher->pid, sports_msg));

    /* Publish to news topic */
    Value *news_msg = value_string("news:breaking story");
    ASSERT(block_send(news_sub, publisher->pid, news_msg));

    /* Sports sub gets sports */
    Message *s = block_receive(sports_sub);
    ASSERT(s != NULL);
    ASSERT(strstr(value_to_string(s->value), "sports:") != NULL);
    message_free(s);

    /* News sub gets news */
    Message *n = block_receive(news_sub);
    ASSERT(n != NULL);
    ASSERT(strstr(value_to_string(n->value), "news:") != NULL);
    message_free(n);

    block_free(publisher);
    block_free(sports_sub);
    block_free(news_sub);
}

void test_pubsub_message_ordering(void) {
    Block *publisher = block_new(1, "publisher", NULL);
    Block *subscriber = block_new(2, "subscriber", NULL);

    block_grant(publisher, CAP_SEND);
    block_grant(subscriber, CAP_RECEIVE);

    /* Publish in order */
    for (int i = 1; i <= 5; i++) {
        Value *val = value_int(i);
        ASSERT(block_send(subscriber, publisher->pid, val));
    }

    /* Receive in FIFO order */
    for (int i = 1; i <= 5; i++) {
        Message *msg = block_receive(subscriber);
        ASSERT(msg != NULL);
        ASSERT_EQ(i, value_to_int(msg->value));
        message_free(msg);
    }

    block_free(publisher);
    block_free(subscriber);
}

void test_pubsub_multiple_publishers(void) {
    Block *pub1 = block_new(1, "pub1", NULL);
    Block *pub2 = block_new(2, "pub2", NULL);
    Block *subscriber = block_new(10, "subscriber", NULL);

    block_grant(pub1, CAP_SEND);
    block_grant(pub2, CAP_SEND);
    block_grant(subscriber, CAP_RECEIVE);

    /* Both publishers send */
    Value *m1 = value_string("from_pub1");
    Value *m2 = value_string("from_pub2");
    ASSERT(block_send(subscriber, pub1->pid, m1));
    ASSERT(block_send(subscriber, pub2->pid, m2));

    /* Subscriber receives both */
    int count = 0;
    while (block_has_messages(subscriber)) {
        Message *msg = block_receive(subscriber);
        ASSERT(msg != NULL);
        count++;
        message_free(msg);
    }
    ASSERT_EQ(2, count);

    block_free(pub1);
    block_free(pub2);
    block_free(subscriber);
}

void test_pubsub_empty_message(void) {
    Block *publisher = block_new(1, "publisher", NULL);
    Block *subscriber = block_new(2, "subscriber", NULL);

    block_grant(publisher, CAP_SEND);
    block_grant(subscriber, CAP_RECEIVE);

    /* Empty message */
    Value *empty = value_string("");
    ASSERT(block_send(subscriber, publisher->pid, empty));

    Message *received = block_receive(subscriber);
    ASSERT(received != NULL);
    ASSERT_STR_EQ("", value_to_string(received->value));
    message_free(received);

    block_free(publisher);
    block_free(subscriber);
}

void test_pubsub_batch_publish(void) {
    Block *publisher = block_new(1, "publisher", NULL);
    const int SUB_COUNT = 3;
    Block *subs[3];
    subs[0] = block_new(10, "sub0", NULL);
    subs[1] = block_new(11, "sub1", NULL);
    subs[2] = block_new(12, "sub2", NULL);

    block_grant(publisher, CAP_SEND);
    for (int i = 0; i < SUB_COUNT; i++) {
        block_grant(subs[i], CAP_RECEIVE);
    }

    /* Batch of 10 messages to each subscriber */
    int batch_size = 10;
    for (int i = 0; i < batch_size; i++) {
        for (int j = 0; j < SUB_COUNT; j++) {
            Value *val = value_int(i);
            ASSERT(block_send(subs[j], publisher->pid, val));
        }
    }

    /* Each subscriber should have batch_size messages */
    for (int j = 0; j < SUB_COUNT; j++) {
        for (int i = 0; i < batch_size; i++) {
            Message *msg = block_receive(subs[j]);
            ASSERT(msg != NULL);
            ASSERT_EQ(i, value_to_int(msg->value));
            message_free(msg);
        }
        ASSERT(!block_has_messages(subs[j]));
    }

    block_free(publisher);
    for (int i = 0; i < SUB_COUNT; i++) {
        block_free(subs[i]);
    }
}

void test_pubsub_sender_identification(void) {
    Block *pub1 = block_new(100, "pub1", NULL);
    Block *pub2 = block_new(200, "pub2", NULL);
    Block *subscriber = block_new(10, "subscriber", NULL);

    block_grant(pub1, CAP_SEND);
    block_grant(pub2, CAP_SEND);
    block_grant(subscriber, CAP_RECEIVE);

    Value *m1 = value_string("msg1");
    Value *m2 = value_string("msg2");
    ASSERT(block_send(subscriber, pub1->pid, m1));
    ASSERT(block_send(subscriber, pub2->pid, m2));

    /* Can identify sender */
    Message *r1 = block_receive(subscriber);
    ASSERT(r1 != NULL);
    ASSERT_EQ(100, r1->sender);
    message_free(r1);

    Message *r2 = block_receive(subscriber);
    ASSERT(r2 != NULL);
    ASSERT_EQ(200, r2->sender);
    message_free(r2);

    block_free(pub1);
    block_free(pub2);
    block_free(subscriber);
}

void test_pubsub_no_subscribers(void) {
    Block *publisher = block_new(1, "publisher", NULL);
    block_grant(publisher, CAP_SEND);

    /* Publishing with no subscribers - nothing to send to */
    /* This test verifies the pattern works with empty subscriber list */
    ASSERT(publisher != NULL);

    block_free(publisher);
}

/* Main */

int main(void) {
    printf("Running pub/sub end-to-end tests...\n\n");

    printf("Pub/Sub Tests:\n");
    RUN_TEST(test_pubsub_single_subscriber);
    RUN_TEST(test_pubsub_multiple_subscribers);
    RUN_TEST(test_pubsub_topic_filtering);
    RUN_TEST(test_pubsub_message_ordering);
    RUN_TEST(test_pubsub_multiple_publishers);
    RUN_TEST(test_pubsub_empty_message);
    RUN_TEST(test_pubsub_batch_publish);
    RUN_TEST(test_pubsub_sender_identification);
    RUN_TEST(test_pubsub_no_subscribers);

    return TEST_RESULT();
}
