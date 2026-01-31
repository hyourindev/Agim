/*
 * Agim - End-to-End Message Passing Tests
 *
 * Tests the complete message passing infrastructure including mailbox
 * operations, flow control, overflow policies, and inter-process
 * communication patterns. Validates Erlang-like message semantics.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "runtime/scheduler.h"
#include "vm/value.h"

#include <pthread.h>
#include <unistd.h>

/* Test 1: Basic mailbox initialization */
void test_mailbox_init(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);

	ASSERT(mailbox_empty(&mailbox));
	ASSERT_EQ(0, mailbox_count(&mailbox));

	mailbox_free(&mailbox);
}

/* Test 2: Single message send/receive */
void test_single_message(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);

	Value *val = value_int(42);
	Message *msg = message_new(1, val);
	ASSERT(msg != NULL);

	/* Push message */
	ASSERT(mailbox_push(&mailbox, msg, 100));
	ASSERT(!mailbox_empty(&mailbox));
	ASSERT_EQ(1, mailbox_count(&mailbox));

	/* Pop message */
	Message *received = mailbox_pop(&mailbox);
	ASSERT(received != NULL);
	ASSERT_EQ(1, received->sender);
	ASSERT(value_is_int(received->value));
	ASSERT_EQ(42, value_to_int(received->value));

	/* Mailbox should be empty */
	ASSERT(mailbox_empty(&mailbox));
	ASSERT_EQ(0, mailbox_count(&mailbox));

	message_free(received);
	mailbox_free(&mailbox);
}

/* Test 3: FIFO ordering */
void test_fifo_ordering(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);

	/* Send messages 1, 2, 3 */
	for (int i = 1; i <= 3; i++) {
		Value *val = value_int(i);
		Message *msg = message_new((Pid)i, val);
		mailbox_push(&mailbox, msg, 100);
	}

	ASSERT_EQ(3, mailbox_count(&mailbox));

	/* Receive in order: 1, 2, 3 */
	for (int i = 1; i <= 3; i++) {
		Message *msg = mailbox_pop(&mailbox);
		ASSERT(msg != NULL);
		ASSERT_EQ(i, value_to_int(msg->value));
		message_free(msg);
	}

	ASSERT(mailbox_empty(&mailbox));
	mailbox_free(&mailbox);
}

/* Test 4: Mailbox size limits */
void test_mailbox_limits(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);
	mailbox_set_limits(&mailbox, 3, 1024);  /* max 3 messages */
	mailbox_set_overflow_policy(&mailbox, OVERFLOW_DROP_NEW);

	/* Send 3 messages - all should succeed */
	for (int i = 0; i < 3; i++) {
		Value *val = value_int(i);
		Message *msg = message_new(1, val);
		SendResult result = mailbox_push_ex(&mailbox, msg);
		ASSERT_EQ(SEND_OK, result);
	}

	ASSERT_EQ(3, mailbox_count(&mailbox));

	/* Fourth message should be rejected (DROP_NEW policy) */
	Value *val = value_int(99);
	Message *msg = message_new(1, val);
	SendResult result = mailbox_push_ex(&mailbox, msg);
	ASSERT_EQ(SEND_FULL, result);

	/* Count should still be 3 */
	ASSERT_EQ(3, mailbox_count(&mailbox));

	/* Verify original messages preserved */
	for (int i = 0; i < 3; i++) {
		Message *m = mailbox_pop(&mailbox);
		ASSERT_EQ(i, value_to_int(m->value));
		message_free(m);
	}

	message_free(msg);
	mailbox_free(&mailbox);
}

/* Test 5: DROP_OLD overflow policy */
void test_overflow_drop_old(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);
	mailbox_set_limits(&mailbox, 3, 1024);
	mailbox_set_overflow_policy(&mailbox, OVERFLOW_DROP_OLD);

	/* Fill mailbox: 1, 2, 3 */
	for (int i = 1; i <= 3; i++) {
		Value *val = value_int(i);
		Message *msg = message_new(1, val);
		mailbox_push_ex(&mailbox, msg);
	}

	/* Send new message - should drop oldest (1) */
	Value *val = value_int(4);
	Message *msg = message_new(1, val);
	SendResult result = mailbox_push_ex(&mailbox, msg);
	ASSERT_EQ(SEND_OK, result);

	ASSERT_EQ(3, mailbox_count(&mailbox));

	/* Should receive 2, 3, 4 (oldest was dropped) */
	for (int expected = 2; expected <= 4; expected++) {
		Message *m = mailbox_pop(&mailbox);
		ASSERT(m != NULL);
		ASSERT_EQ(expected, value_to_int(m->value));
		message_free(m);
	}

	mailbox_free(&mailbox);
}

