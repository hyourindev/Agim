/*
 * Agim - VM Process Operations Tests
 *
 * Comprehensive tests for process-related opcodes:
 * - OP_SPAWN with capability checks
 * - OP_SEND to valid/invalid/dead PIDs
 * - OP_RECEIVE with/without messages, with timeout
 * - OP_SELF returns correct PID
 * - OP_YIELD and reduction counting
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Create bytecode that just halts */
static Bytecode *make_halt_code(void) {
    Bytecode *code = bytecode_new();
    chunk_write_opcode(code->main, OP_HALT, 1);
    return code;
}

/* Create bytecode that pushes SELF and halts */
static Bytecode *make_self_code(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_SELF, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Create bytecode that yields and then halts */
static Bytecode *make_yield_code(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_YIELD, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    return code;
}

/* Create bytecode that yields N times then halts */
static Bytecode *make_multi_yield_code(int yields) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    for (int i = 0; i < yields; i++) {
        chunk_write_opcode(chunk, OP_YIELD, i + 1);
    }
    chunk_write_opcode(chunk, OP_HALT, yields + 1);

    return code;
}

/* Create bytecode that sends a message to a target PID */
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

/* Create bytecode that receives a message */
static Bytecode *make_receive_code(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_RECEIVE, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Create bytecode that loops with reduction counting */
static Bytecode *make_reduction_loop_code(int iterations) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Push counter */
    chunk_add_constant(chunk, value_int(iterations));
    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(0));

    /* counter = iterations */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* loop: if counter <= 0, jump to end */
    size_t loop_start = chunk->code_size;

    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_LE, 2);

    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);

    /* Pop the condition result */
    chunk_write_opcode(chunk, OP_POP, 2);

    /* counter = counter - 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    /* jump back to loop */
    chunk_write_opcode(chunk, OP_LOOP, 4);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, offset & 0xFF, 4);

    /* end: halt */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);
    chunk_write_opcode(chunk, OP_HALT, 5);

    return code;
}

/* ============================================================================
 * OP_SELF Tests
 * ============================================================================ */

void test_self_returns_correct_pid(void) {
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

void test_self_multiple_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code1 = make_self_code();
    Bytecode *code2 = make_self_code();
    Bytecode *code3 = make_self_code();

    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_ALL, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_ALL, NULL);
    Pid pid3 = scheduler_spawn_ex(sched, code3, "block3", CAP_ALL, NULL);

    scheduler_run(sched);

    /* Each block should have its own PID on stack */
    Block *b1 = scheduler_get_block(sched, pid1);
    Block *b2 = scheduler_get_block(sched, pid2);
    Block *b3 = scheduler_get_block(sched, pid3);

    Value *r1 = vm_peek(b1->vm, 0);
    Value *r2 = vm_peek(b2->vm, 0);
    Value *r3 = vm_peek(b3->vm, 0);

    ASSERT_EQ(pid1, r1->as.pid);
    ASSERT_EQ(pid2, r2->as.pid);
    ASSERT_EQ(pid3, r3->as.pid);

    scheduler_free(sched);
    bytecode_free(code1);
    bytecode_free(code2);
    bytecode_free(code3);
}

void test_self_different_pids(void) {
    /* Test that each block has a unique PID */
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code1 = make_self_code();
    Bytecode *code2 = make_self_code();

    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_ALL, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_ALL, NULL);

    /* PIDs must be different */
    ASSERT(pid1 != pid2);
    ASSERT(pid1 != PID_INVALID);
    ASSERT(pid2 != PID_INVALID);

    scheduler_run(sched);

    scheduler_free(sched);
    bytecode_free(code1);
    bytecode_free(code2);
}

/* ============================================================================
 * OP_YIELD Tests
 * ============================================================================ */

void test_yield_basic(void) {
    Scheduler *sched = scheduler_new(NULL);
    Bytecode *code = make_yield_code();

    Pid pid = scheduler_spawn_ex(sched, code, "yielder", CAP_ALL, NULL);

    /* First step: block should yield */
    scheduler_step(sched);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    /* Second step: block should halt */
    scheduler_step(sched);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
    bytecode_free(code);
}

