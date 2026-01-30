/*
 * Agim - Message Passing Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "vm/primitives.h"

/*============================================================================
 * Mailbox Unit Tests
 *============================================================================*/

void test_mailbox_init(void) {
    Mailbox mailbox;
    mailbox_init(&mailbox);

    ASSERT(mailbox_empty(&mailbox));
    ASSERT_EQ(0, mailbox_count(&mailbox));

    mailbox_free(&mailbox);
}

void test_mailbox_push_pop(void) {
    Mailbox mailbox;
    mailbox_init(&mailbox);

    /* Create and push a message */
    Value *v1 = value_int(42);
    Message *msg1 = message_new(1, v1);
    ASSERT(mailbox_push(&mailbox, msg1, 100));

    ASSERT(!mailbox_empty(&mailbox));
    ASSERT_EQ(1, mailbox_count(&mailbox));

    /* Push another message */
    Value *v2 = value_string("hello");
    Message *msg2 = message_new(2, v2);
    ASSERT(mailbox_push(&mailbox, msg2, 100));

    ASSERT_EQ(2, mailbox_count(&mailbox));

    /* Pop messages (FIFO order) */
    Message *popped1 = mailbox_pop(&mailbox);
    ASSERT(popped1 != NULL);
    ASSERT_EQ(1, popped1->sender);
    ASSERT_EQ(42, popped1->value->as.integer);
    message_free(popped1);

    Message *popped2 = mailbox_pop(&mailbox);
    ASSERT(popped2 != NULL);
    ASSERT_EQ(2, popped2->sender);
    ASSERT(value_is_string(popped2->value));
    message_free(popped2);

    /* Mailbox should be empty now */
    ASSERT(mailbox_empty(&mailbox));
    ASSERT(mailbox_pop(&mailbox) == NULL);

    mailbox_free(&mailbox);
}

void test_mailbox_limit(void) {
    Mailbox mailbox;
    mailbox_init(&mailbox);

    /* Fill mailbox to limit */
    for (int i = 0; i < 3; i++) {
        Message *msg = message_new(1, value_int(i));
        ASSERT(mailbox_push(&mailbox, msg, 3));
    }

    ASSERT_EQ(3, mailbox_count(&mailbox));

    /* Should fail when full */
    Message *overflow = message_new(1, value_int(99));
    ASSERT(!mailbox_push(&mailbox, overflow, 3));
    message_free(overflow);

    mailbox_free(&mailbox);
}

/*============================================================================
 * Block Message Tests
 *============================================================================*/

void test_block_send_receive(void) {
    BlockLimits limits = block_limits_default();
    Block *sender = block_new(1, "sender", &limits);
    Block *receiver = block_new(2, "receiver", &limits);

    /* Grant capabilities */
    block_grant(sender, CAP_SEND);
    block_grant(receiver, CAP_RECEIVE);

    /* Send a message */
    Value *msg_value = value_string("hello from sender");
    ASSERT(block_send(receiver, sender->pid, msg_value));
    value_free(msg_value);

    /* Verify receiver has message */
    ASSERT(block_has_messages(receiver));
    ASSERT_EQ(1, receiver->counters.messages_received);

    /* Receive the message */
    Message *received = block_receive(receiver);
    ASSERT(received != NULL);
    ASSERT_EQ(sender->pid, received->sender);
    ASSERT(value_is_string(received->value));

    message_free(received);
    block_free(sender);
    block_free(receiver);
}

void test_block_deep_copy_isolation(void) {
    BlockLimits limits = block_limits_default();
    Block *sender = block_new(1, "sender", &limits);
    Block *receiver = block_new(2, "receiver", &limits);

    /* Create a complex value */
    Value *original = value_array();
    original = array_push(original, value_int(1));
    original = array_push(original, value_int(2));
    original = array_push(original, value_int(3));

    /* Send it */
    ASSERT(block_send(receiver, sender->pid, original));

    /* Modify original - COW may return a new Value */
    original = array_push(original, value_int(4));
    ASSERT_EQ(4, array_length(original));

    /* Received value should not be affected */
    Message *received = block_receive(receiver);
    ASSERT(received != NULL);
    ASSERT_EQ(3, array_length(received->value)); /* Still 3, not 4 */

    message_free(received);
    value_free(original);
    block_free(sender);
    block_free(receiver);
}

