/*
 * Agim - Block Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "vm/bytecode.h"

void test_block_create(void) {
    Block *block = block_new(1, "test", NULL);

    ASSERT(block != NULL);
    ASSERT_EQ(1, block->pid);
    ASSERT(block->vm != NULL);
    ASSERT(block->heap != NULL);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));
    ASSERT(block_is_alive(block));

    block_free(block);
}

void test_block_with_limits(void) {
    BlockLimits limits = {
        .max_heap_size = 1024 * 1024,
        .max_stack_depth = 512,
        .max_call_depth = 64,
        .max_reductions = 5000,
        .max_mailbox_size = 100,
    };

    Block *block = block_new(2, "limited", &limits);

    ASSERT(block != NULL);
    ASSERT_EQ(1024 * 1024, block->limits.max_heap_size);
    ASSERT_EQ(5000, block->limits.max_reductions);

    block_free(block);
}

void test_block_capabilities(void) {
    Block *block = block_new(3, "caps_test", NULL);

    /* No capabilities by default */
    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_INFER));

    /* Grant some capabilities */
    block_grant(block, CAP_SPAWN | CAP_SEND | CAP_RECEIVE);

    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));
    ASSERT(!block_has_cap(block, CAP_INFER));

    /* Revoke one */
    block_revoke(block, CAP_SEND);

    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));

    block_free(block);
}

void test_block_linking(void) {
    Block *block = block_new(10, "linker", NULL);

    /* Link to several blocks */
    ASSERT(block_link(block, 20));
    ASSERT(block_link(block, 30));
    ASSERT(block_link(block, 40));

    size_t count;
    const Pid *links = block_get_links(block, &count);

    ASSERT_EQ(3, count);
    ASSERT(links != NULL);

    /* Verify links (order may vary) */
    bool found_20 = false, found_30 = false, found_40 = false;
    for (size_t i = 0; i < count; i++) {
        if (links[i] == 20) found_20 = true;
        if (links[i] == 30) found_30 = true;
        if (links[i] == 40) found_40 = true;
    }
    ASSERT(found_20 && found_30 && found_40);

    /* Unlink one */
    block_unlink(block, 30);
    links = block_get_links(block, &count);
    ASSERT_EQ(2, count);

    block_free(block);
}

void test_block_run_simple(void) {
    Block *block = block_new(100, "runner", NULL);

    /* Create simple bytecode: push 42, halt */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    block_load(block, code);
    BlockRunResult result = block_run(block);

    ASSERT_EQ(BLOCK_RUN_HALTED, result);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(!block_is_alive(block));
    ASSERT_EQ(0, block->u.exit.exit_code);

    block_free(block);
    bytecode_free(code);
}

void test_block_run_yield(void) {
    Block *block = block_new(101, "yielder", NULL);

    /* Set very low reduction limit */
    block->limits.max_reductions = 5;

    /* Create bytecode with a loop that will exceed reductions */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /*
     * loop:
     *   CONST 1
     *   POP
     *   LOOP loop
     */
    chunk_add_constant(chunk, value_int(1));

    size_t loop_start = chunk->code_size;

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_opcode(chunk, OP_POP, 1);

    /* Jump back to loop_start */
    chunk_write_opcode(chunk, OP_LOOP, 1);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 1);
    chunk_write_byte(chunk, offset & 0xFF, 1);

    block_load(block, code);
    BlockRunResult result = block_run(block);

    /* Should yield due to reduction limit */
    ASSERT_EQ(BLOCK_RUN_YIELD, result);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));
    ASSERT(block_is_alive(block));
    ASSERT(block->counters.reductions > 0);

    block_free(block);
    bytecode_free(code);
}

void test_block_crash(void) {
    Block *block = block_new(102, "crasher", NULL);

    /* Create bytecode that causes division by zero */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(10));
    chunk_add_constant(chunk, value_int(0));

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 1, 1);

    chunk_write_opcode(chunk, OP_DIV, 1);

    block_load(block, code);
    BlockRunResult result = block_run(block);

    ASSERT_EQ(BLOCK_RUN_ERROR, result);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(!block_is_alive(block));
    ASSERT(block->u.exit.exit_code != 0);
    ASSERT(block->u.exit.exit_reason != NULL);

    block_free(block);
    bytecode_free(code);
}

void test_block_exit(void) {
    Block *block = block_new(103, "exiter", NULL);

    block_exit(block, 42);

    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT_EQ(42, block->u.exit.exit_code);
    ASSERT(!block_is_alive(block));

    block_free(block);
}

void test_block_state_names(void) {
    ASSERT_STR_EQ("RUNNABLE", block_state_name(BLOCK_RUNNABLE));
    ASSERT_STR_EQ("RUNNING", block_state_name(BLOCK_RUNNING));
    ASSERT_STR_EQ("WAITING", block_state_name(BLOCK_WAITING));
    ASSERT_STR_EQ("DEAD", block_state_name(BLOCK_DEAD));
}

int main(void) {
    RUN_TEST(test_block_create);
    RUN_TEST(test_block_with_limits);
    RUN_TEST(test_block_capabilities);
    RUN_TEST(test_block_linking);
    RUN_TEST(test_block_run_simple);
    RUN_TEST(test_block_run_yield);
    RUN_TEST(test_block_crash);
    RUN_TEST(test_block_exit);
    RUN_TEST(test_block_state_names);

    return TEST_RESULT();
}
