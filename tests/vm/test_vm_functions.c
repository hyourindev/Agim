/*
 * Agim - VM Function Call Tests
 *
 * P1.1.1.5 - Comprehensive tests for all function call operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "types/closure.h"

/* Helper functions for emitting bytecode */

static void emit_const(Chunk *chunk, size_t index, int line) {
    chunk_write_opcode(chunk, OP_CONST, line);
    chunk_write_byte(chunk, (index >> 8) & 0xFF, line);
    chunk_write_byte(chunk, index & 0xFF, line);
}

static void emit_get_local(Chunk *chunk, uint16_t slot, int line) {
    chunk_write_opcode(chunk, OP_GET_LOCAL, line);
    chunk_write_byte(chunk, (slot >> 8) & 0xFF, line);
    chunk_write_byte(chunk, slot & 0xFF, line);
}

static void emit_call(Chunk *chunk, uint16_t arity, int line) {
    chunk_write_opcode(chunk, OP_CALL, line);
    chunk_write_byte(chunk, (arity >> 8) & 0xFF, line);
    chunk_write_byte(chunk, arity & 0xFF, line);
}

/*
 * =============================================================================
 * Test: OP_CALL with 0 arguments
 * =============================================================================
 */

void test_call_zero_args(void) {
    /* Test: Call a function with no arguments */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create function: returns 42 */
    Chunk *func = chunk_new();
    size_t c_42 = chunk_add_constant(func, value_int(42));
    emit_const(func, c_42, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: call func() */
    Value *func_val = value_function("get_answer", 0);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);

    emit_const(main_chunk, c_func, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_call_zero_args_returns_nil(void) {
    /* Test: Function that returns nil */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create function: returns nil */
    Chunk *func = chunk_new();
    chunk_write_opcode(func, OP_NIL, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: call func() */
    Value *func_val = value_function("get_nil", 0);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);

    emit_const(main_chunk, c_func, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT(value_is_nil(vm_peek(vm, 0)));

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_CALL with various argument counts
 * =============================================================================
 */

void test_call_one_arg(void) {
    /* Test: Call function with 1 argument: identity(x) = x */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create identity function */
    Chunk *func = chunk_new();
    emit_get_local(func, 1, 1);  /* slot 0 is function, slot 1 is arg */
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: call identity(99) */
    Value *func_val = value_function("identity", 1);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_arg = chunk_add_constant(main_chunk, value_int(99));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_arg, 1);
    emit_call(main_chunk, 1, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(99, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_call_two_args(void) {
    /* Test: Call function with 2 arguments: add(a, b) = a + b */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create add function */
    Chunk *func = chunk_new();
    emit_get_local(func, 1, 1);  /* arg 1 */
    emit_get_local(func, 2, 1);  /* arg 2 */
    chunk_write_opcode(func, OP_ADD, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: call add(10, 32) */
    Value *func_val = value_function("add", 2);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_a = chunk_add_constant(main_chunk, value_int(10));
    size_t c_b = chunk_add_constant(main_chunk, value_int(32));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_a, 1);
    emit_const(main_chunk, c_b, 1);
    emit_call(main_chunk, 2, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_call_five_args(void) {
    /* Test: Call function with 5 arguments: sum(a,b,c,d,e) */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create sum function: a+b+c+d+e */
    Chunk *func = chunk_new();
    emit_get_local(func, 1, 1);
    emit_get_local(func, 2, 1);
    chunk_write_opcode(func, OP_ADD, 1);
    emit_get_local(func, 3, 1);
    chunk_write_opcode(func, OP_ADD, 1);
    emit_get_local(func, 4, 1);
    chunk_write_opcode(func, OP_ADD, 1);
    emit_get_local(func, 5, 1);
    chunk_write_opcode(func, OP_ADD, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: call sum(1, 2, 3, 4, 5) */
    Value *func_val = value_function("sum", 5);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_1 = chunk_add_constant(main_chunk, value_int(1));
    size_t c_2 = chunk_add_constant(main_chunk, value_int(2));
    size_t c_3 = chunk_add_constant(main_chunk, value_int(3));
    size_t c_4 = chunk_add_constant(main_chunk, value_int(4));
    size_t c_5 = chunk_add_constant(main_chunk, value_int(5));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_1, 1);
    emit_const(main_chunk, c_2, 1);
    emit_const(main_chunk, c_3, 1);
    emit_const(main_chunk, c_4, 1);
    emit_const(main_chunk, c_5, 1);
    emit_call(main_chunk, 5, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(15, vm_peek(vm, 0)->as.integer);  /* 1+2+3+4+5 = 15 */

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_CALL argument count mismatch
 * =============================================================================
 */

void test_call_wrong_arity_too_few(void) {
    /* Test: Call with fewer args than expected - should error */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create function expecting 2 args */
    Chunk *func = chunk_new();
    emit_get_local(func, 1, 1);
    emit_get_local(func, 2, 1);
    chunk_write_opcode(func, OP_ADD, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: call add with only 1 arg */
    Value *func_val = value_function("add", 2);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_arg = chunk_add_constant(main_chunk, value_int(10));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_arg, 1);
    emit_call(main_chunk, 1, 1);  /* Only 1 arg, but function expects 2 */
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_ARITY, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_call_wrong_arity_too_many(void) {
    /* Test: Call with more args than expected - should error */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create function expecting 1 arg */
    Chunk *func = chunk_new();
    emit_get_local(func, 1, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: call with 2 args */
    Value *func_val = value_function("identity", 1);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_a = chunk_add_constant(main_chunk, value_int(10));
    size_t c_b = chunk_add_constant(main_chunk, value_int(20));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_a, 1);
    emit_const(main_chunk, c_b, 1);
    emit_call(main_chunk, 2, 1);  /* 2 args, but function expects 1 */
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_ARITY, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_CALL with non-callable values
 * =============================================================================
 */

void test_call_non_function_int(void) {
    /* Test: Attempting to call an integer should error */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    size_t c_int = chunk_add_constant(main_chunk, value_int(42));

    emit_const(main_chunk, c_int, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_call_non_function_string(void) {
    /* Test: Attempting to call a string should error */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    size_t c_str = chunk_add_constant(main_chunk, value_string("hello"));

    emit_const(main_chunk, c_str, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_call_nil(void) {
    /* Test: Attempting to call nil should error */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    chunk_write_opcode(main_chunk, OP_NIL, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_CALL stack frame setup
 * =============================================================================
 */

void test_call_preserves_caller_stack(void) {
    /* Test: Values on caller's stack are preserved across call */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create function: returns 999 */
    Chunk *func = chunk_new();
    size_t c_ret = chunk_add_constant(func, value_int(999));
    emit_const(func, c_ret, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: push 42, call func(), result and 42 should both be accessible */
    Value *func_val = value_function("func", 0);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_42 = chunk_add_constant(main_chunk, value_int(42));

    emit_const(main_chunk, c_42, 1);    /* Push 42 */
    emit_const(main_chunk, c_func, 1);  /* Push function */
    emit_call(main_chunk, 0, 1);        /* Call, result on stack */
    /* Stack should be: [42, 999] */
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(999, vm_peek(vm, 0)->as.integer);  /* Top: function result */
    ASSERT_EQ(42, vm_peek(vm, 1)->as.integer);   /* Below: original value */

    vm_free(vm);
    bytecode_free(code);
}

void test_call_nested_functions(void) {
    /* Test: inner(x) = x * 2, outer(x) = inner(x) + 10 */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create inner function: x * 2 */
    Chunk *inner_func = chunk_new();
    size_t c_two_i = chunk_add_constant(inner_func, value_int(2));
    emit_get_local(inner_func, 1, 1);
    emit_const(inner_func, c_two_i, 1);
    chunk_write_opcode(inner_func, OP_MUL, 1);
    chunk_write_opcode(inner_func, OP_RETURN, 1);
    size_t inner_index = bytecode_add_function(code, inner_func);

    /* Create outer function: inner(x) + 10 */
    Chunk *outer_func = chunk_new();
    Value *inner_val = value_function("inner", 1);
    inner_val->as.function->code_offset = inner_index;
    size_t c_inner = chunk_add_constant(outer_func, inner_val);
    size_t c_ten = chunk_add_constant(outer_func, value_int(10));

    emit_const(outer_func, c_inner, 1);    /* Push inner function */
    emit_get_local(outer_func, 1, 1);      /* Push x */
    emit_call(outer_func, 1, 1);           /* Call inner(x) */
    emit_const(outer_func, c_ten, 1);      /* Push 10 */
    chunk_write_opcode(outer_func, OP_ADD, 1);
    chunk_write_opcode(outer_func, OP_RETURN, 1);
    size_t outer_index = bytecode_add_function(code, outer_func);

    /* Main: outer(5) should be 5*2 + 10 = 20 */
    Value *outer_val = value_function("outer", 1);
    outer_val->as.function->code_offset = outer_index;
    size_t c_outer = chunk_add_constant(main_chunk, outer_val);
    size_t c_five = chunk_add_constant(main_chunk, value_int(5));

    emit_const(main_chunk, c_outer, 1);
    emit_const(main_chunk, c_five, 1);
    emit_call(main_chunk, 1, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(20, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_RET value propagation
 * =============================================================================
 */

void test_return_int(void) {
    /* Test: Return an integer value */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    Chunk *func = chunk_new();
    size_t c_val = chunk_add_constant(func, value_int(12345));
    emit_const(func, c_val, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    Value *func_val = value_function("get_num", 0);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);

    emit_const(main_chunk, c_func, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(12345, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_return_string(void) {
    /* Test: Return a string value */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    Chunk *func = chunk_new();
    size_t c_val = chunk_add_constant(func, value_string("hello world"));
    emit_const(func, c_val, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    Value *func_val = value_function("get_str", 0);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);

    emit_const(main_chunk, c_func, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_STR_EQ("hello world", vm_peek(vm, 0)->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

void test_return_bool(void) {
    /* Test: Return a boolean value */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    Chunk *func = chunk_new();
    chunk_write_opcode(func, OP_TRUE, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    Value *func_val = value_function("get_bool", 0);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);

    emit_const(main_chunk, c_func, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT(vm_peek(vm, 0)->as.boolean == true);

    vm_free(vm);
    bytecode_free(code);
}

void test_return_computed_value(void) {
    /* Test: Return a computed value (arg * 2 + 1) */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    Chunk *func = chunk_new();
    size_t c_two = chunk_add_constant(func, value_int(2));
    size_t c_one = chunk_add_constant(func, value_int(1));
    emit_get_local(func, 1, 1);       /* Get arg */
    emit_const(func, c_two, 1);       /* Push 2 */
    chunk_write_opcode(func, OP_MUL, 1);  /* arg * 2 */
    emit_const(func, c_one, 1);       /* Push 1 */
    chunk_write_opcode(func, OP_ADD, 1);  /* (arg * 2) + 1 */
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    Value *func_val = value_function("compute", 1);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_arg = chunk_add_constant(main_chunk, value_int(7));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_arg, 1);
    emit_call(main_chunk, 1, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(15, vm_peek(vm, 0)->as.integer);  /* 7*2 + 1 = 15 */

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_RET void functions (implicit nil return)
 * =============================================================================
 */

void test_return_void_explicit_nil(void) {
    /* Test: Function explicitly returns nil */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    Chunk *func = chunk_new();
    chunk_write_opcode(func, OP_NIL, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    Value *func_val = value_function("void_func", 0);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);

    emit_const(main_chunk, c_func, 1);
    emit_call(main_chunk, 0, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT(value_is_nil(vm_peek(vm, 0)));

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Recursive calls
 * =============================================================================
 */

void test_recursive_factorial(void) {
    /* Test: factorial(5) = 120 */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create recursive factorial function */
    Chunk *fact_func = chunk_new();
    size_t c_one = chunk_add_constant(fact_func, value_int(1));

    /* if n <= 1 return 1 */
    emit_get_local(fact_func, 1, 1);      /* Push n */
    emit_const(fact_func, c_one, 1);      /* Push 1 */
    chunk_write_opcode(fact_func, OP_LE, 1);

    size_t else_jump = chunk_write_jump(fact_func, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(fact_func, OP_POP, 2);

    /* Return 1 */
    emit_const(fact_func, c_one, 2);
    chunk_write_opcode(fact_func, OP_RETURN, 2);

    /* Else: return n * factorial(n-1) */
    chunk_patch_jump(fact_func, else_jump);
    chunk_write_opcode(fact_func, OP_POP, 3);

    /* We need to reference the function itself for recursion */
    /* Get function from slot 0, get n from slot 1 */
    emit_get_local(fact_func, 1, 3);      /* Push n */
    emit_get_local(fact_func, 0, 3);      /* Push function */
    emit_get_local(fact_func, 1, 3);      /* Push n again */
    emit_const(fact_func, c_one, 3);      /* Push 1 */
    chunk_write_opcode(fact_func, OP_SUB, 3);  /* n - 1 */
    emit_call(fact_func, 1, 3);           /* factorial(n-1) */
    chunk_write_opcode(fact_func, OP_MUL, 3);  /* n * factorial(n-1) */
    chunk_write_opcode(fact_func, OP_RETURN, 3);

    size_t func_index = bytecode_add_function(code, fact_func);

    /* Main: call factorial(5) */
    Value *func_val = value_function("factorial", 1);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_five = chunk_add_constant(main_chunk, value_int(5));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_five, 1);
    emit_call(main_chunk, 1, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm->reduction_limit = 1000000;
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(120, vm_peek(vm, 0)->as.integer);  /* 5! = 120 */

    vm_free(vm);
    bytecode_free(code);
}

void test_recursive_fibonacci(void) {
    /* Test: fib(10) = 55 */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create recursive fib function */
    Chunk *fib_func = chunk_new();
    (void)chunk_add_constant(fib_func, value_int(0));  /* unused but keeps indices consistent */
    size_t c_one = chunk_add_constant(fib_func, value_int(1));
    size_t c_two = chunk_add_constant(fib_func, value_int(2));

    /* if n < 2 return n */
    emit_get_local(fib_func, 1, 1);
    emit_const(fib_func, c_two, 1);
    chunk_write_opcode(fib_func, OP_LT, 1);

    size_t else_jump = chunk_write_jump(fib_func, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(fib_func, OP_POP, 2);

    /* Return n */
    emit_get_local(fib_func, 1, 2);
    chunk_write_opcode(fib_func, OP_RETURN, 2);

    /* Else: fib(n-1) + fib(n-2) */
    chunk_patch_jump(fib_func, else_jump);
    chunk_write_opcode(fib_func, OP_POP, 3);

    /* fib(n-1) */
    emit_get_local(fib_func, 0, 3);
    emit_get_local(fib_func, 1, 3);
    emit_const(fib_func, c_one, 3);
    chunk_write_opcode(fib_func, OP_SUB, 3);
    emit_call(fib_func, 1, 3);

    /* fib(n-2) */
    emit_get_local(fib_func, 0, 3);
    emit_get_local(fib_func, 1, 3);
    emit_const(fib_func, c_two, 3);
    chunk_write_opcode(fib_func, OP_SUB, 3);
    emit_call(fib_func, 1, 3);

    chunk_write_opcode(fib_func, OP_ADD, 3);
    chunk_write_opcode(fib_func, OP_RETURN, 3);

    size_t func_index = bytecode_add_function(code, fib_func);

    /* Main: call fib(10) */
    Value *func_val = value_function("fib", 1);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_ten = chunk_add_constant(main_chunk, value_int(10));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_ten, 1);
    emit_call(main_chunk, 1, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm->reduction_limit = 10000000;  /* fib(10) needs many calls */
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(55, vm_peek(vm, 0)->as.integer);  /* fib(10) = 55 */

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Deep recursion (stack overflow detection)
 * =============================================================================
 */

void test_recursive_deep_no_overflow(void) {
    /* Test: Simple recursive countdown that uses the result
     * countdown(n) = if n <= 0 then 0 else 1 + countdown(n-1)
     * This effectively counts the recursion depth
     */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    Chunk *func = chunk_new();
    size_t c_zero = chunk_add_constant(func, value_int(0));
    size_t c_one = chunk_add_constant(func, value_int(1));

    /* if n <= 0 return 0 */
    emit_get_local(func, 1, 1);
    emit_const(func, c_zero, 1);
    chunk_write_opcode(func, OP_LE, 1);

    size_t else_jump = chunk_write_jump(func, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(func, OP_POP, 2);

    /* Return 0 */
    emit_const(func, c_zero, 2);
    chunk_write_opcode(func, OP_RETURN, 2);

    /* Else: return 1 + countdown(n-1) */
    chunk_patch_jump(func, else_jump);
    chunk_write_opcode(func, OP_POP, 3);

    /* Push 1 first, then call, then add */
    emit_const(func, c_one, 3);          /* Push 1 */
    emit_get_local(func, 0, 3);          /* Push function for self-call */
    emit_get_local(func, 1, 3);          /* Push n */
    emit_const(func, c_one, 3);          /* Push 1 */
    chunk_write_opcode(func, OP_SUB, 3); /* n - 1 */
    emit_call(func, 1, 3);               /* countdown(n-1) */
    chunk_write_opcode(func, OP_ADD, 3); /* 1 + result */
    chunk_write_opcode(func, OP_RETURN, 3);

    size_t func_index = bytecode_add_function(code, func);

    /* Main: countdown(10) should return 10 */
    Value *func_val = value_function("countdown", 1);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);
    size_t c_start = chunk_add_constant(main_chunk, value_int(10));

    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_start, 1);
    emit_call(main_chunk, 1, 1);
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm->reduction_limit = 10000000;
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(10, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Multiple return values via stack manipulation
 * =============================================================================
 */

void test_multiple_calls_sequential(void) {
    /* Test: Call multiple functions sequentially */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* double(x) = x * 2 */
    Chunk *double_func = chunk_new();
    size_t c_two = chunk_add_constant(double_func, value_int(2));
    emit_get_local(double_func, 1, 1);
    emit_const(double_func, c_two, 1);
    chunk_write_opcode(double_func, OP_MUL, 1);
    chunk_write_opcode(double_func, OP_RETURN, 1);
    size_t double_index = bytecode_add_function(code, double_func);

    /* Main: double(double(5)) = 20 */
    Value *double_val = value_function("double", 1);
    double_val->as.function->code_offset = double_index;
    size_t c_func = chunk_add_constant(main_chunk, double_val);
    size_t c_five = chunk_add_constant(main_chunk, value_int(5));

    /* First call: double(5) = 10 */
    emit_const(main_chunk, c_func, 1);
    emit_const(main_chunk, c_five, 1);
    emit_call(main_chunk, 1, 1);

    /* Second call: double(10) = 20 */
    emit_const(main_chunk, c_func, 2);
    chunk_write_opcode(main_chunk, OP_SWAP, 2);
    emit_call(main_chunk, 1, 2);

    chunk_write_opcode(main_chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(20, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Closure basic functionality
 * =============================================================================
 */

void test_closure_basic(void) {
    /* Test: Create and call a simple closure */
    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create a simple function that will be wrapped as closure */
    Chunk *func = chunk_new();
    size_t c_val = chunk_add_constant(func, value_int(42));
    emit_const(func, c_val, 1);
    chunk_write_opcode(func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, func);

    /* Create closure with 0 upvalues */
    chunk_write_opcode(main_chunk, OP_CLOSURE, 1);
    chunk_write_byte(main_chunk, (func_index >> 8) & 0xFF, 1);
    chunk_write_byte(main_chunk, func_index & 0xFF, 1);
    chunk_write_byte(main_chunk, 0, 1);  /* 0 upvalues */

    emit_call(main_chunk, 0, 2);
    chunk_write_opcode(main_chunk, OP_HALT, 3);

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
 * Main test runner
 * =============================================================================
 */

int main(void) {
    printf("=== VM Function Call Tests (P1.1.1.5) ===\n\n");

    printf("--- OP_CALL with 0 arguments tests ---\n");
    RUN_TEST(test_call_zero_args);
    RUN_TEST(test_call_zero_args_returns_nil);

    printf("\n--- OP_CALL with various argument counts ---\n");
    RUN_TEST(test_call_one_arg);
    RUN_TEST(test_call_two_args);
    RUN_TEST(test_call_five_args);

    printf("\n--- OP_CALL argument count mismatch ---\n");
    RUN_TEST(test_call_wrong_arity_too_few);
    RUN_TEST(test_call_wrong_arity_too_many);

    printf("\n--- OP_CALL with non-callable values ---\n");
    RUN_TEST(test_call_non_function_int);
    RUN_TEST(test_call_non_function_string);
    RUN_TEST(test_call_nil);

    printf("\n--- OP_CALL stack frame setup ---\n");
    RUN_TEST(test_call_preserves_caller_stack);
    RUN_TEST(test_call_nested_functions);

    printf("\n--- OP_RET value propagation ---\n");
    RUN_TEST(test_return_int);
    RUN_TEST(test_return_string);
    RUN_TEST(test_return_bool);
    RUN_TEST(test_return_computed_value);

    printf("\n--- OP_RET void functions ---\n");
    RUN_TEST(test_return_void_explicit_nil);

    printf("\n--- Recursive calls ---\n");
    RUN_TEST(test_recursive_factorial);
    RUN_TEST(test_recursive_fibonacci);
    RUN_TEST(test_recursive_deep_no_overflow);

    printf("\n--- Multiple calls ---\n");
    RUN_TEST(test_multiple_calls_sequential);

    printf("\n--- Closure tests ---\n");
    RUN_TEST(test_closure_basic);

    return TEST_RESULT();
}
