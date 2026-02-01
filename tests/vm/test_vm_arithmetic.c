/*
 * Agim - VM Arithmetic Operations Tests
 *
 * P1.1.1.2 - Comprehensive tests for arithmetic operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "vm/nanbox.h"
#include "types/string.h"

#include <math.h>
#include <float.h>
#include <stdint.h>

/* Helper to create bytecode for binary operation */
static Bytecode *make_binary_op(Value *a, Value *b, Opcode op) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, a);
    chunk_add_constant(chunk, b);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 1, 1);

    chunk_write_opcode(chunk, op, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Helper to create bytecode for unary operation */
static Bytecode *make_unary_op(Value *a, Opcode op) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_add_constant(chunk, a);

    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    chunk_write_opcode(chunk, op, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/*
 * =============================================================================
 * Test: OP_ADD
 * =============================================================================
 */

void test_add_integers(void) {
    Bytecode *code = make_binary_op(value_int(10), value_int(20), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(30, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_add_negative_integers(void) {
    Bytecode *code = make_binary_op(value_int(-10), value_int(-20), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(-30, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_add_integer_overflow(void) {
    /* Test large integers - C behavior on overflow */
    int64_t large = (1LL << 62);
    Bytecode *code = make_binary_op(value_int(large), value_int(large), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* Integer overflow wraps in C - this is expected behavior */
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));

    vm_free(vm);
    bytecode_free(code);
}

void test_add_floats(void) {
    Bytecode *code = make_binary_op(value_float(1.5), value_float(2.5), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 4.0) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_add_float_precision(void) {
    /* Test floating point precision edge case */
    Bytecode *code = make_binary_op(value_float(0.1), value_float(0.2), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    /* 0.1 + 0.2 != 0.3 exactly in IEEE 754 */
    ASSERT(fabs(v->as.floating - 0.3) < 0.0000001);

    vm_free(vm);
    bytecode_free(code);
}

void test_add_mixed_int_float(void) {
    Bytecode *code = make_binary_op(value_int(10), value_float(2.5), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    /* Mixed operations promote to float */
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 12.5) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_add_float_int(void) {
    /* Test order doesn't matter */
    Bytecode *code = make_binary_op(value_float(2.5), value_int(10), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 12.5) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_add_strings(void) {
    Bytecode *code = make_binary_op(value_string("hello"), value_string(" world"), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_string(v));
    ASSERT_STR_EQ("hello world", v->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

void test_add_type_error(void) {
    /* Adding bool + int should fail */
    Bytecode *code = make_binary_op(value_bool(true), value_int(1), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_SUB
 * =============================================================================
 */

void test_sub_integers(void) {
    Bytecode *code = make_binary_op(value_int(30), value_int(10), OP_SUB);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(20, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_sub_negative_result(void) {
    Bytecode *code = make_binary_op(value_int(10), value_int(30), OP_SUB);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(-20, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_sub_integer_underflow(void) {
    /* Test underflow with minimum 48-bit int: -(2^47) */
    int64_t nanbox_min = -(1LL << 47);
    Bytecode *code = make_binary_op(value_int(nanbox_min), value_int(1), OP_SUB);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* Underflow wraps */
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));

    vm_free(vm);
    bytecode_free(code);
}

void test_sub_floats(void) {
    Bytecode *code = make_binary_op(value_float(5.5), value_float(2.5), OP_SUB);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 3.0) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_sub_mixed_types(void) {
    Bytecode *code = make_binary_op(value_int(10), value_float(2.5), OP_SUB);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 7.5) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_MUL
 * =============================================================================
 */

void test_mul_integers(void) {
    Bytecode *code = make_binary_op(value_int(6), value_int(7), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(42, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_mul_by_zero(void) {
    Bytecode *code = make_binary_op(value_int(100), value_int(0), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(0, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_mul_negative(void) {
    Bytecode *code = make_binary_op(value_int(-5), value_int(3), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(-15, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_mul_both_negative(void) {
    Bytecode *code = make_binary_op(value_int(-5), value_int(-3), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(15, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_mul_integer_overflow(void) {
    int64_t large = (1LL << 40);
    Bytecode *code = make_binary_op(value_int(large), value_int(large), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* Overflow wraps */
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));

    vm_free(vm);
    bytecode_free(code);
}

void test_mul_floats(void) {
    Bytecode *code = make_binary_op(value_float(2.5), value_float(4.0), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 10.0) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_mul_mixed_types(void) {
    Bytecode *code = make_binary_op(value_int(3), value_float(2.5), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 7.5) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_DIV
 * =============================================================================
 */

void test_div_integers(void) {
    Bytecode *code = make_binary_op(value_int(42), value_int(6), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(7, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_div_integer_truncation(void) {
    /* Integer division truncates toward zero */
    Bytecode *code = make_binary_op(value_int(7), value_int(3), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(2, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_div_by_zero_int(void) {
    Bytecode *code = make_binary_op(value_int(10), value_int(0), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_DIVISION_BY_ZERO, result);
    const char *error = vm_error(vm);
    ASSERT(error != NULL);

    vm_free(vm);
    bytecode_free(code);
}

void test_div_by_zero_float(void) {
    Bytecode *code = make_binary_op(value_float(10.0), value_float(0.0), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    /* Float division by zero returns error (not Inf in this implementation) */
    ASSERT_EQ(VM_ERROR_DIVISION_BY_ZERO, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_div_floats(void) {
    Bytecode *code = make_binary_op(value_float(10.0), value_float(4.0), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 2.5) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_div_mixed_types(void) {
    Bytecode *code = make_binary_op(value_int(10), value_float(4.0), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 2.5) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_div_negative(void) {
    Bytecode *code = make_binary_op(value_int(-10), value_int(3), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    /* C integer division truncates toward zero */
    ASSERT_EQ(-3, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_MOD
 * =============================================================================
 */

void test_mod_positive(void) {
    Bytecode *code = make_binary_op(value_int(17), value_int(5), OP_MOD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(2, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_mod_negative_dividend(void) {
    /* C semantics: result has sign of dividend */
    Bytecode *code = make_binary_op(value_int(-17), value_int(5), OP_MOD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(-2, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_mod_negative_divisor(void) {
    Bytecode *code = make_binary_op(value_int(17), value_int(-5), OP_MOD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(2, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_mod_both_negative(void) {
    Bytecode *code = make_binary_op(value_int(-17), value_int(-5), OP_MOD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(-2, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_mod_by_zero(void) {
    Bytecode *code = make_binary_op(value_int(10), value_int(0), OP_MOD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_DIVISION_BY_ZERO, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_mod_with_floats_error(void) {
    /* Modulo requires integers */
    Bytecode *code = make_binary_op(value_float(17.0), value_float(5.0), OP_MOD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_mod_exact_division(void) {
    Bytecode *code = make_binary_op(value_int(15), value_int(5), OP_MOD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(0, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_NEG
 * =============================================================================
 */

void test_neg_positive_int(void) {
    Bytecode *code = make_unary_op(value_int(42), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(-42, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_neg_negative_int(void) {
    Bytecode *code = make_unary_op(value_int(-42), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(42, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_neg_zero(void) {
    Bytecode *code = make_unary_op(value_int(0), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    ASSERT_EQ(0, v->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_neg_min_int(void) {
    /* Negating MIN_INT (48-bit) overflows - test behavior */
    int64_t nanbox_min = -(1LL << 47);
    Bytecode *code = make_unary_op(value_int(nanbox_min), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    /* Result may overflow but operation completes */
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));

    vm_free(vm);
    bytecode_free(code);
}

void test_neg_float(void) {
    Bytecode *code = make_unary_op(value_float(3.14), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - (-3.14)) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_neg_negative_float(void) {
    Bytecode *code = make_unary_op(value_float(-2.5), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(fabs(v->as.floating - 2.5) < 0.0001);

    vm_free(vm);
    bytecode_free(code);
}

void test_neg_type_error(void) {
    Bytecode *code = make_unary_op(value_string("hello"), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_neg_bool_error(void) {
    Bytecode *code = make_unary_op(value_bool(true), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: NaN propagation
 * =============================================================================
 */

void test_nan_add(void) {
    Bytecode *code = make_binary_op(value_float(NAN), value_float(1.0), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(isnan(v->as.floating));

    vm_free(vm);
    bytecode_free(code);
}

void test_nan_sub(void) {
    Bytecode *code = make_binary_op(value_float(1.0), value_float(NAN), OP_SUB);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(isnan(v->as.floating));

    vm_free(vm);
    bytecode_free(code);
}

void test_nan_mul(void) {
    Bytecode *code = make_binary_op(value_float(NAN), value_float(2.0), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(isnan(v->as.floating));

    vm_free(vm);
    bytecode_free(code);
}

void test_nan_div(void) {
    Bytecode *code = make_binary_op(value_float(NAN), value_float(2.0), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(isnan(v->as.floating));

    vm_free(vm);
    bytecode_free(code);
}

void test_nan_neg(void) {
    Bytecode *code = make_unary_op(value_float(NAN), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(isnan(v->as.floating));

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Infinity handling
 * =============================================================================
 */

void test_inf_add(void) {
    Bytecode *code = make_binary_op(value_float(INFINITY), value_float(1.0), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(isinf(v->as.floating) && v->as.floating > 0);

    vm_free(vm);
    bytecode_free(code);
}

void test_neg_inf(void) {
    Bytecode *code = make_unary_op(value_float(INFINITY), OP_NEG);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    ASSERT(isinf(v->as.floating) && v->as.floating < 0);

    vm_free(vm);
    bytecode_free(code);
}

void test_inf_minus_inf(void) {
    Bytecode *code = make_binary_op(value_float(INFINITY), value_float(INFINITY), OP_SUB);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    /* Inf - Inf = NaN */
    ASSERT(isnan(v->as.floating));

    vm_free(vm);
    bytecode_free(code);
}

void test_inf_mul_zero(void) {
    Bytecode *code = make_binary_op(value_float(INFINITY), value_float(0.0), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));
    /* Inf * 0 = NaN */
    ASSERT(isnan(v->as.floating));

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Edge cases
 * =============================================================================
 */

void test_add_max_int_one(void) {
    /*
     * NaN-boxing uses 48-bit signed integers.
     * Max value is 2^47 - 1 = 140737488355327
     * Adding 1 causes overflow which wraps.
     */
    int64_t nanbox_max = (1LL << 47) - 1;
    Bytecode *code = make_binary_op(value_int(nanbox_max), value_int(1), OP_ADD);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));
    /* After overflow, the value wraps - just verify it completes */

    vm_free(vm);
    bytecode_free(code);
}

void test_div_min_by_minus_one(void) {
    /* MIN_INT (48-bit) / -1 would overflow - test behavior */
    int64_t nanbox_min = -(1LL << 47);
    Bytecode *code = make_binary_op(value_int(nanbox_min), value_int(-1), OP_DIV);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    /* Should complete - behavior is implementation-defined */
    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_int(v));

    vm_free(vm);
    bytecode_free(code);
}

void test_float_denormal(void) {
    /* Test with denormal (subnormal) float */
    double denormal = DBL_MIN / 2.0;
    Bytecode *code = make_binary_op(value_float(denormal), value_float(2.0), OP_MUL);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *v = vm_peek(vm, 0);
    ASSERT(value_is_float(v));

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Main
 * =============================================================================
 */

int main(void) {
    printf("=== VM Arithmetic Operations Tests (P1.1.1.2) ===\n\n");

    printf("--- OP_ADD ---\n");
    RUN_TEST(test_add_integers);
    RUN_TEST(test_add_negative_integers);
    RUN_TEST(test_add_integer_overflow);
    RUN_TEST(test_add_floats);
    RUN_TEST(test_add_float_precision);
    RUN_TEST(test_add_mixed_int_float);
    RUN_TEST(test_add_float_int);
    RUN_TEST(test_add_strings);
    RUN_TEST(test_add_type_error);

    printf("\n--- OP_SUB ---\n");
    RUN_TEST(test_sub_integers);
    RUN_TEST(test_sub_negative_result);
    RUN_TEST(test_sub_integer_underflow);
    RUN_TEST(test_sub_floats);
    RUN_TEST(test_sub_mixed_types);

    printf("\n--- OP_MUL ---\n");
    RUN_TEST(test_mul_integers);
    RUN_TEST(test_mul_by_zero);
    RUN_TEST(test_mul_negative);
    RUN_TEST(test_mul_both_negative);
    RUN_TEST(test_mul_integer_overflow);
    RUN_TEST(test_mul_floats);
    RUN_TEST(test_mul_mixed_types);

    printf("\n--- OP_DIV ---\n");
    RUN_TEST(test_div_integers);
    RUN_TEST(test_div_integer_truncation);
    RUN_TEST(test_div_by_zero_int);
    RUN_TEST(test_div_by_zero_float);
    RUN_TEST(test_div_floats);
    RUN_TEST(test_div_mixed_types);
    RUN_TEST(test_div_negative);

    printf("\n--- OP_MOD ---\n");
    RUN_TEST(test_mod_positive);
    RUN_TEST(test_mod_negative_dividend);
    RUN_TEST(test_mod_negative_divisor);
    RUN_TEST(test_mod_both_negative);
    RUN_TEST(test_mod_by_zero);
    RUN_TEST(test_mod_with_floats_error);
    RUN_TEST(test_mod_exact_division);

    printf("\n--- OP_NEG ---\n");
    RUN_TEST(test_neg_positive_int);
    RUN_TEST(test_neg_negative_int);
    RUN_TEST(test_neg_zero);
    RUN_TEST(test_neg_min_int);
    RUN_TEST(test_neg_float);
    RUN_TEST(test_neg_negative_float);
    RUN_TEST(test_neg_type_error);
    RUN_TEST(test_neg_bool_error);

    printf("\n--- NaN propagation ---\n");
    RUN_TEST(test_nan_add);
    RUN_TEST(test_nan_sub);
    RUN_TEST(test_nan_mul);
    RUN_TEST(test_nan_div);
    RUN_TEST(test_nan_neg);

    printf("\n--- Infinity handling ---\n");
    RUN_TEST(test_inf_add);
    RUN_TEST(test_neg_inf);
    RUN_TEST(test_inf_minus_inf);
    RUN_TEST(test_inf_mul_zero);

    printf("\n--- Edge cases ---\n");
    RUN_TEST(test_add_max_int_one);
    RUN_TEST(test_div_min_by_minus_one);
    RUN_TEST(test_float_denormal);

    return TEST_RESULT();
}
