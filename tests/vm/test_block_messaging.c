/*
 * Agim - Block Messaging Tests
 *
 * P1.1.4.5: Tests for block message passing operations.
 * - block_send to live block
 * - block_send to dead block
 * - block_send COW for arrays
 * - block_send COW for maps
 * - block_send copies closures
 * - block_receive pops message
 * - block_receive empty returns NULL
 * - block_has_messages
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "vm/value.h"

/*
 * Test: Block starts with empty mailbox
 */
void test_messaging_initially_empty(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_messages(block));

    block_free(block);
}

/*
 * Test: block_send to live block succeeds
 */
void test_send_to_live_block(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_int(42);
    bool sent = block_send(target, 2, msg);
    ASSERT(sent);
    ASSERT(block_has_messages(target));

    value_release(msg);
    block_free(target);
}

/*
 * Test: block_send to dead block fails
 */
void test_send_to_dead_block(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    block_exit(target, 0);  /* Kill the block */
    ASSERT(!block_is_alive(target));

    Value *msg = value_int(42);
    bool sent = block_send(target, 2, msg);
    ASSERT(!sent);

    value_release(msg);
    block_free(target);
}

/*
 * Test: block_send with NULL target fails
 */
void test_send_null_target(void) {
    Value *msg = value_int(42);
    bool sent = block_send(NULL, 2, msg);
    ASSERT(!sent);

    value_release(msg);
}

/*
 * Test: block_send with NULL value sends nil
 */
void test_send_null_value(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    bool sent = block_send(target, 2, NULL);
    ASSERT(sent);
    ASSERT(block_has_messages(target));

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_NIL);

    message_free(received);
    block_free(target);
}

/*
 * Test: block_send integer value
 */
void test_send_integer(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_int(12345);
    bool sent = block_send(target, 2, msg);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_INT);
    ASSERT_EQ(12345, received->value->as.integer);
    ASSERT_EQ(2, received->sender);

    message_free(received);
    value_release(msg);
    block_free(target);
}

/*
 * Test: block_send float value
 */
void test_send_float(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_float(3.14159);
    bool sent = block_send(target, 2, msg);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_FLOAT);

    message_free(received);
    value_release(msg);
    block_free(target);
}

/*
 * Test: block_send boolean value
 */
void test_send_boolean(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_bool(true);
    bool sent = block_send(target, 2, msg);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_BOOL);
    ASSERT(received->value->as.boolean);

    message_free(received);
    value_release(msg);
    block_free(target);
}

/*
 * Test: block_send string value
 */
void test_send_string(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_string("hello world");
    bool sent = block_send(target, 2, msg);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_STRING);

    message_free(received);
    value_release(msg);
    block_free(target);
}

/*
 * Test: block_send PID value
 */
void test_send_pid(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_pid(99);
    bool sent = block_send(target, 2, msg);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_PID);
    ASSERT_EQ(99, received->value->as.pid);

    message_free(received);
    value_release(msg);
    block_free(target);
}

/*
 * Test: block_send array with COW
 */
void test_send_array_cow(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *arr = value_array();
    array_push(arr, value_int(1));
    array_push(arr, value_int(2));
    array_push(arr, value_int(3));

    bool sent = block_send(target, 2, arr);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_ARRAY);
    ASSERT_EQ(3, received->value->as.array->length);

    message_free(received);
    value_release(arr);
    block_free(target);
}

/*
 * Test: block_send map with COW
 */
void test_send_map_cow(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *map = value_map();
    map_set(map, "key1", value_int(100));
    map_set(map, "key2", value_string("value"));

    bool sent = block_send(target, 2, map);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_MAP);

    message_free(received);
    value_release(map);
    block_free(target);
}

/*
 * Test: block_receive returns NULL for empty mailbox
 */
void test_receive_empty_returns_null(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    Message *msg = block_receive(block);
    ASSERT(msg == NULL);

    block_free(block);
}

/*
 * Test: block_receive with NULL block returns NULL
 */
void test_receive_null_block(void) {
    Message *msg = block_receive(NULL);
    ASSERT(msg == NULL);
}

/*
 * Test: block_receive pops message (FIFO)
 */