void test_yield_multiple(void) {
    Scheduler *sched = scheduler_new(NULL);
    Bytecode *code = make_multi_yield_code(3);

    Pid pid = scheduler_spawn_ex(sched, code, "multi_yielder", CAP_ALL, NULL);

    /* Each yield should transition to runnable */
    for (int i = 0; i < 3; i++) {
        scheduler_step(sched);
        Block *block = scheduler_get_block(sched, pid);
        ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));
    }

    /* Final step should halt */
    scheduler_step(sched);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
    bytecode_free(code);
}

void test_yield_fairness(void) {
    /* Test that multiple blocks get fair execution via yield */
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *code1 = make_multi_yield_code(2);
    Bytecode *code2 = make_multi_yield_code(2);

    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_ALL, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_ALL, NULL);

    /* Run to completion */
    scheduler_run(sched);

    /* Both should complete */
    Block *b1 = scheduler_get_block(sched, pid1);
    Block *b2 = scheduler_get_block(sched, pid2);
    ASSERT_EQ(BLOCK_DEAD, block_state(b1));
    ASSERT_EQ(BLOCK_DEAD, block_state(b2));

    /* Context switches should show interleaving */
    SchedulerStats stats = scheduler_stats(sched);
    ASSERT(stats.context_switches >= 4); /* At least 2 yields per block */

    scheduler_free(sched);
    bytecode_free(code1);
    bytecode_free(code2);
}

void test_reduction_counting(void) {
    /* Test that blocks yield when reduction limit is hit */
    SchedulerConfig config = scheduler_config_default();
    Scheduler *sched = scheduler_new(&config);

    BlockLimits limits = block_limits_default();
    limits.max_reductions = 10; /* Very low to force preemption */

    Bytecode *code = make_reduction_loop_code(100);
    Pid pid = scheduler_spawn_ex(sched, code, "looper", CAP_ALL, &limits);

    scheduler_run(sched);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    /* Should have context switched multiple times due to reduction limit */
    SchedulerStats stats = scheduler_stats(sched);
    ASSERT(stats.context_switches > 1);

    scheduler_free(sched);
    bytecode_free(code);
}

/* ============================================================================
 * OP_SEND Tests
 * ============================================================================ */

void test_send_to_valid_pid(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create receiver that waits for message */
    Bytecode *recv_code = make_receive_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_SEND | CAP_RECEIVE, NULL);

    /* Step to put receiver in waiting state */
    scheduler_step(sched);
    Block *receiver = scheduler_get_block(sched, receiver_pid);
    ASSERT_EQ(BLOCK_WAITING, block_state(receiver));

    /* Create sender that sends to receiver */
    Bytecode *send_code = make_send_code(receiver_pid, 42);
    Pid sender_pid = scheduler_spawn_ex(sched, send_code, "sender",
                                        CAP_SEND | CAP_RECEIVE, NULL);
    (void)sender_pid;

    scheduler_run(sched);

    /* Receiver should have message on stack */
    ASSERT_EQ(BLOCK_DEAD, block_state(receiver));
    Value *result = vm_peek(receiver->vm, 0);
    ASSERT(result != NULL);
    ASSERT_EQ(VAL_MAP, result->type);

    Value *value = map_get(result, "value");
    ASSERT(value != NULL);
    ASSERT_EQ(42, value->as.integer);

    scheduler_free(sched);
    bytecode_free(recv_code);
    bytecode_free(send_code);
}

void test_send_to_invalid_pid(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Send to non-existent PID (99999) */
    Bytecode *send_code = make_send_code(99999, 42);
    Pid sender_pid = scheduler_spawn_ex(sched, send_code, "sender",
                                        CAP_SEND | CAP_RECEIVE, NULL);

    scheduler_run(sched);

    /* Sender should have error state */
    Block *sender = scheduler_get_block(sched, sender_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(sender));
    /* The send to invalid PID should fail with error */
    ASSERT(sender->u.exit.exit_reason != NULL);

    scheduler_free(sched);
    bytecode_free(send_code);
}