/* Test 6: Block send/receive integration */
void test_block_messaging(void)
{
	Block *sender = block_new(1, "sender", NULL);
	Block *receiver = block_new(2, "receiver", NULL);

	/* Grant messaging capabilities */
	block_grant(sender, CAP_SEND);
	block_grant(receiver, CAP_RECEIVE);

	/* Sender sends to receiver */
	Value *val = value_string("hello");
	ASSERT(block_send(receiver, sender->pid, val));

	/* Receiver should have message */
	ASSERT(block_has_messages(receiver));

	Message *msg = block_receive(receiver);
	ASSERT(msg != NULL);
	ASSERT_EQ(1, msg->sender);
	ASSERT(value_is_string(msg->value));
	ASSERT_STR_EQ("hello", value_to_string(msg->value));

	/* No more messages */
	ASSERT(!block_has_messages(receiver));

	message_free(msg);
	block_free(sender);
	block_free(receiver);
}

/* Test 7: Multiple senders to single receiver */
void test_multiple_senders(void)
{
	Block *receiver = block_new(1, "receiver", NULL);
	Block *sender1 = block_new(10, "sender1", NULL);
	Block *sender2 = block_new(20, "sender2", NULL);
	Block *sender3 = block_new(30, "sender3", NULL);

	/* All senders send messages */
	block_send(receiver, sender1->pid, value_int(100));
	block_send(receiver, sender2->pid, value_int(200));
	block_send(receiver, sender3->pid, value_int(300));

	ASSERT_EQ(3, mailbox_count(&receiver->mailbox));

	/* Receive all - FIFO order */
	Message *m1 = block_receive(receiver);
	Message *m2 = block_receive(receiver);
	Message *m3 = block_receive(receiver);

	ASSERT_EQ(10, m1->sender);
	ASSERT_EQ(100, value_to_int(m1->value));

	ASSERT_EQ(20, m2->sender);
	ASSERT_EQ(200, value_to_int(m2->value));

	ASSERT_EQ(30, m3->sender);
	ASSERT_EQ(300, value_to_int(m3->value));

	message_free(m1);
	message_free(m2);
	message_free(m3);
	block_free(receiver);
	block_free(sender1);
	block_free(sender2);
	block_free(sender3);
}

/* Test 8: Message with complex value types */
void test_complex_message_types(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);

	/* Array message */
	Value *arr = value_array();
	array_push(arr, value_int(1));
	array_push(arr, value_int(2));
	array_push(arr, value_int(3));
	Message *msg1 = message_new(1, arr);
	mailbox_push(&mailbox, msg1, 100);

	/* Map message */
	Value *map = value_map();
	map_set(map, "key", value_string("value"));
	Message *msg2 = message_new(2, map);
	mailbox_push(&mailbox, msg2, 100);

	/* Receive and verify array */
	Message *recv1 = mailbox_pop(&mailbox);
	ASSERT(value_is_array(recv1->value));
	ASSERT_EQ(3, array_length(recv1->value));

	/* Receive and verify map */
	Message *recv2 = mailbox_pop(&mailbox);
	ASSERT(value_is_map(recv2->value));
	Value *val = map_get(recv2->value, "key");
	ASSERT(val != NULL);
	ASSERT_STR_EQ("value", value_to_string(val));

	message_free(recv1);
	message_free(recv2);
	mailbox_free(&mailbox);
}

/* Test 9: Receive from empty mailbox */
void test_receive_empty(void)
{
	Block *block = block_new(1, "empty_recv", NULL);

	ASSERT(!block_has_messages(block));

	Message *msg = block_receive(block);
	ASSERT(msg == NULL);

	block_free(block);
}

/* Test 10: High-volume messaging */
void test_high_volume_messaging(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);

	const int NUM_MESSAGES = 1000;

	/* Send many messages */
	for (int i = 0; i < NUM_MESSAGES; i++) {
		Value *val = value_int(i);
		Message *msg = message_new(1, val);
		ASSERT(mailbox_push(&mailbox, msg, NUM_MESSAGES + 10));
	}

	ASSERT_EQ(NUM_MESSAGES, mailbox_count(&mailbox));

	/* Receive all and verify order */
	for (int i = 0; i < NUM_MESSAGES; i++) {
		Message *msg = mailbox_pop(&mailbox);
		ASSERT(msg != NULL);
		ASSERT_EQ(i, value_to_int(msg->value));
		message_free(msg);
	}

	ASSERT(mailbox_empty(&mailbox));
	mailbox_free(&mailbox);
}

/* Test 11: Mailbox statistics */
void test_mailbox_statistics(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);
	mailbox_set_limits(&mailbox, 5, 1024);
	mailbox_set_overflow_policy(&mailbox, OVERFLOW_DROP_NEW);

	/* Send 7 messages (2 will be dropped) */
	for (int i = 0; i < 7; i++) {
		Value *val = value_int(i);
		Message *msg = message_new(1, val);
		mailbox_push_ex(&mailbox, msg);
		if (i >= 5) {
			message_free(msg);  /* Dropped messages need cleanup */
		}
	}

	/* Check dropped count */
	ASSERT_EQ(2, mailbox.dropped_count);
	ASSERT_EQ(5, mailbox_count(&mailbox));

	/* Drain mailbox */
	while (!mailbox_empty(&mailbox)) {
		Message *m = mailbox_pop(&mailbox);
		message_free(m);
	}

	mailbox_free(&mailbox);
}