/*============================================================================
 * VM Opcode Tests
 *============================================================================*/

/* Helper: create bytecode that pushes SELF */
static Bytecode *make_self_code(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_SELF, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

void test_opcode_self(void) {
    Scheduler *sched = scheduler_new(NULL);
    Bytecode *code = make_self_code();

    Pid pid = scheduler_spawn_ex(sched, code, "self_test", CAP_ALL, NULL);
    scheduler_run(sched);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    /* The SELF opcode should have pushed the block's PID */
    Value *result = vm_peek(block->vm, 0);
    ASSERT(result != NULL);
    ASSERT_EQ(VAL_PID, result->type);
    ASSERT_EQ(pid, result->as.pid);

    scheduler_free(sched);
    bytecode_free(code);
}

/* Helper: create bytecode that sends a message */
static Bytecode *make_send_code(Pid target_pid, int64_t msg_value) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Push target PID */
    chunk_add_constant(chunk, value_pid(target_pid));
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Push message value */
    chunk_add_constant(chunk, value_int(msg_value));
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 1, 2);

    /* Send */
    chunk_write_opcode(chunk, OP_SEND, 3);
    chunk_write_opcode(chunk, OP_HALT, 3);

    return code;
}

/* Helper: create bytecode that receives a message */
static Bytecode *make_receive_code(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_RECEIVE, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Helper: create simple halt code */
static Bytecode *make_halt_code(void) {
    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);
    return code;
}

void test_opcode_send_receive(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* First, spawn a receiver that will wait */
    Bytecode *recv_code = make_receive_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_SEND | CAP_RECEIVE, NULL);

    /* Run one step - receiver should go to WAITING */
    scheduler_step(sched);
    Block *receiver = scheduler_get_block(sched, receiver_pid);
    ASSERT_EQ(BLOCK_WAITING, block_state(receiver));

    /* Now spawn sender with receiver's PID */
    Bytecode *send_code = make_send_code(receiver_pid, 42);
    Pid sender_pid = scheduler_spawn_ex(sched, send_code, "sender",
                                        CAP_SEND | CAP_RECEIVE, NULL);
    (void)sender_pid;

    /* Run to completion */
    scheduler_run(sched);

    /* Verify both blocks completed */
    ASSERT_EQ(BLOCK_DEAD, block_state(receiver));

    /* Receiver should have the message on its stack */
    Value *result = vm_peek(receiver->vm, 0);
    ASSERT(result != NULL);
    ASSERT_EQ(VAL_MAP, result->type);

    /* Check message content */
    Value *value = map_get(result, "value");
    ASSERT(value != NULL);
    ASSERT_EQ(42, value->as.integer);

    scheduler_free(sched);
    bytecode_free(recv_code);
    bytecode_free(send_code);
}

void test_send_wakes_waiting_block(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Spawn receiver first */
    Bytecode *recv_code = make_receive_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_RECEIVE, NULL);

    /* Step once to put receiver in WAITING state */
    scheduler_step(sched);
    Block *receiver = scheduler_get_block(sched, receiver_pid);
    ASSERT_EQ(BLOCK_WAITING, block_state(receiver));

    /* Now spawn sender */
    Bytecode *send_code = make_send_code(receiver_pid, 100);
    scheduler_spawn_ex(sched, send_code, "sender", CAP_SEND, NULL);

    /* Run sender - should wake receiver */
    scheduler_step(sched);

    /* Receiver should now be runnable */
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(receiver));

    /* Run to completion */
    scheduler_run(sched);

    ASSERT_EQ(BLOCK_DEAD, block_state(receiver));

    scheduler_free(sched);
    bytecode_free(recv_code);
    bytecode_free(send_code);
}