void test_receive_fifo_order(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    /* Send multiple messages */
    Value *msg1 = value_int(1);
    Value *msg2 = value_int(2);
    Value *msg3 = value_int(3);

    block_send(target, 10, msg1);
    block_send(target, 20, msg2);
    block_send(target, 30, msg3);

    /* Receive in FIFO order */
    Message *r1 = block_receive(target);
    ASSERT(r1 != NULL);
    ASSERT_EQ(1, r1->value->as.integer);
    ASSERT_EQ(10, r1->sender);

    Message *r2 = block_receive(target);
    ASSERT(r2 != NULL);
    ASSERT_EQ(2, r2->value->as.integer);
    ASSERT_EQ(20, r2->sender);

    Message *r3 = block_receive(target);
    ASSERT(r3 != NULL);
    ASSERT_EQ(3, r3->value->as.integer);
    ASSERT_EQ(30, r3->sender);

    /* Queue should now be empty */
    ASSERT(!block_has_messages(target));

    message_free(r1);
    message_free(r2);
    message_free(r3);
    value_release(msg1);
    value_release(msg2);
    value_release(msg3);
    block_free(target);
}

/*
 * Test: block_has_messages returns true when has messages
 */
void test_has_messages_true(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    ASSERT(!block_has_messages(target));

    Value *msg = value_int(42);
    block_send(target, 2, msg);

    ASSERT(block_has_messages(target));

    value_release(msg);
    block_free(target);
}

/*
 * Test: block_has_messages returns false after receive
 */
void test_has_messages_false_after_receive(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_int(42);
    block_send(target, 2, msg);
    ASSERT(block_has_messages(target));

    Message *received = block_receive(target);
    ASSERT(!block_has_messages(target));

    message_free(received);
    value_release(msg);
    block_free(target);
}

/*
 * Test: block_has_messages with NULL block returns false
 */
void test_has_messages_null_block(void) {
    ASSERT(!block_has_messages(NULL));
}

/*
 * Test: Messages received counter increments
 */
void test_messages_received_counter(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    ASSERT_EQ(0, atomic_load(&target->counters.messages_received));

    Value *msg = value_int(42);
    block_send(target, 2, msg);
    ASSERT_EQ(1, atomic_load(&target->counters.messages_received));

    block_send(target, 2, msg);
    ASSERT_EQ(2, atomic_load(&target->counters.messages_received));

    block_send(target, 2, msg);
    ASSERT_EQ(3, atomic_load(&target->counters.messages_received));

    value_release(msg);
    block_free(target);
}

/*
 * Test: Message sender is preserved
 */
void test_message_sender_preserved(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_int(42);
    block_send(target, 12345, msg);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT_EQ(12345, received->sender);

    message_free(received);
    value_release(msg);
    block_free(target);
}

/*
 * Test: Multiple messages from different senders
 */
void test_multiple_senders(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg1 = value_int(1);
    Value *msg2 = value_int(2);
    Value *msg3 = value_int(3);

    block_send(target, 100, msg1);
    block_send(target, 200, msg2);
    block_send(target, 300, msg3);

    Message *r1 = block_receive(target);
    Message *r2 = block_receive(target);
    Message *r3 = block_receive(target);

    ASSERT_EQ(100, r1->sender);
    ASSERT_EQ(200, r2->sender);
    ASSERT_EQ(300, r3->sender);

    message_free(r1);
    message_free(r2);
    message_free(r3);
    value_release(msg1);
    value_release(msg2);
    value_release(msg3);
    block_free(target);
}

/*
 * Test: Mailbox respects max_mailbox_size limit
 */
void test_mailbox_size_limit(void) {
    BlockLimits limits = block_limits_default();
    limits.max_mailbox_size = 3;  /* Very small limit */

    Block *target = block_new(1, "target", &limits);
    ASSERT(target != NULL);

    Value *msg = value_int(42);

    /* Fill up to limit */
    ASSERT(block_send(target, 2, msg));
    ASSERT(block_send(target, 2, msg));
    ASSERT(block_send(target, 2, msg));

    /* Fourth message should fail */
    bool sent = block_send(target, 2, msg);
    ASSERT(!sent);

    value_release(msg);
    block_free(target);
}

/*
 * Test: Sending nil value
 */
void test_send_nil(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg = value_nil();
    bool sent = block_send(target, 2, msg);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_NIL);

    message_free(received);
    value_release(msg);
    block_free(target);
}

