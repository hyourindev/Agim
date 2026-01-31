/*
 * Agim - Integration Tests
 *
 * Real programs exercising VM features end-to-end.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "types/array.h"
#include "types/map.h"
#include "types/string.h"

/* Helpers */
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

static void emit_set_local(Chunk *chunk, uint16_t slot, int line) {
    chunk_write_opcode(chunk, OP_SET_LOCAL, line);
    chunk_write_byte(chunk, (slot >> 8) & 0xFF, line);
    chunk_write_byte(chunk, slot & 0xFF, line);
}

static void emit_call(Chunk *chunk, uint16_t arity, int line) {
    chunk_write_opcode(chunk, OP_CALL, line);
    chunk_write_byte(chunk, (arity >> 8) & 0xFF, line);
    chunk_write_byte(chunk, arity & 0xFF, line);
}

/* Test: Countdown loop */
void test_countdown_loop(void) {
    printf("  Program: countdown from 1000 to 0\n");

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Constants */
    size_t c_start = chunk_add_constant(chunk, value_int(1000));  /* 0: start value */
    size_t c_one = chunk_add_constant(chunk, value_int(1));       /* 1: decrement */
    size_t c_zero = chunk_add_constant(chunk, value_int(0));      /* 2: comparison */

    /*
     * Equivalent code:
     *   let i = 1000
     *   while i > 0:
     *     i = i - 1
     *   return i  (should be 0)
     */

    /* Line 1: Push initial value (local slot 0) */
    emit_const(chunk, c_start, 1);      /* stack: [1000] */

    /* Line 2: Loop start - check condition */
    size_t loop_start = chunk->code_size;
    chunk_write_opcode(chunk, OP_DUP, 2);          /* stack: [i, i] */
    emit_const(chunk, c_zero, 2);                  /* stack: [i, i, 0] */
    chunk_write_opcode(chunk, OP_LE, 2);           /* stack: [i, i<=0] */
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);
    chunk_write_opcode(chunk, OP_POP, 2);          /* stack: [i] */

    /* Line 3: Decrement */
    emit_const(chunk, c_one, 3);                   /* stack: [i, 1] */
    chunk_write_opcode(chunk, OP_SUB, 3);          /* stack: [i-1] */

    /* Line 4: Loop back */
    chunk_write_opcode(chunk, OP_LOOP, 4);
    uint16_t loop_offset = (uint16_t)(chunk->code_size - loop_start + 2);
    chunk_write_byte(chunk, (loop_offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, loop_offset & 0xFF, 4);

    /* Exit */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);          /* Pop the condition result */
    chunk_write_opcode(chunk, OP_HALT, 5);

    /* Run */
    VM *vm = vm_new();
    vm->reduction_limit = 1000000;  /* Allow enough iterations */
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(0, vm_peek(vm, 0)->as.integer);

    printf("    Result: i = %ld (expected 0)\n", vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/* Test: Function call and return */
void test_function_call(void) {
    printf("  Program: function that adds two numbers\n");

    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* Create function chunk: add(a, b) { return a + b } */
    Chunk *add_func = chunk_new();
    emit_get_local(add_func, 1, 1);      /* Get first arg (slot 1, slot 0 is function itself) */
    emit_get_local(add_func, 2, 1);      /* Get second arg */
    chunk_write_opcode(add_func, OP_ADD, 1);
    chunk_write_opcode(add_func, OP_RETURN, 1);

    size_t func_index = bytecode_add_function(code, add_func);

    /* Main: call add(10, 32) */
    /* Push function value */
    Value *func_val = value_function("add", 2);
    func_val->as.function->code_offset = func_index;
    size_t c_func = chunk_add_constant(main_chunk, func_val);

    size_t c_a = chunk_add_constant(main_chunk, value_int(10));
    size_t c_b = chunk_add_constant(main_chunk, value_int(32));

    emit_const(main_chunk, c_func, 1);   /* Push function */
    emit_const(main_chunk, c_a, 1);      /* Push arg 1 */
    emit_const(main_chunk, c_b, 1);      /* Push arg 2 */
    emit_call(main_chunk, 2, 1);         /* Call with 2 args */
    chunk_write_opcode(main_chunk, OP_HALT, 2);

    /* Run */
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    printf("    Result: add(10, 32) = %ld (expected 42)\n", vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/* Test: Recursive factorial */
void test_recursive_factorial(void) {
    printf("  Program: factorial(5) using recursion\n");

    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /*
     * factorial(n):
     *   if n <= 1: return 1
     *   return n * factorial(n - 1)
     */
    Chunk *fact_func = chunk_new();
    size_t c_one_f = chunk_add_constant(fact_func, value_int(1));

    /* if n <= 1 */
    emit_get_local(fact_func, 1, 1);      /* Get n */
    emit_const(fact_func, c_one_f, 1);
    chunk_write_opcode(fact_func, OP_LE, 1);
    size_t else_jump = chunk_write_jump(fact_func, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(fact_func, OP_POP, 1);

    /* return 1 */
    emit_const(fact_func, c_one_f, 2);
    chunk_write_opcode(fact_func, OP_RETURN, 2);

    /* else: return n * factorial(n-1) */
    chunk_patch_jump(fact_func, else_jump);
    chunk_write_opcode(fact_func, OP_POP, 3);

    emit_get_local(fact_func, 1, 3);      /* n */
    emit_get_local(fact_func, 0, 3);      /* factorial function */
    emit_get_local(fact_func, 1, 3);      /* n */
    emit_const(fact_func, c_one_f, 3);    /* 1 */
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

    /* Run */
    VM *vm = vm_new();
    vm->reduction_limit = 1000000;
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(120, vm_peek(vm, 0)->as.integer);  /* 5! = 120 */

    printf("    Result: factorial(5) = %ld (expected 120)\n", vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/* Test: Fibonacci sequence */
void test_fibonacci(void) {
    printf("  Program: fibonacci(10) using recursion\n");

    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /*
     * fib(n):
     *   if n <= 1: return n
     *   return fib(n-1) + fib(n-2)
     */
    Chunk *fib_func = chunk_new();
    size_t c_one_f = chunk_add_constant(fib_func, value_int(1));
    size_t c_two_f = chunk_add_constant(fib_func, value_int(2));

    /* if n <= 1 */
    emit_get_local(fib_func, 1, 1);
    emit_const(fib_func, c_one_f, 1);
    chunk_write_opcode(fib_func, OP_LE, 1);
    size_t else_jump = chunk_write_jump(fib_func, OP_JUMP_UNLESS, 1);
    chunk_write_opcode(fib_func, OP_POP, 1);

    /* return n */
    emit_get_local(fib_func, 1, 2);
    chunk_write_opcode(fib_func, OP_RETURN, 2);

    /* else: fib(n-1) + fib(n-2) */
    chunk_patch_jump(fib_func, else_jump);
    chunk_write_opcode(fib_func, OP_POP, 3);

    /* fib(n-1) */
    emit_get_local(fib_func, 0, 3);       /* fib function */
    emit_get_local(fib_func, 1, 3);       /* n */
    emit_const(fib_func, c_one_f, 3);     /* 1 */
    chunk_write_opcode(fib_func, OP_SUB, 3);
    emit_call(fib_func, 1, 3);

    /* fib(n-2) */
    emit_get_local(fib_func, 0, 3);       /* fib function */
    emit_get_local(fib_func, 1, 3);       /* n */
    emit_const(fib_func, c_two_f, 3);     /* 2 */
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

    /* Run */
    VM *vm = vm_new();
    vm->reduction_limit = 10000000;  /* fib(10) needs many calls */
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(55, vm_peek(vm, 0)->as.integer);  /* fib(10) = 55 */

    printf("    Result: fib(10) = %ld (expected 55)\n", vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/* Test: Build and iterate array */
void test_array_operations(void) {
    printf("  Program: build array [1,2,3,4,5], sum all elements\n");

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Constants */
    size_t c_nums[5];
    for (int i = 0; i < 5; i++) {
        c_nums[i] = chunk_add_constant(chunk, value_int(i + 1));
    }
    size_t c_zero = chunk_add_constant(chunk, value_int(0));
    size_t c_one = chunk_add_constant(chunk, value_int(1));
    size_t c_five = chunk_add_constant(chunk, value_int(5));

    /*
     * arr = [1, 2, 3, 4, 5]
     * sum = 0
     * i = 0
     * while i < 5:
     *   sum = sum + arr[i]
     *   i = i + 1
     * return sum
     *
     * Stack layout: [arr, sum, i]
     */

    /* Create array and push elements */
    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    for (int i = 0; i < 5; i++) {
        emit_const(chunk, c_nums[i], 1);
        chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    }

    /* Initialize sum = 0 */
    emit_const(chunk, c_zero, 2);  /* stack: [arr, sum=0] */

    /* Initialize i = 0 */
    emit_const(chunk, c_zero, 2);  /* stack: [arr, sum, i=0] */

    /* Loop start */
    size_t loop_start = chunk->code_size;

    /* Check i < 5 */
    chunk_write_opcode(chunk, OP_DUP, 3);          /* [arr, sum, i, i] */
    emit_const(chunk, c_five, 3);                  /* [arr, sum, i, i, 5] */
    chunk_write_opcode(chunk, OP_GE, 3);           /* [arr, sum, i, i>=5] */
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 3);
    chunk_write_opcode(chunk, OP_POP, 3);          /* [arr, sum, i] */

    /* sum = sum + arr[i] */
    /* Get arr[i] */
    emit_get_local(chunk, 0, 4);                   /* [arr, sum, i, arr] */
    emit_get_local(chunk, 2, 4);                   /* [arr, sum, i, arr, i] */
    chunk_write_opcode(chunk, OP_ARRAY_GET, 4);    /* [arr, sum, i, arr[i]] */

    /* Add to sum */
    emit_get_local(chunk, 1, 4);                   /* [arr, sum, i, arr[i], sum] */
    chunk_write_opcode(chunk, OP_ADD, 4);          /* [arr, sum, i, sum+arr[i]] */
    emit_set_local(chunk, 1, 4);                   /* [arr, sum=sum+arr[i], i, sum] */
    chunk_write_opcode(chunk, OP_POP, 4);          /* [arr, sum, i] */

    /* i = i + 1 */
    emit_const(chunk, c_one, 5);
    chunk_write_opcode(chunk, OP_ADD, 5);          /* [arr, sum, i+1] */

    /* Loop back */
    chunk_write_opcode(chunk, OP_LOOP, 5);
    uint16_t loop_offset = (uint16_t)(chunk->code_size - loop_start + 2);
    chunk_write_byte(chunk, (loop_offset >> 8) & 0xFF, 5);
    chunk_write_byte(chunk, loop_offset & 0xFF, 5);

    /* Exit: pop condition and i, leave sum on stack */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 6);          /* Pop condition */
    chunk_write_opcode(chunk, OP_POP, 6);          /* Pop i */
    chunk_write_opcode(chunk, OP_SWAP, 6);         /* [sum, arr] */
    chunk_write_opcode(chunk, OP_POP, 6);          /* [sum] */
    chunk_write_opcode(chunk, OP_HALT, 6);

    /* Run */
    VM *vm = vm_new();
    vm->reduction_limit = 100000;
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(15, vm_peek(vm, 0)->as.integer);  /* 1+2+3+4+5 = 15 */

    printf("    Result: sum([1,2,3,4,5]) = %ld (expected 15)\n", vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/* Test: Map operations */
void test_map_operations(void) {
    printf("  Program: build map {a: 10, b: 20}, get a + b\n");

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Constants */
    size_t c_key_a = chunk_add_constant(chunk, value_string("a"));
    size_t c_key_b = chunk_add_constant(chunk, value_string("b"));
    size_t c_val_a = chunk_add_constant(chunk, value_int(10));
    size_t c_val_b = chunk_add_constant(chunk, value_int(20));

    /* Create map */
    chunk_write_opcode(chunk, OP_MAP_NEW, 1);

    /* Set a = 10 */
    emit_const(chunk, c_key_a, 1);
    emit_const(chunk, c_val_a, 1);
    chunk_write_opcode(chunk, OP_MAP_SET, 1);

    /* Set b = 20 */
    emit_const(chunk, c_key_b, 2);
    emit_const(chunk, c_val_b, 2);
    chunk_write_opcode(chunk, OP_MAP_SET, 2);

    /* Get a */
    chunk_write_opcode(chunk, OP_DUP, 3);
    emit_const(chunk, c_key_a, 3);
    chunk_write_opcode(chunk, OP_MAP_GET, 3);      /* [map, a_val] */

    /* Get b */
    chunk_write_opcode(chunk, OP_SWAP, 3);          /* [a_val, map] */
    emit_const(chunk, c_key_b, 3);
    chunk_write_opcode(chunk, OP_MAP_GET, 3);      /* [a_val, b_val] */

    /* Add */
    chunk_write_opcode(chunk, OP_ADD, 4);
    chunk_write_opcode(chunk, OP_HALT, 4);

    /* Run */
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(30, vm_peek(vm, 0)->as.integer);

    printf("    Result: map['a'] + map['b'] = %ld (expected 30)\n", vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/* Test: String operations */
void test_string_operations(void) {
    printf("  Program: \"Hello\" + \", \" + \"World\" + \"!\"\n");

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_hello = chunk_add_constant(chunk, value_string("Hello"));
    size_t c_comma = chunk_add_constant(chunk, value_string(", "));
    size_t c_world = chunk_add_constant(chunk, value_string("World"));
    size_t c_bang = chunk_add_constant(chunk, value_string("!"));

    emit_const(chunk, c_hello, 1);
    emit_const(chunk, c_comma, 1);
    chunk_write_opcode(chunk, OP_ADD, 1);
    emit_const(chunk, c_world, 1);
    chunk_write_opcode(chunk, OP_ADD, 1);
    emit_const(chunk, c_bang, 1);
    chunk_write_opcode(chunk, OP_ADD, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    /* Run */
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_STR_EQ("Hello, World!", vm_peek(vm, 0)->as.string->data);

    printf("    Result: \"%s\" (expected \"Hello, World!\")\n", vm_peek(vm, 0)->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

/* Test: Nested function calls */
void test_nested_calls(void) {
    printf("  Program: outer(inner(5)) where inner(x)=x*2, outer(x)=x+10\n");

    Bytecode *code = bytecode_new();
    Chunk *main_chunk = code->main;

    /* inner(x) = x * 2 */
    Chunk *inner_func = chunk_new();
    size_t c_two_i = chunk_add_constant(inner_func, value_int(2));
    emit_get_local(inner_func, 1, 1);
    emit_const(inner_func, c_two_i, 1);
    chunk_write_opcode(inner_func, OP_MUL, 1);
    chunk_write_opcode(inner_func, OP_RETURN, 1);
    size_t inner_index = bytecode_add_function(code, inner_func);

    /* outer(x) = x + 10 */
    Chunk *outer_func = chunk_new();
    size_t c_ten_o = chunk_add_constant(outer_func, value_int(10));
    emit_get_local(outer_func, 1, 1);
    emit_const(outer_func, c_ten_o, 1);
    chunk_write_opcode(outer_func, OP_ADD, 1);
    chunk_write_opcode(outer_func, OP_RETURN, 1);
    size_t outer_index = bytecode_add_function(code, outer_func);

    /* Main: outer(inner(5)) */
    Value *inner_val = value_function("inner", 1);
    inner_val->as.function->code_offset = inner_index;
    size_t c_inner = chunk_add_constant(main_chunk, inner_val);

    Value *outer_val = value_function("outer", 1);
    outer_val->as.function->code_offset = outer_index;
    size_t c_outer = chunk_add_constant(main_chunk, outer_val);

    size_t c_five = chunk_add_constant(main_chunk, value_int(5));

    /* Call inner(5) */
    emit_const(main_chunk, c_inner, 1);
    emit_const(main_chunk, c_five, 1);
    emit_call(main_chunk, 1, 1);

    /* Call outer(result) */
    emit_const(main_chunk, c_outer, 2);
    chunk_write_opcode(main_chunk, OP_SWAP, 2);
    emit_call(main_chunk, 1, 2);

    chunk_write_opcode(main_chunk, OP_HALT, 3);

    /* Run */
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* inner(5) = 10, outer(10) = 20 */
    ASSERT_EQ(20, vm_peek(vm, 0)->as.integer);

    printf("    Result: outer(inner(5)) = %ld (expected 20)\n", vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/* Test: Complex expression */
void test_complex_expression(void) {
    printf("  Program: ((10 + 5) * 2 - 4) / 2\n");

    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_10 = chunk_add_constant(chunk, value_int(10));
    size_t c_5 = chunk_add_constant(chunk, value_int(5));
    size_t c_2 = chunk_add_constant(chunk, value_int(2));
    size_t c_4 = chunk_add_constant(chunk, value_int(4));

    /* (10 + 5) */
    emit_const(chunk, c_10, 1);
    emit_const(chunk, c_5, 1);
    chunk_write_opcode(chunk, OP_ADD, 1);

    /* * 2 */
    emit_const(chunk, c_2, 1);
    chunk_write_opcode(chunk, OP_MUL, 1);

    /* - 4 */
    emit_const(chunk, c_4, 1);
    chunk_write_opcode(chunk, OP_SUB, 1);

    /* / 2 */
    emit_const(chunk, c_2, 1);
    chunk_write_opcode(chunk, OP_DIV, 1);

    chunk_write_opcode(chunk, OP_HALT, 1);

    /* Run */
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* ((10+5)*2-4)/2 = (15*2-4)/2 = (30-4)/2 = 26/2 = 13 */
    ASSERT_EQ(13, vm_peek(vm, 0)->as.integer);

    printf("    Result: ((10+5)*2-4)/2 = %ld (expected 13)\n", vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/* Main */

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Agim VM Integration Tests\n");
    printf("=================================================\n\n");

    RUN_TEST(test_countdown_loop);
    RUN_TEST(test_function_call);
    RUN_TEST(test_recursive_factorial);
    RUN_TEST(test_fibonacci);
    RUN_TEST(test_array_operations);
    RUN_TEST(test_map_operations);
    RUN_TEST(test_string_operations);
    RUN_TEST(test_nested_calls);
    RUN_TEST(test_complex_expression);

    printf("\n=================================================\n");
    return TEST_RESULT();
}
