/*
 * Agim - Bytecode Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/bytecode.h"

void test_chunk_create(void) {
    Chunk *chunk = chunk_new();
    ASSERT(chunk != NULL);
    ASSERT_EQ(0, chunk->code_size);
    ASSERT_EQ(0, chunk->constants_size);
    chunk_free(chunk);
}

void test_chunk_write(void) {
    Chunk *chunk = chunk_new();

    chunk_write_opcode(chunk, OP_NIL, 1);
    chunk_write_opcode(chunk, OP_TRUE, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    ASSERT_EQ(3, chunk->code_size);
    ASSERT_EQ(OP_NIL, chunk->code[0]);
    ASSERT_EQ(OP_TRUE, chunk->code[1]);
    ASSERT_EQ(OP_HALT, chunk->code[2]);

    chunk_free(chunk);
}

void test_chunk_constants(void) {
    Chunk *chunk = chunk_new();

    size_t i1 = chunk_add_constant(chunk, value_int(42));
    size_t i2 = chunk_add_constant(chunk, value_string("hello"));

    ASSERT_EQ(0, i1);
    ASSERT_EQ(1, i2);
    ASSERT_EQ(2, chunk->constants_size);
    ASSERT_EQ(42, chunk->constants[0]->as.integer);

    chunk_free(chunk);
}

void test_chunk_jump(void) {
    Chunk *chunk = chunk_new();

    chunk_write_opcode(chunk, OP_TRUE, 1);
    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);

    /* Write some instructions */
    chunk_write_opcode(chunk, OP_NIL, 2);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Patch the jump */
    chunk_patch_jump(chunk, jump);

    chunk_write_opcode(chunk, OP_HALT, 3);

    /* Verify jump offset */
    uint16_t offset = chunk_read_arg(chunk, jump);
    ASSERT_EQ(2, offset); /* Should skip NIL and POP */

    chunk_free(chunk);
}

void test_bytecode_create(void) {
    Bytecode *code = bytecode_new();
    ASSERT(code != NULL);
    ASSERT(code->main != NULL);
    ASSERT_EQ(0, code->functions_count);
    bytecode_free(code);
}

void test_bytecode_strings(void) {
    Bytecode *code = bytecode_new();

    size_t i1 = bytecode_add_string(code, "foo");
    size_t i2 = bytecode_add_string(code, "bar");
    size_t i3 = bytecode_add_string(code, "foo"); /* Duplicate */

    ASSERT_EQ(0, i1);
    ASSERT_EQ(1, i2);
    ASSERT_EQ(0, i3); /* Should return existing index */

    ASSERT_STR_EQ("foo", bytecode_get_string(code, 0));
    ASSERT_STR_EQ("bar", bytecode_get_string(code, 1));

    bytecode_free(code);
}

void test_bytecode_functions(void) {
    Bytecode *code = bytecode_new();

    Chunk *fn1 = chunk_new();
    chunk_write_opcode(fn1, OP_NIL, 1);
    chunk_write_opcode(fn1, OP_RETURN, 1);

    Chunk *fn2 = chunk_new();
    chunk_write_opcode(fn2, OP_TRUE, 1);
    chunk_write_opcode(fn2, OP_RETURN, 1);

    size_t i1 = bytecode_add_function(code, fn1);
    size_t i2 = bytecode_add_function(code, fn2);

    ASSERT_EQ(0, i1);
    ASSERT_EQ(1, i2);
    ASSERT_EQ(2, code->functions_count);

    bytecode_free(code);
}

int main(void) {
    RUN_TEST(test_chunk_create);
    RUN_TEST(test_chunk_write);
    RUN_TEST(test_chunk_constants);
    RUN_TEST(test_chunk_jump);
    RUN_TEST(test_bytecode_create);
    RUN_TEST(test_bytecode_strings);
    RUN_TEST(test_bytecode_functions);

    return TEST_RESULT();
}