/*
 * Test: Large number of messages
 */
void test_many_messages(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    /* Send 50 messages */
    for (int i = 0; i < 50; i++) {
        Value *msg = value_int(i);
        bool sent = block_send(target, 2, msg);
        ASSERT(sent);
        value_release(msg);
    }

    ASSERT_EQ(50, atomic_load(&target->counters.messages_received));

    /* Receive all messages */
    for (int i = 0; i < 50; i++) {
        Message *received = block_receive(target);
        ASSERT(received != NULL);
        ASSERT_EQ(i, received->value->as.integer);
        message_free(received);
    }

    ASSERT(!block_has_messages(target));

    block_free(target);
}

/*
 * Test: Nested array in message
 */
void test_send_nested_array(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *outer = value_array();
    Value *inner = value_array();
    array_push(inner, value_int(1));
    array_push(inner, value_int(2));
    array_push(outer, inner);
    array_push(outer, value_int(3));

    bool sent = block_send(target, 2, outer);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_ARRAY);
    ASSERT_EQ(2, received->value->as.array->length);

    message_free(received);
    value_release(outer);
    block_free(target);
}

/*
 * Test: Message with nested map
 */
void test_send_nested_map(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *outer = value_map();
    Value *inner = value_map();
    map_set(inner, "a", value_int(1));
    map_set(outer, "nested", inner);
    map_set(outer, "top", value_int(2));

    bool sent = block_send(target, 2, outer);
    ASSERT(sent);

    Message *received = block_receive(target);
    ASSERT(received != NULL);
    ASSERT(received->value->type == VAL_MAP);

    message_free(received);
    value_release(outer);
    block_free(target);
}

/*
 * Test: Send and receive interleaved
 */
void test_send_receive_interleaved(void) {
    Block *target = block_new(1, "target", NULL);
    ASSERT(target != NULL);

    Value *msg1 = value_int(1);
    Value *msg2 = value_int(2);

    block_send(target, 2, msg1);

    Message *r1 = block_receive(target);
    ASSERT_EQ(1, r1->value->as.integer);

    block_send(target, 2, msg2);

    Message *r2 = block_receive(target);
    ASSERT_EQ(2, r2->value->as.integer);

    message_free(r1);
    message_free(r2);
    value_release(msg1);
    value_release(msg2);
    block_free(target);
}

int main(void) {
    printf("Running block messaging tests...\n");

    printf("\nInitial state tests:\n");
    RUN_TEST(test_messaging_initially_empty);

    printf("\nblock_send tests:\n");
    RUN_TEST(test_send_to_live_block);
    RUN_TEST(test_send_to_dead_block);
    RUN_TEST(test_send_null_target);
    RUN_TEST(test_send_null_value);

    printf("\nValue type tests:\n");
    RUN_TEST(test_send_integer);
    RUN_TEST(test_send_float);
    RUN_TEST(test_send_boolean);
    RUN_TEST(test_send_string);
    RUN_TEST(test_send_pid);
    RUN_TEST(test_send_nil);

    printf("\nCOW tests:\n");
    RUN_TEST(test_send_array_cow);
    RUN_TEST(test_send_map_cow);

    printf("\nblock_receive tests:\n");
    RUN_TEST(test_receive_empty_returns_null);
    RUN_TEST(test_receive_null_block);
    RUN_TEST(test_receive_fifo_order);

    printf("\nblock_has_messages tests:\n");
    RUN_TEST(test_has_messages_true);
    RUN_TEST(test_has_messages_false_after_receive);
    RUN_TEST(test_has_messages_null_block);

    printf("\nCounter tests:\n");
    RUN_TEST(test_messages_received_counter);

    printf("\nSender tests:\n");
    RUN_TEST(test_message_sender_preserved);
    RUN_TEST(test_multiple_senders);

    printf("\nLimits tests:\n");
    RUN_TEST(test_mailbox_size_limit);

    printf("\nComplex message tests:\n");
    RUN_TEST(test_send_nested_array);
    RUN_TEST(test_send_nested_map);

    printf("\nScale tests:\n");
    RUN_TEST(test_many_messages);
    RUN_TEST(test_send_receive_interleaved);

    return TEST_RESULT();
}