void test_send_to_dead_process(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create and complete a process */
    Bytecode *halt_code = make_halt_code();
    Pid dead_pid = scheduler_spawn_ex(sched, halt_code, "dead_block",
                                      CAP_ALL, NULL);

    /* Run it to completion */
    scheduler_run(sched);
    Block *dead_block = scheduler_get_block(sched, dead_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(dead_block));

    /* Now try to send to the dead process */
    Bytecode *send_code = make_send_code(dead_pid, 42);
    Pid sender_pid = scheduler_spawn_ex(sched, send_code, "sender",
                                        CAP_SEND | CAP_RECEIVE, NULL);

    scheduler_run(sched);

    /* Sender should complete but send fails silently or with error */
    Block *sender = scheduler_get_block(sched, sender_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(sender));

    scheduler_free(sched);
    bytecode_free(halt_code);
    bytecode_free(send_code);
}

void test_send_without_capability(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create receiver */
    Bytecode *recv_code = make_halt_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_RECEIVE, NULL);

    /* Create sender WITHOUT CAP_SEND */
    Bytecode *send_code = make_send_code(receiver_pid, 42);
    Pid sender_pid = scheduler_spawn_ex(sched, send_code, "sender",
                                        CAP_NONE, NULL); /* No capabilities! */

    scheduler_run(sched);

    /* Sender should crash due to capability check */
    Block *sender = scheduler_get_block(sched, sender_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(sender));
    ASSERT(sender->u.exit.exit_reason != NULL);

    scheduler_free(sched);
    bytecode_free(recv_code);
    bytecode_free(send_code);
}

void test_send_wakes_waiting_receiver(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Spawn receiver first */
    Bytecode *recv_code = make_receive_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_RECEIVE, NULL);

    /* Step to put receiver in WAITING state */
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

/* ============================================================================
 * OP_RECEIVE Tests
 * ============================================================================ */

void test_receive_with_message(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create receiver that will receive a message */
    Bytecode *recv_code = make_receive_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_SEND | CAP_RECEIVE, NULL);

    /* Step to put receiver in waiting state */
    scheduler_step(sched);

    /* Send message directly via block API */
    Block *receiver = scheduler_get_block(sched, receiver_pid);
    Value *msg = value_int(99);
    block_send(receiver, 1, msg); /* sender PID = 1 */
    value_free(msg);

    /* Wake the receiver - block_send doesn't automatically wake */
    scheduler_wake_block(sched, receiver);

    /* Resume receiver - should get the message */
    scheduler_run(sched);

    ASSERT_EQ(BLOCK_DEAD, block_state(receiver));
    Value *result = vm_peek(receiver->vm, 0);
    ASSERT(result != NULL);
    ASSERT_EQ(VAL_MAP, result->type);

    Value *value = map_get(result, "value");
    ASSERT(value != NULL);
    ASSERT_EQ(99, value->as.integer);

    scheduler_free(sched);
    bytecode_free(recv_code);
}

void test_receive_without_message_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);

    Bytecode *recv_code = make_receive_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_RECEIVE, NULL);

    /* Single step - receiver should go to WAITING */
    scheduler_step(sched);

    Block *receiver = scheduler_get_block(sched, receiver_pid);
    ASSERT_EQ(BLOCK_WAITING, block_state(receiver));

    /* Verify no message was received */
    ASSERT(!block_has_messages(receiver));

    scheduler_free(sched);
    bytecode_free(recv_code);
}

void test_receive_without_capability(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create receiver WITHOUT CAP_RECEIVE */
    Bytecode *recv_code = make_receive_code();
    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_NONE, NULL); /* No capabilities! */

    scheduler_run(sched);

    /* Receiver should crash due to capability check */
    Block *receiver = scheduler_get_block(sched, receiver_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(receiver));
    ASSERT(receiver->u.exit.exit_reason != NULL);

    scheduler_free(sched);
    bytecode_free(recv_code);
}