void test_send_without_capability(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Spawn receiver */
    Bytecode *recv_code = make_halt_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_RECEIVE, NULL);

    /* Spawn sender WITHOUT CAP_SEND */
    Bytecode *send_code = make_send_code(receiver_pid, 42);
    Pid sender_pid = scheduler_spawn_ex(sched, send_code, "sender",
                                        CAP_NONE, NULL); /* No capabilities! */

    /* Run - sender should crash */
    scheduler_run(sched);

    Block *sender = scheduler_get_block(sched, sender_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(sender));
    ASSERT(sender->u.exit.exit_reason != NULL); /* Should have error */

    scheduler_free(sched);
    bytecode_free(recv_code);
    bytecode_free(send_code);
}

/*============================================================================
 * Primitives Tests
 *============================================================================*/

void test_primitives_memory(void) {
    PrimitivesRuntime *rt = primitives_new();

    /* Set a value */
    Value *v1 = value_string("test value");
    ASSERT(primitives_memory_set(rt, "key1", v1));
    value_free(v1);

    /* Check it exists */
    ASSERT(primitives_memory_has(rt, "key1"));
    ASSERT(!primitives_memory_has(rt, "nonexistent"));

    /* Get the value */
    Value *retrieved = primitives_memory_get(rt, "key1");
    ASSERT(retrieved != NULL);
    ASSERT(value_is_string(retrieved));
    ASSERT_STR_EQ("test value", retrieved->as.string->data);
    value_free(retrieved);

    /* Update the value */
    Value *v2 = value_int(42);
    ASSERT(primitives_memory_set(rt, "key1", v2));
    value_free(v2);

    Value *updated = primitives_memory_get(rt, "key1");
    ASSERT(value_is_int(updated));
    ASSERT_EQ(42, updated->as.integer);
    value_free(updated);

    /* Delete */
    ASSERT(primitives_memory_delete(rt, "key1"));
    ASSERT(!primitives_memory_has(rt, "key1"));

    primitives_free(rt);
}

void test_primitives_tools(void) {
    PrimitivesRuntime *rt = primitives_new();
    primitives_register_builtins(rt);

    /* Test type tool */
    Value *args[1] = { value_int(42) };
    Value *result = primitives_call_tool(rt, NULL, "type", args, 1);
    ASSERT(result != NULL);
    ASSERT(value_is_string(result));
    ASSERT_STR_EQ("int", result->as.string->data);
    value_free(result);
    value_free(args[0]);

    /* Test len tool */
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));
    args[0] = arr;
    result = primitives_call_tool(rt, NULL, "len", args, 1);
    ASSERT(result != NULL);
    ASSERT_EQ(3, result->as.integer);
    value_free(result);
    value_free(arr);

    /* Test nonexistent tool */
    result = primitives_call_tool(rt, NULL, "nonexistent", NULL, 0);
    ASSERT(result == NULL);

    primitives_free(rt);
}

/* Mock inference callback for testing */
static Value *mock_infer(Block *block, Value *prompt, void *context) {
    (void)block;
    (void)context;

    /* Return a response based on prompt */
    if (value_is_string(prompt)) {
        return value_string("Mock response to your prompt");
    }
    return value_nil();
}

void test_primitives_infer(void) {
    PrimitivesRuntime *rt = primitives_new();
    primitives_set_infer(rt, mock_infer, NULL);

    Value *prompt = value_string("Hello, AI!");
    Value *result = primitives_infer(rt, NULL, prompt);

    ASSERT(result != NULL);
    ASSERT(value_is_string(result));
    ASSERT_STR_EQ("Mock response to your prompt", result->as.string->data);

    value_free(prompt);
    value_free(result);
    primitives_free(rt);
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    /* Mailbox tests */
    RUN_TEST(test_mailbox_init);
    RUN_TEST(test_mailbox_push_pop);
    RUN_TEST(test_mailbox_limit);

    /* Block message tests */
    RUN_TEST(test_block_send_receive);
    RUN_TEST(test_block_deep_copy_isolation);

    /* VM opcode tests */
    RUN_TEST(test_opcode_self);
    RUN_TEST(test_opcode_send_receive);
    RUN_TEST(test_send_wakes_waiting_block);
    RUN_TEST(test_send_without_capability);

    /* Primitives tests */
    RUN_TEST(test_primitives_memory);
    RUN_TEST(test_primitives_tools);
    RUN_TEST(test_primitives_infer);

    return TEST_RESULT();
}
