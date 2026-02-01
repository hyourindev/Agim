/*
 * Agim - VM Comparison Operations Tests
 *
 * P1.1.1.3 - Comprehensive tests for comparison operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "vm/nanbox.h"
#include "types/string.h"
#include "types/array.h"
#include "types/map.h"

#include <math.h>
#include <float.h>

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

/* Helper to run and get boolean result */
static bool run_get_bool(Bytecode *code) {
    VM *vm = vm_new();
    vm_load(vm, code);
    vm_run(vm);
    Value *v = vm_peek(vm, 0);
    bool result = v->as.boolean;
    vm_free(vm);
    bytecode_free(code);
    return result;
}

/*
 * =============================================================================
 * Test: OP_EQ - Equality with all type combinations
 * =============================================================================
 */

void test_eq_nil_nil(void) {
    Bytecode *code = make_binary_op(value_nil(), value_nil(), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_bool_true_true(void) {
    Bytecode *code = make_binary_op(value_bool(true), value_bool(true), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_bool_false_false(void) {
    Bytecode *code = make_binary_op(value_bool(false), value_bool(false), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_bool_true_false(void) {
    Bytecode *code = make_binary_op(value_bool(true), value_bool(false), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_int_same(void) {
    Bytecode *code = make_binary_op(value_int(42), value_int(42), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_int_different(void) {
    Bytecode *code = make_binary_op(value_int(42), value_int(43), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_int_zero(void) {
    Bytecode *code = make_binary_op(value_int(0), value_int(0), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_int_negative(void) {
    Bytecode *code = make_binary_op(value_int(-10), value_int(-10), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_float_same(void) {
    Bytecode *code = make_binary_op(value_float(3.14), value_float(3.14), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_float_different(void) {
    Bytecode *code = make_binary_op(value_float(3.14), value_float(2.71), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_int_float_same_value(void) {
    /* 5 == 5.0 should be true (numeric equality) */
    Bytecode *code = make_binary_op(value_int(5), value_float(5.0), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_float_int_same_value(void) {
    Bytecode *code = make_binary_op(value_float(5.0), value_int(5), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_string_same(void) {
    Bytecode *code = make_binary_op(value_string("hello"), value_string("hello"), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_string_different(void) {
    Bytecode *code = make_binary_op(value_string("hello"), value_string("world"), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_string_empty(void) {
    Bytecode *code = make_binary_op(value_string(""), value_string(""), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_string_case_sensitive(void) {
    Bytecode *code = make_binary_op(value_string("Hello"), value_string("hello"), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_pid_same(void) {
    Bytecode *code = make_binary_op(value_pid(12345), value_pid(12345), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_pid_different(void) {
    Bytecode *code = make_binary_op(value_pid(12345), value_pid(54321), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_nil_int(void) {
    /* Different types should not be equal */
    Bytecode *code = make_binary_op(value_nil(), value_int(0), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_bool_int(void) {
    /* true != 1 (different types) */
    Bytecode *code = make_binary_op(value_bool(true), value_int(1), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_int_string(void) {
    Bytecode *code = make_binary_op(value_int(42), value_string("42"), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

/*
 * =============================================================================
 * Test: OP_EQ - Reference equality for objects
 * =============================================================================
 */

void test_eq_array_same_content(void) {
    /* Two arrays with same content should be equal (deep equality) */
    Value *a = value_array();
    a = array_push(a, value_int(1));
    a = array_push(a, value_int(2));

    Value *b = value_array();
    b = array_push(b, value_int(1));
    b = array_push(b, value_int(2));

    Bytecode *code = make_binary_op(a, b, OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_array_different_content(void) {
    Value *a = value_array();
    a = array_push(a, value_int(1));

    Value *b = value_array();
    b = array_push(b, value_int(2));

    Bytecode *code = make_binary_op(a, b, OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_array_empty(void) {
    Value *a = value_array();
    Value *b = value_array();
    Bytecode *code = make_binary_op(a, b, OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_map_same_content(void) {
    Value *a = value_map();
    map_set(a, "key", value_int(42));

    Value *b = value_map();
    map_set(b, "key", value_int(42));

    Bytecode *code = make_binary_op(a, b, OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_eq_map_different_content(void) {
    Value *a = value_map();
    map_set(a, "key", value_int(42));

    Value *b = value_map();
    map_set(b, "key", value_int(99));

    Bytecode *code = make_binary_op(a, b, OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

/*
 * =============================================================================
 * Test: OP_NE - Not Equal
 * =============================================================================
 */

void test_ne_same_int(void) {
    Bytecode *code = make_binary_op(value_int(42), value_int(42), OP_NE);
    ASSERT(run_get_bool(code) == false);
}

void test_ne_different_int(void) {
    Bytecode *code = make_binary_op(value_int(42), value_int(43), OP_NE);
    ASSERT(run_get_bool(code) == true);
}

void test_ne_nil_nil(void) {
    Bytecode *code = make_binary_op(value_nil(), value_nil(), OP_NE);
    ASSERT(run_get_bool(code) == false);
}

void test_ne_different_types(void) {
    Bytecode *code = make_binary_op(value_int(1), value_string("1"), OP_NE);
    ASSERT(run_get_bool(code) == true);
}

/*
 * =============================================================================
 * Test: OP_LT - Less Than
 * =============================================================================
 */

void test_lt_int_true(void) {
    Bytecode *code = make_binary_op(value_int(5), value_int(10), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_int_false(void) {
    Bytecode *code = make_binary_op(value_int(10), value_int(5), OP_LT);
    ASSERT(run_get_bool(code) == false);
}

void test_lt_int_equal(void) {
    Bytecode *code = make_binary_op(value_int(5), value_int(5), OP_LT);
    ASSERT(run_get_bool(code) == false);
}

void test_lt_float_true(void) {
    Bytecode *code = make_binary_op(value_float(1.5), value_float(2.5), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_float_false(void) {
    Bytecode *code = make_binary_op(value_float(2.5), value_float(1.5), OP_LT);
    ASSERT(run_get_bool(code) == false);
}

void test_lt_mixed_int_float(void) {
    Bytecode *code = make_binary_op(value_int(5), value_float(5.5), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_mixed_float_int(void) {
    Bytecode *code = make_binary_op(value_float(4.5), value_int(5), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_negative(void) {
    Bytecode *code = make_binary_op(value_int(-10), value_int(-5), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_string_lexicographic(void) {
    /* "apple" < "banana" */
    Bytecode *code = make_binary_op(value_string("apple"), value_string("banana"), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_string_same_prefix(void) {
    /* "ab" < "abc" */
    Bytecode *code = make_binary_op(value_string("ab"), value_string("abc"), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_string_case(void) {
    /* ASCII: 'A' (65) < 'a' (97) */
    Bytecode *code = make_binary_op(value_string("A"), value_string("a"), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_string_equal(void) {
    Bytecode *code = make_binary_op(value_string("hello"), value_string("hello"), OP_LT);
    ASSERT(run_get_bool(code) == false);
}

void test_lt_type_error(void) {
    /* Comparing incompatible types should error */
    Bytecode *code = make_binary_op(value_int(1), value_string("1"), OP_LT);
    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);
    ASSERT_EQ(VM_ERROR_TYPE, result);
    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_LE - Less Than or Equal
 * =============================================================================
 */

void test_le_less(void) {
    Bytecode *code = make_binary_op(value_int(5), value_int(10), OP_LE);
    ASSERT(run_get_bool(code) == true);
}

void test_le_equal(void) {
    Bytecode *code = make_binary_op(value_int(5), value_int(5), OP_LE);
    ASSERT(run_get_bool(code) == true);
}

void test_le_greater(void) {
    Bytecode *code = make_binary_op(value_int(10), value_int(5), OP_LE);
    ASSERT(run_get_bool(code) == false);
}

void test_le_float_boundary(void) {
    Bytecode *code = make_binary_op(value_float(5.0), value_float(5.0), OP_LE);
    ASSERT(run_get_bool(code) == true);
}

void test_le_string(void) {
    Bytecode *code = make_binary_op(value_string("abc"), value_string("abc"), OP_LE);
    ASSERT(run_get_bool(code) == true);
}

/*
 * =============================================================================
 * Test: OP_GT - Greater Than
 * =============================================================================
 */

void test_gt_true(void) {
    Bytecode *code = make_binary_op(value_int(10), value_int(5), OP_GT);
    ASSERT(run_get_bool(code) == true);
}

void test_gt_false(void) {
    Bytecode *code = make_binary_op(value_int(5), value_int(10), OP_GT);
    ASSERT(run_get_bool(code) == false);
}

void test_gt_equal(void) {
    Bytecode *code = make_binary_op(value_int(5), value_int(5), OP_GT);
    ASSERT(run_get_bool(code) == false);
}

void test_gt_string(void) {
    Bytecode *code = make_binary_op(value_string("z"), value_string("a"), OP_GT);
    ASSERT(run_get_bool(code) == true);
}

/*
 * =============================================================================
 * Test: OP_GE - Greater Than or Equal
 * =============================================================================
 */

void test_ge_greater(void) {
    Bytecode *code = make_binary_op(value_int(10), value_int(5), OP_GE);
    ASSERT(run_get_bool(code) == true);
}

void test_ge_equal(void) {
    Bytecode *code = make_binary_op(value_int(5), value_int(5), OP_GE);
    ASSERT(run_get_bool(code) == true);
}

void test_ge_less(void) {
    Bytecode *code = make_binary_op(value_int(5), value_int(10), OP_GE);
    ASSERT(run_get_bool(code) == false);
}

/*
 * =============================================================================
 * Test: OP_NOT - Logical negation
 * =============================================================================
 */

void test_not_true(void) {
    Bytecode *code = make_unary_op(value_bool(true), OP_NOT);
    ASSERT(run_get_bool(code) == false);
}

void test_not_false(void) {
    Bytecode *code = make_unary_op(value_bool(false), OP_NOT);
    ASSERT(run_get_bool(code) == true);
}

void test_not_nil(void) {
    /* nil is falsy, so !nil = true */
    Bytecode *code = make_unary_op(value_nil(), OP_NOT);
    ASSERT(run_get_bool(code) == true);
}

void test_not_zero_int(void) {
    /* 0 is falsy */
    Bytecode *code = make_unary_op(value_int(0), OP_NOT);
    ASSERT(run_get_bool(code) == true);
}

void test_not_nonzero_int(void) {
    /* non-zero is truthy */
    Bytecode *code = make_unary_op(value_int(42), OP_NOT);
    ASSERT(run_get_bool(code) == false);
}

void test_not_zero_float(void) {
    Bytecode *code = make_unary_op(value_float(0.0), OP_NOT);
    ASSERT(run_get_bool(code) == true);
}

void test_not_nonzero_float(void) {
    Bytecode *code = make_unary_op(value_float(3.14), OP_NOT);
    ASSERT(run_get_bool(code) == false);
}

void test_not_string(void) {
    /* Non-empty string is truthy */
    Bytecode *code = make_unary_op(value_string("hello"), OP_NOT);
    ASSERT(run_get_bool(code) == false);
}

void test_not_empty_string(void) {
    /* Empty string is still truthy (it's an object) */
    Bytecode *code = make_unary_op(value_string(""), OP_NOT);
    ASSERT(run_get_bool(code) == false);
}

/*
 * =============================================================================
 * Test: Comparisons with NaN
 * =============================================================================
 */

void test_nan_eq_nan(void) {
    /* NaN != NaN (IEEE 754) */
    Bytecode *code = make_binary_op(value_float(NAN), value_float(NAN), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_nan_ne_nan(void) {
    /* NaN != NaN is true */
    Bytecode *code = make_binary_op(value_float(NAN), value_float(NAN), OP_NE);
    ASSERT(run_get_bool(code) == true);
}

void test_nan_lt_number(void) {
    /* NaN < x is always false */
    Bytecode *code = make_binary_op(value_float(NAN), value_float(1.0), OP_LT);
    ASSERT(run_get_bool(code) == false);
}

void test_number_lt_nan(void) {
    /* x < NaN is always false */
    Bytecode *code = make_binary_op(value_float(1.0), value_float(NAN), OP_LT);
    ASSERT(run_get_bool(code) == false);
}

void test_nan_gt_number(void) {
    Bytecode *code = make_binary_op(value_float(NAN), value_float(1.0), OP_GT);
    ASSERT(run_get_bool(code) == false);
}

void test_nan_le_number(void) {
    Bytecode *code = make_binary_op(value_float(NAN), value_float(1.0), OP_LE);
    ASSERT(run_get_bool(code) == false);
}

void test_nan_ge_number(void) {
    Bytecode *code = make_binary_op(value_float(NAN), value_float(1.0), OP_GE);
    ASSERT(run_get_bool(code) == false);
}

/*
 * =============================================================================
 * Test: Comparisons with Infinity
 * =============================================================================
 */

void test_inf_eq_inf(void) {
    Bytecode *code = make_binary_op(value_float(INFINITY), value_float(INFINITY), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_neg_inf_eq_neg_inf(void) {
    Bytecode *code = make_binary_op(value_float(-INFINITY), value_float(-INFINITY), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_inf_ne_neg_inf(void) {
    Bytecode *code = make_binary_op(value_float(INFINITY), value_float(-INFINITY), OP_NE);
    ASSERT(run_get_bool(code) == true);
}

void test_inf_gt_number(void) {
    Bytecode *code = make_binary_op(value_float(INFINITY), value_float(1e308), OP_GT);
    ASSERT(run_get_bool(code) == true);
}

void test_neg_inf_lt_number(void) {
    Bytecode *code = make_binary_op(value_float(-INFINITY), value_float(-1e308), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_number_lt_inf(void) {
    Bytecode *code = make_binary_op(value_float(1e308), value_float(INFINITY), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_inf_eq_number(void) {
    Bytecode *code = make_binary_op(value_float(INFINITY), value_float(1e308), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

/*
 * =============================================================================
 * Test: Edge cases
 * =============================================================================
 */

void test_eq_negative_zero(void) {
    /* -0.0 == 0.0 in IEEE 754 */
    Bytecode *code = make_binary_op(value_float(-0.0), value_float(0.0), OP_EQ);
    ASSERT(run_get_bool(code) == true);
}

void test_lt_negative_zero(void) {
    /* -0.0 is not less than 0.0 */
    Bytecode *code = make_binary_op(value_float(-0.0), value_float(0.0), OP_LT);
    ASSERT(run_get_bool(code) == false);
}

void test_eq_very_close_floats(void) {
    /* Floats that differ by epsilon */
    double a = 1.0;
    double b = 1.0 + DBL_EPSILON;
    Bytecode *code = make_binary_op(value_float(a), value_float(b), OP_EQ);
    ASSERT(run_get_bool(code) == false);
}

void test_lt_very_close_floats(void) {
    double a = 1.0;
    double b = 1.0 + DBL_EPSILON;
    Bytecode *code = make_binary_op(value_float(a), value_float(b), OP_LT);
    ASSERT(run_get_bool(code) == true);
}

void test_double_negation(void) {
    /* !!true == true */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_TRUE, 1);
    chunk_write_opcode(chunk, OP_NOT, 1);
    chunk_write_opcode(chunk, OP_NOT, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    VM *vm = vm_new();
    vm_load(vm, code);
    vm_run(vm);
    Value *v = vm_peek(vm, 0);
    ASSERT(v->as.boolean == true);
    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Main
 * =============================================================================
 */

int main(void) {
    printf("=== VM Comparison Operations Tests (P1.1.1.3) ===\n\n");

    printf("--- OP_EQ with all type combinations ---\n");
    RUN_TEST(test_eq_nil_nil);
    RUN_TEST(test_eq_bool_true_true);
    RUN_TEST(test_eq_bool_false_false);
    RUN_TEST(test_eq_bool_true_false);
    RUN_TEST(test_eq_int_same);
    RUN_TEST(test_eq_int_different);
    RUN_TEST(test_eq_int_zero);
    RUN_TEST(test_eq_int_negative);
    RUN_TEST(test_eq_float_same);
    RUN_TEST(test_eq_float_different);
    RUN_TEST(test_eq_int_float_same_value);
    RUN_TEST(test_eq_float_int_same_value);
    RUN_TEST(test_eq_string_same);
    RUN_TEST(test_eq_string_different);
    RUN_TEST(test_eq_string_empty);
    RUN_TEST(test_eq_string_case_sensitive);
    RUN_TEST(test_eq_pid_same);
    RUN_TEST(test_eq_pid_different);
    RUN_TEST(test_eq_nil_int);
    RUN_TEST(test_eq_bool_int);
    RUN_TEST(test_eq_int_string);

    printf("\n--- OP_EQ reference equality for objects ---\n");
    RUN_TEST(test_eq_array_same_content);
    RUN_TEST(test_eq_array_different_content);
    RUN_TEST(test_eq_array_empty);
    RUN_TEST(test_eq_map_same_content);
    RUN_TEST(test_eq_map_different_content);

    printf("\n--- OP_NE ---\n");
    RUN_TEST(test_ne_same_int);
    RUN_TEST(test_ne_different_int);
    RUN_TEST(test_ne_nil_nil);
    RUN_TEST(test_ne_different_types);

    printf("\n--- OP_LT with strings (lexicographic) ---\n");
    RUN_TEST(test_lt_int_true);
    RUN_TEST(test_lt_int_false);
    RUN_TEST(test_lt_int_equal);
    RUN_TEST(test_lt_float_true);
    RUN_TEST(test_lt_float_false);
    RUN_TEST(test_lt_mixed_int_float);
    RUN_TEST(test_lt_mixed_float_int);
    RUN_TEST(test_lt_negative);
    RUN_TEST(test_lt_string_lexicographic);
    RUN_TEST(test_lt_string_same_prefix);
    RUN_TEST(test_lt_string_case);
    RUN_TEST(test_lt_string_equal);
    RUN_TEST(test_lt_type_error);

    printf("\n--- OP_LE boundary cases ---\n");
    RUN_TEST(test_le_less);
    RUN_TEST(test_le_equal);
    RUN_TEST(test_le_greater);
    RUN_TEST(test_le_float_boundary);
    RUN_TEST(test_le_string);

    printf("\n--- OP_GT ---\n");
    RUN_TEST(test_gt_true);
    RUN_TEST(test_gt_false);
    RUN_TEST(test_gt_equal);
    RUN_TEST(test_gt_string);

    printf("\n--- OP_GE ---\n");
    RUN_TEST(test_ge_greater);
    RUN_TEST(test_ge_equal);
    RUN_TEST(test_ge_less);

    printf("\n--- OP_NOT ---\n");
    RUN_TEST(test_not_true);
    RUN_TEST(test_not_false);
    RUN_TEST(test_not_nil);
    RUN_TEST(test_not_zero_int);
    RUN_TEST(test_not_nonzero_int);
    RUN_TEST(test_not_zero_float);
    RUN_TEST(test_not_nonzero_float);
    RUN_TEST(test_not_string);
    RUN_TEST(test_not_empty_string);

    printf("\n--- Comparison with NaN values ---\n");
    RUN_TEST(test_nan_eq_nan);
    RUN_TEST(test_nan_ne_nan);
    RUN_TEST(test_nan_lt_number);
    RUN_TEST(test_number_lt_nan);
    RUN_TEST(test_nan_gt_number);
    RUN_TEST(test_nan_le_number);
    RUN_TEST(test_nan_ge_number);

    printf("\n--- Comparison with Inf values ---\n");
    RUN_TEST(test_inf_eq_inf);
    RUN_TEST(test_neg_inf_eq_neg_inf);
    RUN_TEST(test_inf_ne_neg_inf);
    RUN_TEST(test_inf_gt_number);
    RUN_TEST(test_neg_inf_lt_number);
    RUN_TEST(test_number_lt_inf);
    RUN_TEST(test_inf_eq_number);

    printf("\n--- Edge cases ---\n");
    RUN_TEST(test_eq_negative_zero);
    RUN_TEST(test_lt_negative_zero);
    RUN_TEST(test_eq_very_close_floats);
    RUN_TEST(test_lt_very_close_floats);
    RUN_TEST(test_double_negation);

    return TEST_RESULT();
}