void test_receive_fifo_order(void) {
    /* Test that messages are received in FIFO order */
    Scheduler *sched = scheduler_new(NULL);

    /* Create a block that receives multiple messages */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Receive 3 messages and put them in an array */
    /* Create array */
    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);

    /* Receive and push first message */
    chunk_write_opcode(chunk, OP_RECEIVE, 2);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 2);

    /* Receive and push second message */
    chunk_write_opcode(chunk, OP_RECEIVE, 3);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 3);

    /* Receive and push third message */
    chunk_write_opcode(chunk, OP_RECEIVE, 4);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 4);

    chunk_write_opcode(chunk, OP_HALT, 5);

    Pid receiver_pid = scheduler_spawn_ex(sched, code, "receiver",
                                          CAP_RECEIVE, NULL);

    /* Pre-send messages via block API before running */
    Block *receiver = scheduler_get_block(sched, receiver_pid);
    Value *m1 = value_int(111);
    Value *m2 = value_int(222);
    Value *m3 = value_int(333);
    block_send(receiver, 1, m1);
    block_send(receiver, 1, m2);
    block_send(receiver, 1, m3);
    value_free(m1);
    value_free(m2);
    value_free(m3);

    scheduler_run(sched);

    ASSERT_EQ(BLOCK_DEAD, block_state(receiver));

    /* Check array has messages in FIFO order */
    Value *result = vm_peek(receiver->vm, 0);
    ASSERT(result != NULL);
    ASSERT_EQ(VAL_ARRAY, result->type);
    ASSERT_EQ(3, array_length(result));

    /* Messages should be in order 111, 222, 333 */
    Value *e1 = array_get(result, 0);
    Value *e2 = array_get(result, 1);
    Value *e3 = array_get(result, 2);

    /* Each element is a map with value field */
    ASSERT_EQ(111, map_get(e1, "value")->as.integer);
    ASSERT_EQ(222, map_get(e2, "value")->as.integer);
    ASSERT_EQ(333, map_get(e3, "value")->as.integer);

    scheduler_free(sched);
    bytecode_free(code);
}

/* ============================================================================
 * OP_SPAWN Tests (via scheduler_spawn_ex API)
 * Note: OP_SPAWN at bytecode level requires complex function setup.
 * Testing spawn capabilities through the scheduler API which uses the same
 * capability checks internally.
 * ============================================================================ */

void test_spawn_capability_enforcement(void) {
    /* Test that CAP_SPAWN affects scheduler_spawn_ex indirectly.
     * When a block tries to spawn via OP_SPAWN, it checks CAP_SPAWN.
     * We can test by creating bytecode that pushes a function and spawns. */

    Scheduler *sched = scheduler_new(NULL);

    /* Create simple bytecode that will try to spawn a function */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* First, create a function chunk and add it to the bytecode */
    Chunk *fn_chunk = chunk_new();
    chunk_write_opcode(fn_chunk, OP_HALT, 1);
    size_t fn_index = bytecode_add_function(code, fn_chunk);

    /* Create function value pointing to this chunk */
    Value *fn_val = value_function("child", 0);
    fn_val->as.function->code_offset = fn_index;

    /* Add function to constants and load it */
    size_t const_idx = chunk_add_constant(chunk, fn_val);
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, (const_idx >> 8) & 0xFF, 1);
    chunk_write_byte(chunk, const_idx & 0xFF, 1);

    /* Try to spawn */
    chunk_write_opcode(chunk, OP_SPAWN, 2);
    chunk_write_opcode(chunk, OP_HALT, 3);

    /* Spawn parent WITHOUT CAP_SPAWN */
    Pid parent_pid = scheduler_spawn_ex(sched, code, "parent",
                                        CAP_SEND | CAP_RECEIVE, NULL); /* Missing CAP_SPAWN! */

    scheduler_run(sched);

    /* Parent should fail due to capability check */
    Block *parent = scheduler_get_block(sched, parent_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(parent));
    ASSERT(parent->u.exit.exit_reason != NULL);

    scheduler_free(sched);
    bytecode_free(code);
}

