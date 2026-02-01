/*
 * Agim - VM Control Flow Tests
 *
 * P1.1.1.4 - Comprehensive tests for all control flow operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/bytecode.h"
#include "vm/vm.h"

/*
 * =============================================================================
 * Test: OP_JUMP forward
 * =============================================================================
 */

void test_jump_forward_basic(void) {
    /* Test: Jump over an instruction that would push a value */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));
    chunk_add_constant(chunk, value_int(999));

    /* Push 42 first */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Jump over the 999 push */
    size_t jump = chunk_write_jump(chunk, OP_JUMP, 2);

    /* This should be skipped */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 4);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_forward_multiple_instructions(void) {
    /* Test: Jump over multiple instructions */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(2));
    chunk_add_constant(chunk, value_int(3));
    chunk_add_constant(chunk, value_int(100));

    /* Push 100 first (marker that we started) */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 3, 1);

    /* Jump over 3 constant pushes */
    size_t jump = chunk_write_jump(chunk, OP_JUMP, 2);

    /* These should be skipped */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 0, 3);

    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);

    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 2, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 4);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* Only 100 should be on stack */
    ASSERT_EQ(100, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_forward_zero_offset(void) {
    /* Test: Jump with offset 0 (no-op jump, continues to next instruction) */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(77));

    /* Push constant */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Jump 0 bytes (no-op) */
    chunk_write_opcode(chunk, OP_JUMP, 2);
    chunk_write_byte(chunk, 0, 2);  /* high byte */
    chunk_write_byte(chunk, 0, 2);  /* low byte */

    chunk_write_opcode(chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(77, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_forward_chained(void) {
    /* Test: Multiple jumps in sequence */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(2));
    chunk_add_constant(chunk, value_int(3));

    /* First jump */
    size_t jump1 = chunk_write_jump(chunk, OP_JUMP, 1);

    /* Skipped: push 1 */
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 0, 2);

    chunk_patch_jump(chunk, jump1);

    /* Second jump */
    size_t jump2 = chunk_write_jump(chunk, OP_JUMP, 3);

    /* Skipped: push 2 */
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, 0, 4);
    chunk_write_byte(chunk, 1, 4);

    chunk_patch_jump(chunk, jump2);

    /* This is executed: push 3 */
    chunk_write_opcode(chunk, OP_CONST, 5);
    chunk_write_byte(chunk, 0, 5);
    chunk_write_byte(chunk, 2, 5);

    chunk_write_opcode(chunk, OP_HALT, 6);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(3, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_JUMP to end of code
 * =============================================================================
 */

void test_jump_to_halt(void) {
    /* Test: Jump directly to HALT instruction */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));

    /* Push value first */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Jump to halt */
    size_t jump = chunk_write_jump(chunk, OP_JUMP, 2);

    /* Should be skipped */
    chunk_write_opcode(chunk, OP_POP, 3);
    chunk_write_opcode(chunk, OP_TRUE, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 4);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_out_of_bounds(void) {
    /* Test: Jump beyond code bounds should fail */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Write a jump with a very large offset */
    chunk_write_opcode(chunk, OP_JUMP, 1);
    chunk_write_byte(chunk, 0xFF, 1);  /* high byte */
    chunk_write_byte(chunk, 0xFF, 1);  /* low byte = 65535 */

    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_RUNTIME, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_JUMP_IF with truthy values
 * =============================================================================
 */

void test_jump_if_true_boolean(void) {
    /* Test: JUMP_IF with true takes the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));
    chunk_add_constant(chunk, value_int(0));

    chunk_write_opcode(chunk, OP_TRUE, 1);
    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Else: push 0 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    size_t end = chunk_write_jump(chunk, OP_JUMP, 3);

    /* Then: push 42 */
    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_POP, 4);
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, 0, 4);
    chunk_write_byte(chunk, 0, 4);

    chunk_patch_jump(chunk, end);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_if_nonzero_int(void) {
    /* Test: JUMP_IF with non-zero integer takes the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(99));
    chunk_add_constant(chunk, value_int(1));

    /* Push 99 (truthy int) */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* If not taken: push 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    size_t end = chunk_write_jump(chunk, OP_JUMP, 3);

    /* If taken: stack still has 99 */
    chunk_patch_jump(chunk, jump);

    chunk_patch_jump(chunk, end);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* 99 should be on stack (didn't pop, jumped to end) */
    ASSERT_EQ(99, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_if_negative_int(void) {
    /* Test: JUMP_IF with negative integer (truthy) takes the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(-1));
    chunk_add_constant(chunk, value_int(0));

    /* Push -1 (truthy) */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Not taken: push 0 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    size_t end = chunk_write_jump(chunk, OP_JUMP, 3);

    chunk_patch_jump(chunk, jump);
    chunk_patch_jump(chunk, end);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* -1 should be on stack (truthy, jumped) */
    ASSERT_EQ(-1, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_if_nonzero_float(void) {
    /* Test: JUMP_IF with non-zero float takes the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_float(0.1));
    chunk_add_constant(chunk, value_int(0));

    /* Push 0.1 (truthy float) */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Not taken: push 0 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    size_t end = chunk_write_jump(chunk, OP_JUMP, 3);

    chunk_patch_jump(chunk, jump);
    chunk_patch_jump(chunk, end);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* 0.1 should be on stack */
    ASSERT(vm_peek(vm, 0)->as.floating > 0.0);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_if_string(void) {
    /* Test: JUMP_IF with non-empty string (truthy) takes the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_string("hello"));
    chunk_add_constant(chunk, value_int(0));

    /* Push "hello" (truthy) */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Not taken: push 0 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    size_t end = chunk_write_jump(chunk, OP_JUMP, 3);

    chunk_patch_jump(chunk, jump);
    chunk_patch_jump(chunk, end);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* String should be on stack */
    ASSERT(vm_peek(vm, 0)->type == VAL_STRING);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_JUMP_IF with falsy values
 * =============================================================================
 */

void test_jump_if_false_boolean(void) {
    /* Test: JUMP_IF with false does not take the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_FALSE, 1);
    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* This executes: push 42 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 0, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_if_nil(void) {
    /* Test: JUMP_IF with nil does not take the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_NIL, 1);
    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* This executes */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 0, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_if_zero_int(void) {
    /* Test: JUMP_IF with 0 (falsy) does not take the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(0));
    chunk_add_constant(chunk, value_int(42));

    /* Push 0 (falsy) */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* This executes: push 42 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_if_zero_float(void) {
    /* Test: JUMP_IF with 0.0 (falsy) does not take the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_float(0.0));
    chunk_add_constant(chunk, value_int(42));

    /* Push 0.0 (falsy) */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* This executes: push 42 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_JUMP_UNLESS inverse behavior
 * =============================================================================
 */

void test_jump_unless_false(void) {
    /* Test: JUMP_UNLESS with false takes the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_FALSE, 1);
    size_t jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Skipped: push 0 */
    chunk_write_opcode(chunk, OP_NIL, 3);
    size_t end = chunk_write_jump(chunk, OP_JUMP, 3);

    /* This executes */
    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_POP, 4);
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, 0, 4);
    chunk_write_byte(chunk, 0, 4);

    chunk_patch_jump(chunk, end);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_unless_true(void) {
    /* Test: JUMP_UNLESS with true does not take the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_TRUE, 1);
    size_t jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* This executes */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 0, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_unless_nil(void) {
    /* Test: JUMP_UNLESS with nil takes the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_NIL, 1);
    size_t jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Skipped */
    chunk_write_opcode(chunk, OP_NIL, 3);
    size_t end = chunk_write_jump(chunk, OP_JUMP, 3);

    /* This executes */
    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_POP, 4);
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, 0, 4);
    chunk_write_byte(chunk, 0, 4);

    chunk_patch_jump(chunk, end);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_unless_zero(void) {
    /* Test: JUMP_UNLESS with 0 takes the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(0));
    chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* Skipped */
    chunk_write_opcode(chunk, OP_NIL, 3);
    size_t end = chunk_write_jump(chunk, OP_JUMP, 3);

    /* This executes */
    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_POP, 4);
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, 0, 4);
    chunk_write_byte(chunk, 1, 4);

    chunk_patch_jump(chunk, end);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_unless_nonzero(void) {
    /* Test: JUMP_UNLESS with non-zero does not take the jump */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(5));
    chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    size_t jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* This executes */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);

    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_LOOP backward jumps
 * =============================================================================
 */

void test_loop_basic(void) {
    /* Test: Simple loop that executes 3 times
     *
     * Algorithm:
     * counter = 3
     * while counter > 0:
     *   counter = counter - 1
     * result = counter  (should be 0)
     */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(3));  /* initial counter */
    chunk_add_constant(chunk, value_int(1));  /* decrement */
    chunk_add_constant(chunk, value_int(0));  /* comparison */

    /* Push counter = 3 */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Loop start */
    size_t loop_start = chunk->code_size;

    /* DUP counter, push 0, compare GT */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_GT, 2);

    /* If not > 0, exit loop */
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 2);
    chunk_write_opcode(chunk, OP_POP, 3);  /* pop comparison result */

    /* counter = counter - 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    /* Loop back */
    uint16_t loop_offset = (uint16_t)(chunk->code_size + 3 - loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 4);
    chunk_write_byte(chunk, (loop_offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, loop_offset & 0xFF, 4);

    /* Exit: pop comparison result, counter is on stack */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);  /* pop false from comparison */
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(0, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_loop_never_executes(void) {
    /* Test: Loop condition false from start - never executes body */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(0));  /* initial counter */
    chunk_add_constant(chunk, value_int(0));  /* comparison */

    /* Push counter = 0 */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Loop start */
    size_t loop_start = chunk->code_size;

    /* DUP counter, push 0, compare GT */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 1, 2);
    chunk_write_opcode(chunk, OP_GT, 2);

    /* If not > 0, exit loop */
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 2);
    chunk_write_opcode(chunk, OP_POP, 3);  /* pop comparison result */

    /* Body would go here (never executed) */

    /* Loop back */
    uint16_t loop_offset = (uint16_t)(chunk->code_size + 3 - loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 4);
    chunk_write_byte(chunk, (loop_offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, loop_offset & 0xFF, 4);

    /* Exit */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);  /* pop comparison result */
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(0, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_loop_once(void) {
    /* Test: Loop executes exactly once */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(1));  /* initial counter */
    chunk_add_constant(chunk, value_int(1));  /* decrement */
    chunk_add_constant(chunk, value_int(0));  /* comparison */

    /* Push counter = 1 */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Loop start */
    size_t loop_start = chunk->code_size;

    /* Check if counter > 0 */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_GT, 2);

    /* Exit if not > 0 */
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 2);
    chunk_write_opcode(chunk, OP_POP, 3);

    /* Decrement: counter = counter - 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    /* Loop back */
    uint16_t loop_offset = (uint16_t)(chunk->code_size + 3 - loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 4);
    chunk_write_byte(chunk, (loop_offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, loop_offset & 0xFF, 4);

    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(0, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_loop_backward_bounds_check(void) {
    /* Test: Loop with offset larger than current position should fail */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Write a loop at position 0 with large backward offset */
    chunk_write_opcode(chunk, OP_LOOP, 1);
    chunk_write_byte(chunk, 0xFF, 1);  /* high byte */
    chunk_write_byte(chunk, 0xFF, 1);  /* low byte = 65535 */

    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_RUNTIME, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_LOOP iteration limits (reduction counting)
 * =============================================================================
 */

void test_loop_many_iterations(void) {
    /* Test: Loop that iterates 100 times */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(100));
    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(0));

    /* Push counter = 100 */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Loop start */
    size_t loop_start = chunk->code_size;

    /* Check counter > 0 */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_GT, 2);

    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 2);
    chunk_write_opcode(chunk, OP_POP, 3);

    /* Decrement */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    /* Loop back */
    uint16_t loop_offset = (uint16_t)(chunk->code_size + 3 - loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 4);
    chunk_write_byte(chunk, (loop_offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, loop_offset & 0xFF, 4);

    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);
    chunk_write_opcode(chunk, OP_HALT, 5);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(0, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Nested loops
 * =============================================================================
 */

void test_nested_loops_simple(void) {
    /* Test: Nested loop that counts total iterations
     *
     * outer = 3, inner = 2, total = 0
     * while outer > 0:
     *   inner_count = 2
     *   while inner_count > 0:
     *     total = total + 1
     *     inner_count = inner_count - 1
     *   outer = outer - 1
     * Result: total should be 6 (3 * 2)
     */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(3));   /* outer initial */
    chunk_add_constant(chunk, value_int(2));   /* inner initial */
    chunk_add_constant(chunk, value_int(0));   /* total initial / comparison */
    chunk_add_constant(chunk, value_int(1));   /* increment/decrement */

    /* Stack layout: [outer, total] */

    /* Push outer = 3 */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Push total = 0 */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 2, 1);

    /* Outer loop start */
    size_t outer_loop_start = chunk->code_size;

    /* Check outer > 0: peek at outer (index 1), compare to 0 */
    chunk_write_opcode(chunk, OP_DUP2, 2);  /* [outer, total, outer, total] */
    chunk_write_opcode(chunk, OP_POP, 2);   /* [outer, total, outer] */
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);         /* [outer, total, outer, 0] */
    chunk_write_opcode(chunk, OP_GT, 2);    /* [outer, total, bool] */

    size_t outer_exit = chunk_write_jump(chunk, OP_JUMP_UNLESS, 2);
    chunk_write_opcode(chunk, OP_POP, 3);   /* pop comparison result */

    /* Push inner_count = 2 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);          /* [outer, total, inner] */

    /* Inner loop start */
    size_t inner_loop_start = chunk->code_size;

    /* Check inner > 0 */
    chunk_write_opcode(chunk, OP_DUP, 4);   /* [outer, total, inner, inner] */
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, 0, 4);
    chunk_write_byte(chunk, 2, 4);          /* [outer, total, inner, inner, 0] */
    chunk_write_opcode(chunk, OP_GT, 4);    /* [outer, total, inner, bool] */

    size_t inner_exit = chunk_write_jump(chunk, OP_JUMP_UNLESS, 4);
    chunk_write_opcode(chunk, OP_POP, 5);   /* [outer, total, inner] */

    /* total = total + 1: swap to get total on top */
    chunk_write_opcode(chunk, OP_SWAP, 5);  /* [outer, inner, total] */
    chunk_write_opcode(chunk, OP_CONST, 5);
    chunk_write_byte(chunk, 0, 5);
    chunk_write_byte(chunk, 3, 5);          /* [outer, inner, total, 1] */
    chunk_write_opcode(chunk, OP_ADD, 5);   /* [outer, inner, total+1] */
    chunk_write_opcode(chunk, OP_SWAP, 5);  /* [outer, total+1, inner] */

    /* inner = inner - 1 */
    chunk_write_opcode(chunk, OP_CONST, 5);
    chunk_write_byte(chunk, 0, 5);
    chunk_write_byte(chunk, 3, 5);          /* [outer, total, inner, 1] */
    chunk_write_opcode(chunk, OP_SUB, 5);   /* [outer, total, inner-1] */

    /* Inner loop back */
    uint16_t inner_offset = (uint16_t)(chunk->code_size + 3 - inner_loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 6);
    chunk_write_byte(chunk, (inner_offset >> 8) & 0xFF, 6);
    chunk_write_byte(chunk, inner_offset & 0xFF, 6);

    /* Inner loop exit */
    chunk_patch_jump(chunk, inner_exit);
    chunk_write_opcode(chunk, OP_POP, 7);   /* pop inner loop comparison */
    chunk_write_opcode(chunk, OP_POP, 7);   /* pop inner counter */
    /* Stack: [outer, total] */

    /* outer = outer - 1: swap to get outer on top */
    chunk_write_opcode(chunk, OP_SWAP, 7);  /* [total, outer] */
    chunk_write_opcode(chunk, OP_CONST, 7);
    chunk_write_byte(chunk, 0, 7);
    chunk_write_byte(chunk, 3, 7);          /* [total, outer, 1] */
    chunk_write_opcode(chunk, OP_SUB, 7);   /* [total, outer-1] */
    chunk_write_opcode(chunk, OP_SWAP, 7);  /* [outer-1, total] */

    /* Outer loop back */
    uint16_t outer_offset = (uint16_t)(chunk->code_size + 3 - outer_loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 8);
    chunk_write_byte(chunk, (outer_offset >> 8) & 0xFF, 8);
    chunk_write_byte(chunk, outer_offset & 0xFF, 8);

    /* Outer loop exit */
    chunk_patch_jump(chunk, outer_exit);
    chunk_write_opcode(chunk, OP_POP, 9);   /* pop outer loop comparison */
    chunk_write_opcode(chunk, OP_SWAP, 9);  /* [total, outer] */
    chunk_write_opcode(chunk, OP_POP, 9);   /* [total] */
    chunk_write_opcode(chunk, OP_HALT, 9);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(6, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Break/continue semantics (simulated with jumps)
 * =============================================================================
 */

void test_break_from_loop(void) {
    /* Test: Loop with break when counter reaches specific value
     *
     * counter = 5
     * while counter > 0:
     *   if counter == 3:
     *     break
     *   counter = counter - 1
     * Result: counter should be 3
     */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(5));  /* initial */
    chunk_add_constant(chunk, value_int(3));  /* break value */
    chunk_add_constant(chunk, value_int(0));  /* comparison */
    chunk_add_constant(chunk, value_int(1));  /* decrement */

    /* Push counter = 5 */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Loop start */
    size_t loop_start = chunk->code_size;

    /* Check counter > 0 */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_GT, 2);

    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 2);
    chunk_write_opcode(chunk, OP_POP, 3);

    /* Check if counter == 3 (break condition) */
    chunk_write_opcode(chunk, OP_DUP, 3);
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_EQ, 3);

    size_t break_jump = chunk_write_jump(chunk, OP_JUMP_IF, 3);
    chunk_write_opcode(chunk, OP_POP, 4);

    /* Decrement counter */
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, 0, 4);
    chunk_write_byte(chunk, 3, 4);
    chunk_write_opcode(chunk, OP_SUB, 4);

    /* Loop back */
    uint16_t loop_offset = (uint16_t)(chunk->code_size + 3 - loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 5);
    chunk_write_byte(chunk, (loop_offset >> 8) & 0xFF, 5);
    chunk_write_byte(chunk, loop_offset & 0xFF, 5);

    /* Break target - pop the true from break condition check */
    chunk_patch_jump(chunk, break_jump);
    chunk_write_opcode(chunk, OP_POP, 6);
    size_t to_end = chunk_write_jump(chunk, OP_JUMP, 6);

    /* Normal exit */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 7);

    chunk_patch_jump(chunk, to_end);
    chunk_write_opcode(chunk, OP_HALT, 7);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(3, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_continue_in_loop(void) {
    /* Test: Loop with continue - verifies continue jumps back to loop start
     *
     * Simpler test: just verify continue works by counting iterations
     * Uses a single counter that decrements, with continue when counter == 3
     *
     * counter = 5
     * iterations = 0
     * while counter > 0:
     *   counter = counter - 1
     *   iterations = iterations + 1
     *   if counter == 2:
     *     continue  (skip to loop check, no extra work)
     * Result: iterations should be 5 (loop runs 5 times)
     *
     * Note: In this simplified test, continue just means jump back to loop
     * condition. The effect is the same as not having continue, which is
     * correct - continue doesn't skip iterations, it just skips the rest
     * of the loop body.
     */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(5));  /* initial counter */
    chunk_add_constant(chunk, value_int(0));  /* comparison / iterations init */
    chunk_add_constant(chunk, value_int(1));  /* decrement/increment */
    chunk_add_constant(chunk, value_int(2));  /* continue value */

    /* Stack: [counter] (we just count iterations by final counter value) */

    /* Push counter = 5 */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* Loop start */
    size_t loop_start = chunk->code_size;

    /* Check counter > 0 */
    chunk_write_opcode(chunk, OP_DUP, 2);    /* [counter, counter] */
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 1, 2);           /* [counter, counter, 0] */
    chunk_write_opcode(chunk, OP_GT, 2);     /* [counter, bool] */

    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 2);
    chunk_write_opcode(chunk, OP_POP, 3);    /* [counter] */

    /* Decrement counter: counter = counter - 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 2, 3);           /* [counter, 1] */
    chunk_write_opcode(chunk, OP_SUB, 3);    /* [counter-1] */

    /* Check if counter == 2 (continue condition) */
    chunk_write_opcode(chunk, OP_DUP, 4);    /* [counter, counter] */
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, 0, 4);
    chunk_write_byte(chunk, 3, 4);           /* [counter, counter, 2] */
    chunk_write_opcode(chunk, OP_EQ, 4);     /* [counter, bool] */

    size_t continue_jump = chunk_write_jump(chunk, OP_JUMP_IF, 4);
    chunk_write_opcode(chunk, OP_POP, 5);    /* [counter] */

    /* Normal loop back */
    uint16_t loop_offset = (uint16_t)(chunk->code_size + 3 - loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 6);
    chunk_write_byte(chunk, (loop_offset >> 8) & 0xFF, 6);
    chunk_write_byte(chunk, loop_offset & 0xFF, 6);

    /* Continue target - pop the true, jump to loop start */
    chunk_patch_jump(chunk, continue_jump);
    chunk_write_opcode(chunk, OP_POP, 7);   /* pop true: [counter] */
    uint16_t cont_offset = (uint16_t)(chunk->code_size + 3 - loop_start);
    chunk_write_opcode(chunk, OP_LOOP, 7);
    chunk_write_byte(chunk, (cont_offset >> 8) & 0xFF, 7);
    chunk_write_byte(chunk, cont_offset & 0xFF, 7);

    /* Normal exit */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 8);   /* pop false: [counter] */
    chunk_write_opcode(chunk, OP_HALT, 8);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* Counter should be 0 after 5 decrements */
    ASSERT_EQ(0, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Edge cases
 * =============================================================================
 */

void test_jump_preserves_stack(void) {
    /* Test: Jump doesn't modify stack values */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(2));
    chunk_add_constant(chunk, value_int(3));

    /* Push values */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 1, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 2, 1);

    /* Jump over nothing */
    size_t jump = chunk_write_jump(chunk, OP_JUMP, 2);
    chunk_patch_jump(chunk, jump);

    chunk_write_opcode(chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(3, vm_peek(vm, 0)->as.integer);
    ASSERT_EQ(2, vm_peek(vm, 1)->as.integer);
    ASSERT_EQ(1, vm_peek(vm, 2)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_conditional_jump_preserves_condition(void) {
    /* Test: JUMP_IF/JUMP_UNLESS don't pop the condition */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_TRUE, 1);
    size_t jump = chunk_write_jump(chunk, OP_JUMP_IF, 1);
    chunk_patch_jump(chunk, jump);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* True should still be on stack */
    ASSERT(vm_peek(vm, 0)->as.boolean == true);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_if_out_of_bounds(void) {
    /* Test: JUMP_IF with out of bounds offset */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_TRUE, 1);
    chunk_write_opcode(chunk, OP_JUMP_IF, 1);
    chunk_write_byte(chunk, 0xFF, 1);
    chunk_write_byte(chunk, 0xFF, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_RUNTIME, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_jump_unless_out_of_bounds(void) {
    /* Test: JUMP_UNLESS with out of bounds offset */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_FALSE, 1);
    chunk_write_opcode(chunk, OP_JUMP_UNLESS, 1);
    chunk_write_byte(chunk, 0xFF, 1);
    chunk_write_byte(chunk, 0xFF, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_RUNTIME, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Main test runner
 * =============================================================================
 */

int main(void) {
    printf("=== VM Control Flow Tests (P1.1.1.4) ===\n\n");

    printf("--- OP_JUMP forward tests ---\n");
    RUN_TEST(test_jump_forward_basic);
    RUN_TEST(test_jump_forward_multiple_instructions);
    RUN_TEST(test_jump_forward_zero_offset);
    RUN_TEST(test_jump_forward_chained);

    printf("\n--- OP_JUMP to end of code tests ---\n");
    RUN_TEST(test_jump_to_halt);
    RUN_TEST(test_jump_out_of_bounds);

    printf("\n--- OP_JUMP_IF truthy tests ---\n");
    RUN_TEST(test_jump_if_true_boolean);
    RUN_TEST(test_jump_if_nonzero_int);
    RUN_TEST(test_jump_if_negative_int);
    RUN_TEST(test_jump_if_nonzero_float);
    RUN_TEST(test_jump_if_string);

    printf("\n--- OP_JUMP_IF falsy tests ---\n");
    RUN_TEST(test_jump_if_false_boolean);
    RUN_TEST(test_jump_if_nil);
    RUN_TEST(test_jump_if_zero_int);
    RUN_TEST(test_jump_if_zero_float);

    printf("\n--- OP_JUMP_UNLESS tests ---\n");
    RUN_TEST(test_jump_unless_false);
    RUN_TEST(test_jump_unless_true);
    RUN_TEST(test_jump_unless_nil);
    RUN_TEST(test_jump_unless_zero);
    RUN_TEST(test_jump_unless_nonzero);

    printf("\n--- OP_LOOP tests ---\n");
    RUN_TEST(test_loop_basic);
    RUN_TEST(test_loop_never_executes);
    RUN_TEST(test_loop_once);
    RUN_TEST(test_loop_backward_bounds_check);
    RUN_TEST(test_loop_many_iterations);

    printf("\n--- Nested loop tests ---\n");
    RUN_TEST(test_nested_loops_simple);

    printf("\n--- Break/continue tests ---\n");
    RUN_TEST(test_break_from_loop);
    RUN_TEST(test_continue_in_loop);

    printf("\n--- Edge case tests ---\n");
    RUN_TEST(test_jump_preserves_stack);
    RUN_TEST(test_conditional_jump_preserves_condition);
    RUN_TEST(test_jump_if_out_of_bounds);
    RUN_TEST(test_jump_unless_out_of_bounds);

    return TEST_RESULT();
}