/* Test 12: Message value ownership transfer */
void test_message_ownership(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);

	/* Create value and wrap in message */
	Value *val = value_string("test ownership");
	Message *msg = message_new(1, val);

	/* Message takes ownership of value */
	mailbox_push(&mailbox, msg, 10);

	/* Pop and verify value still valid */
	Message *recv = mailbox_pop(&mailbox);
	ASSERT(recv != NULL);
	ASSERT(value_is_string(recv->value));
	ASSERT_STR_EQ("test ownership", value_to_string(recv->value));

	/* Cleanup */
	message_free(recv);
	mailbox_free(&mailbox);
}

/*
 * Test 13: Concurrent mailbox access
 *
 * Tests thread-safety of the lock-free MPSC queue by having multiple
 * producer threads send to a single consumer.
 */
typedef struct {
	Mailbox *mailbox;
	int start_id;
	int count;
} ProducerArgs;

static void *producer_thread(void *arg)
{
	ProducerArgs *args = (ProducerArgs *)arg;

	for (int i = 0; i < args->count; i++) {
		Value *val = value_int(args->start_id + i);
		Message *msg = message_new((Pid)args->start_id, val);
		mailbox_push(args->mailbox, msg, 10000);
	}

	return NULL;
}

void test_concurrent_messaging(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);

	const int NUM_PRODUCERS = 4;
	const int MSGS_PER_PRODUCER = 100;
	pthread_t threads[NUM_PRODUCERS];
	ProducerArgs args[NUM_PRODUCERS];

	/* Start producer threads */
	for (int i = 0; i < NUM_PRODUCERS; i++) {
		args[i].mailbox = &mailbox;
		args[i].start_id = i * 1000;
		args[i].count = MSGS_PER_PRODUCER;
		pthread_create(&threads[i], NULL, producer_thread, &args[i]);
	}

	/* Wait for all producers */
	for (int i = 0; i < NUM_PRODUCERS; i++) {
		pthread_join(threads[i], NULL);
	}

	/* Should have all messages */
	ASSERT_EQ(NUM_PRODUCERS * MSGS_PER_PRODUCER, mailbox_count(&mailbox));

	/* Consume all */
	int received = 0;
	while (!mailbox_empty(&mailbox)) {
		Message *msg = mailbox_pop(&mailbox);
		if (msg) {
			received++;
			message_free(msg);
		}
	}

	ASSERT_EQ(NUM_PRODUCERS * MSGS_PER_PRODUCER, received);
	mailbox_free(&mailbox);
}

/* Test 14: Self-messaging */
void test_self_messaging(void)
{
	Block *block = block_new(1, "self_sender", NULL);
	block_grant(block, CAP_SEND | CAP_RECEIVE);

	/* Send to self */
	ASSERT(block_send(block, block->pid, value_int(42)));

	/* Should receive own message */
	ASSERT(block_has_messages(block));
	Message *msg = block_receive(block);
	ASSERT(msg != NULL);
	ASSERT_EQ(1, msg->sender);  /* Self */
	ASSERT_EQ(42, value_to_int(msg->value));

	message_free(msg);
	block_free(block);
}

/* Test 15: Message with nil value */
void test_nil_message(void)
{
	Mailbox mailbox;
	mailbox_init(&mailbox);

	Value *val = value_nil();
	Message *msg = message_new(1, val);
	mailbox_push(&mailbox, msg, 10);

	Message *recv = mailbox_pop(&mailbox);
	ASSERT(recv != NULL);
	ASSERT(value_is_nil(recv->value));

	message_free(recv);
	mailbox_free(&mailbox);
}

int main(void)
{
	printf("=== E2E Message Passing Tests ===\n\n");

	RUN_TEST(test_mailbox_init);
	RUN_TEST(test_single_message);
	RUN_TEST(test_fifo_ordering);
	RUN_TEST(test_mailbox_limits);
	RUN_TEST(test_overflow_drop_old);
	RUN_TEST(test_block_messaging);
	RUN_TEST(test_multiple_senders);
	RUN_TEST(test_complex_message_types);
	RUN_TEST(test_receive_empty);
	RUN_TEST(test_high_volume_messaging);
	RUN_TEST(test_mailbox_statistics);
	RUN_TEST(test_message_ownership);
	RUN_TEST(test_concurrent_messaging);
	RUN_TEST(test_self_messaging);
	RUN_TEST(test_nil_message);

	return TEST_RESULT();
}