void test_spawn_with_capability(void) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create bytecode that spawns a child process */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* First, create a function chunk */
    Chunk *fn_chunk = chunk_new();
    chunk_write_opcode(fn_chunk, OP_HALT, 1);
    size_t fn_index = bytecode_add_function(code, fn_chunk);

    /* Create function value pointing to this chunk */
    Value *fn_val = value_function("child", 0);
    fn_val->as.function->code_offset = fn_index;

    /* Add function to constants and load it */
    size_t const_idx = chunk_add_constant(chunk, fn_val);
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, (const_idx >> 8) & 0xFF, 1);
    chunk_write_byte(chunk, const_idx & 0xFF, 1);

    /* Spawn */
    chunk_write_opcode(chunk, OP_SPAWN, 2);
    chunk_write_opcode(chunk, OP_HALT, 3);

    /* Spawn parent WITH CAP_SPAWN */
    Pid parent_pid = scheduler_spawn_ex(sched, code, "parent",
                                        CAP_SPAWN | CAP_SEND, NULL);

    scheduler_run(sched);

    /* Parent should succeed */
    Block *parent = scheduler_get_block(sched, parent_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(parent));
    /* No error */
    ASSERT(parent->u.exit.exit_reason == NULL);

    /* Check that a child PID was pushed */
    Value *result = vm_peek(parent->vm, 0);
    ASSERT(result != NULL);
    ASSERT_EQ(VAL_PID, result->type);
    ASSERT(result->as.pid != parent_pid); /* Child has different PID */

    scheduler_free(sched);
    bytecode_free(code);
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

void test_ping_pong_communication(void) {
    /* Test two processes exchanging messages */
    Scheduler *sched = scheduler_new(NULL);

    /* Pong: receives message, sends response, then halts */
    Bytecode *pong_code = bytecode_new();
    Chunk *pong_chunk = pong_code->main;

    /* Receive message */
    chunk_write_opcode(pong_chunk, OP_RECEIVE, 1);
    /* Get sender PID from message */
    chunk_add_constant(pong_chunk, value_string("sender"));
    chunk_write_opcode(pong_chunk, OP_CONST, 2);
    chunk_write_byte(pong_chunk, 0, 2);
    chunk_write_byte(pong_chunk, 0, 2);
    chunk_write_opcode(pong_chunk, OP_MAP_GET, 2);
    /* Push response value */
    chunk_add_constant(pong_chunk, value_int(999)); /* pong response */
    chunk_write_opcode(pong_chunk, OP_CONST, 3);
    chunk_write_byte(pong_chunk, 0, 3);
    chunk_write_byte(pong_chunk, 1, 3);
    /* Send response */
    chunk_write_opcode(pong_chunk, OP_SEND, 4);
    /* Pop result */
    chunk_write_opcode(pong_chunk, OP_POP, 4);
    chunk_write_opcode(pong_chunk, OP_HALT, 5);

    Pid pong_pid = scheduler_spawn_ex(sched, pong_code, "pong",
                                      CAP_SEND | CAP_RECEIVE, NULL);

    /* Step to put pong in waiting state */
    scheduler_step(sched);
    Block *pong = scheduler_get_block(sched, pong_pid);
    ASSERT_EQ(BLOCK_WAITING, block_state(pong));

    /* Ping: sends message to pong, receives response */
    Bytecode *ping_code = bytecode_new();
    Chunk *ping_chunk = ping_code->main;

    /* Push pong PID */
    chunk_add_constant(ping_chunk, value_pid(pong_pid));
    chunk_write_opcode(ping_chunk, OP_CONST, 1);
    chunk_write_byte(ping_chunk, 0, 1);
    chunk_write_byte(ping_chunk, 0, 1);
    /* Push message */
    chunk_add_constant(ping_chunk, value_int(42)); /* ping message */
    chunk_write_opcode(ping_chunk, OP_CONST, 2);
    chunk_write_byte(ping_chunk, 0, 2);
    chunk_write_byte(ping_chunk, 1, 2);
    /* Send */
    chunk_write_opcode(ping_chunk, OP_SEND, 3);
    /* Pop send result */
    chunk_write_opcode(ping_chunk, OP_POP, 3);
    /* Receive response */
    chunk_write_opcode(ping_chunk, OP_RECEIVE, 4);
    chunk_write_opcode(ping_chunk, OP_HALT, 5);

    Pid ping_pid = scheduler_spawn_ex(sched, ping_code, "ping",
                                      CAP_SEND | CAP_RECEIVE, NULL);

    /* Run to completion */
    scheduler_run(sched);

    /* Both should be dead */
    Block *ping = scheduler_get_block(sched, ping_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(ping));
    ASSERT_EQ(BLOCK_DEAD, block_state(pong));

    /* Ping should have received pong's response (999) */
    Value *result = vm_peek(ping->vm, 0);
    ASSERT(result != NULL);
    ASSERT_EQ(VAL_MAP, result->type);
    Value *value = map_get(result, "value");
    ASSERT(value != NULL);
    ASSERT_EQ(999, value->as.integer);

    scheduler_free(sched);
    bytecode_free(pong_code);
    bytecode_free(ping_code);
}

