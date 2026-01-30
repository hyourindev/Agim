/*
 * Agim - VM Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/bytecode.h"
#include "vm/vm.h"

void test_vm_create(void) {
    VM *vm = vm_new();
    ASSERT(vm != NULL);
    vm_free(vm);
}

void test_vm_stack(void) {
    VM *vm = vm_new();

    vm_push(vm, value_int(1));
    vm_push(vm, value_int(2));
    vm_push(vm, value_int(3));

    ASSERT_EQ(3, vm_peek(vm, 0)->as.integer);
    ASSERT_EQ(2, vm_peek(vm, 1)->as.integer);
    ASSERT_EQ(1, vm_peek(vm, 2)->as.integer);

    Value *v = vm_pop(vm);
    ASSERT_EQ(3, v->as.integer);

    vm_free(vm);
}

void test_vm_arithmetic(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* 10 + 20 = 30 */
    chunk_add_constant(chunk, value_int(10));
    chunk_add_constant(chunk, value_int(20));

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 1, 1);

    chunk_write_opcode(chunk, OP_ADD, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(30, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_vm_comparison(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* 5 < 10 = true */
    chunk_add_constant(chunk, value_int(5));
    chunk_add_constant(chunk, value_int(10));

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 1, 1);

    chunk_write_opcode(chunk, OP_LT, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT(vm_peek(vm, 0)->as.boolean == true);

    vm_free(vm);
    bytecode_free(code);
}

void test_vm_jump(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* if (true) push 42 else push 0 */
    chunk_add_constant(chunk, value_int(42));
    chunk_add_constant(chunk, value_int(0));

    chunk_write_opcode(chunk, OP_TRUE, 1);
    size_t else_jump = chunk_write_jump(chunk, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(chunk, OP_POP, 1);

    /* Then branch: push 42 */
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 0, 2);
    size_t end_jump = chunk_write_jump(chunk, OP_JUMP, 2);

    /* Else branch: push 0 */
    chunk_patch_jump(chunk, else_jump);
    chunk_write_opcode(chunk, OP_POP, 3);
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);

    chunk_patch_jump(chunk, end_jump);
    chunk_write_opcode(chunk, OP_HALT, 4);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_vm_array(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_int(10));
    chunk_add_constant(chunk, value_int(20));
    chunk_add_constant(chunk, value_int(0));

    /* Create array and push elements */
    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 1, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);

    /* Get element at index 0 */
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_ARRAY_GET, 2);

    chunk_write_opcode(chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(10, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_vm_string_concat(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, value_string("hello"));
    chunk_add_constant(chunk, value_string(" world"));

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 1, 1);

    chunk_write_opcode(chunk, OP_ADD, 1); /* String concat */
    chunk_write_opcode(chunk, OP_HALT, 1);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_STR_EQ("hello world", vm_peek(vm, 0)->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

int main(void) {
    RUN_TEST(test_vm_create);
    RUN_TEST(test_vm_stack);
    RUN_TEST(test_vm_arithmetic);
    RUN_TEST(test_vm_comparison);
    RUN_TEST(test_vm_jump);
    RUN_TEST(test_vm_array);
    RUN_TEST(test_vm_string_concat);

    return TEST_RESULT();
}