void test_multiple_senders_single_receiver(void) {
    /* Test multiple processes sending to a single receiver */
    Scheduler *sched = scheduler_new(NULL);

    /* Receiver: receives 3 messages, puts them in array */
    Bytecode *recv_code = bytecode_new();
    Chunk *recv_chunk = recv_code->main;

    /* Create array */
    chunk_write_opcode(recv_chunk, OP_ARRAY_NEW, 1);

    /* Receive 3 messages */
    for (int i = 0; i < 3; i++) {
        chunk_write_opcode(recv_chunk, OP_RECEIVE, i + 2);
        chunk_write_opcode(recv_chunk, OP_ARRAY_PUSH, i + 2);
    }

    chunk_write_opcode(recv_chunk, OP_HALT, 5);

    Pid receiver_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                          CAP_RECEIVE, NULL);

    /* Create 3 senders */
    Bytecode *send1 = make_send_code(receiver_pid, 111);
    Bytecode *send2 = make_send_code(receiver_pid, 222);
    Bytecode *send3 = make_send_code(receiver_pid, 333);

    scheduler_spawn_ex(sched, send1, "sender1", CAP_SEND, NULL);
    scheduler_spawn_ex(sched, send2, "sender2", CAP_SEND, NULL);
    scheduler_spawn_ex(sched, send3, "sender3", CAP_SEND, NULL);

    scheduler_run(sched);

    Block *receiver = scheduler_get_block(sched, receiver_pid);
    ASSERT_EQ(BLOCK_DEAD, block_state(receiver));

    /* Receiver should have all 3 messages */
    Value *result = vm_peek(receiver->vm, 0);
    ASSERT(result != NULL);
    ASSERT_EQ(VAL_ARRAY, result->type);
    ASSERT_EQ(3, array_length(result));

    scheduler_free(sched);
    bytecode_free(recv_code);
    bytecode_free(send1);
    bytecode_free(send2);
    bytecode_free(send3);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    /* OP_SELF tests */
    RUN_TEST(test_self_returns_correct_pid);
    RUN_TEST(test_self_multiple_blocks);
    RUN_TEST(test_self_different_pids);

    /* OP_YIELD tests */
    RUN_TEST(test_yield_basic);
    RUN_TEST(test_yield_multiple);
    RUN_TEST(test_yield_fairness);
    RUN_TEST(test_reduction_counting);

    /* OP_SEND tests */
    RUN_TEST(test_send_to_valid_pid);
    RUN_TEST(test_send_to_invalid_pid);
    RUN_TEST(test_send_to_dead_process);
    RUN_TEST(test_send_without_capability);
    RUN_TEST(test_send_wakes_waiting_receiver);

    /* OP_RECEIVE tests */
    RUN_TEST(test_receive_with_message);
    RUN_TEST(test_receive_without_message_blocks);
    RUN_TEST(test_receive_without_capability);
    RUN_TEST(test_receive_fifo_order);

    /* OP_SPAWN tests */
    RUN_TEST(test_spawn_capability_enforcement);
    RUN_TEST(test_spawn_with_capability);

    /* Integration tests */
    RUN_TEST(test_ping_pong_communication);
    RUN_TEST(test_multiple_senders_single_receiver);

    return TEST_RESULT();
}
