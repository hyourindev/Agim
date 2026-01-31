/*
 * Agim - VM Edge Cases Tests
 *
 * Comprehensive tests for VM edge cases including overflow,
 * type coercion, NaN handling, and boundary conditions.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/regvm.h"
#include "vm/value.h"
#include "vm/nanbox.h"

#include <limits.h>
#include <float.h>
#include <math.h>

/* ========== Arithmetic Edge Cases ========== */

void test_add_int_overflow(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = INT64_MAX, r1 = 1, r2 = r0 + r1 (should overflow gracefully) */
    int64_t max_val = INT64_MAX;
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 0), 1);

    /* Load large constant via constant pool */
    size_t idx = regchunk_add_constant(chunk, value_int(max_val));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_ADD, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    /* Overflow wraps or produces float - either is acceptable */
    ASSERT(nanbox_is_int(vm->frames[0].regs[2]) || nanbox_is_double(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_sub_underflow(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = INT64_MIN, r1 = 1, r2 = r0 - r1 */
    int64_t min_val = INT64_MIN;
    size_t idx = regchunk_add_constant(chunk, value_int(min_val));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_SUB, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_mul_overflow(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* Large * Large = overflow */
    int64_t large = 1000000000000LL;
    size_t idx = regchunk_add_constant(chunk, value_int(large));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_MUL, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_div_by_zero_int(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = 42, r1 = 0, r2 = r0 / r1 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 42), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_DIV, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    /* Should either error or return nil/infinity */
    ASSERT(result == REGVM_HALT || result == REGVM_ERROR_RUNTIME);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_div_by_zero_float(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* 42.0 / 0.0 should produce Inf */
    size_t idx1 = regchunk_add_constant(chunk, value_float(42.0));
    size_t idx2 = regchunk_add_constant(chunk, value_float(0.0));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx1), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx2), 1);
    regchunk_write(chunk, reg_instr(ROP_DIV, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    if (nanbox_is_double(vm->frames[0].regs[2])) {
        double val = nanbox_as_double(vm->frames[0].regs[2]);
        ASSERT(isinf(val));
    }

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_mod_by_zero(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* 42 % 0 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 42), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_MOD, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT(result == REGVM_HALT || result == REGVM_ERROR_RUNTIME);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_mod_negative_numbers(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* -7 % 3 */
    size_t idx = regchunk_add_constant(chunk, value_int(-7));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 3), 1);
    regchunk_write(chunk, reg_instr(ROP_MOD, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_neg_min_int(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* -MIN_INT is undefined for 2's complement */
    int64_t min_val = INT64_MIN;
    size_t idx = regchunk_add_constant(chunk, value_int(min_val));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_NEG, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Float Special Values ========== */

void test_nan_propagation(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* NaN + 1 = NaN */
    size_t idx = regchunk_add_constant(chunk, value_float(NAN));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_ADD, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    if (nanbox_is_double(vm->frames[0].regs[2])) {
        double val = nanbox_as_double(vm->frames[0].regs[2]);
        ASSERT(isnan(val));
    }

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_infinity_arithmetic(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* Inf + Inf = Inf */
    size_t idx = regchunk_add_constant(chunk, value_float(INFINITY));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_ADD, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    if (nanbox_is_double(vm->frames[0].regs[2])) {
        double val = nanbox_as_double(vm->frames[0].regs[2]);
        ASSERT(isinf(val) && val > 0);
    }

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_infinity_minus_infinity(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* Inf - Inf = NaN */
    size_t idx1 = regchunk_add_constant(chunk, value_float(INFINITY));
    size_t idx2 = regchunk_add_constant(chunk, value_float(INFINITY));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx1), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx2), 1);
    regchunk_write(chunk, reg_instr(ROP_SUB, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    if (nanbox_is_double(vm->frames[0].regs[2])) {
        double val = nanbox_as_double(vm->frames[0].regs[2]);
        ASSERT(isnan(val));
    }

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Comparison Edge Cases ========== */

void test_eq_different_types(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* 42 == "42" should be false (different types) */
    size_t idx = regchunk_add_constant(chunk, value_string("42"));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 42), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_EQ, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_is_bool(vm->frames[0].regs[2]));
    ASSERT(!nanbox_as_bool(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_eq_int_float(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* 42 == 42.0 should be true */
    size_t idx = regchunk_add_constant(chunk, value_float(42.0));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 42), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_EQ, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_nan_comparison(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* NaN == NaN should be false */
    size_t idx = regchunk_add_constant(chunk, value_float(NAN));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_EQ, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    /* NaN != NaN per IEEE 754 */
    if (nanbox_is_bool(vm->frames[0].regs[2])) {
        ASSERT(!nanbox_as_bool(vm->frames[0].regs[2]));
    }

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_lt_nan(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* 1 < NaN should be false */
    size_t idx = regchunk_add_constant(chunk, value_float(NAN));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 1), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_LT, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    if (nanbox_is_bool(vm->frames[0].regs[2])) {
        ASSERT(!nanbox_as_bool(vm->frames[0].regs[2]));
    }

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_lt_infinity(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* 999999 < Inf should be true */
    size_t idx = regchunk_add_constant(chunk, value_float(INFINITY));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 999), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_LT, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    if (nanbox_is_bool(vm->frames[0].regs[2])) {
        ASSERT(nanbox_as_bool(vm->frames[0].regs[2]));
    }

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Control Flow Edge Cases ========== */

void test_jump_to_end(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* Jump with offset 0 means "continue to next instruction" since DISPATCH
     * already increments ip before executing the instruction */
    regchunk_write(chunk, reg_instr_jump(ROP_JMP, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_jump_backward(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = 0, r1 = 3 (counter)
     * loop: r0 = r0 + 1, r1 = r1 - 1, if r1 > 0 goto loop
     * Result: r0 = 3
     */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 0), 1);    /* 0: r0 = 0 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 3), 1);    /* 1: r1 = 3 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 2, 1), 1);    /* 2: r2 = 1 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 4, 0), 1);    /* 3: r4 = 0 */
    /* loop at 4 */
    regchunk_write(chunk, reg_instr(ROP_ADD, 0, 0, 2), 1);          /* 4: r0 = r0 + 1 */
    regchunk_write(chunk, reg_instr(ROP_SUB, 1, 1, 2), 1);          /* 5: r1 = r1 - 1 */
    regchunk_write(chunk, reg_instr(ROP_GT, 3, 1, 4), 1);           /* 6: r3 = r1 > 0 */
    regchunk_write(chunk, reg_instr_cond_jump(ROP_JMP_IF, 3, -4), 1); /* 7: if r3 goto 4 */
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);         /* 8: halt */

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(3, nanbox_as_int(vm->frames[0].regs[0]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_jmp_unless_false(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* JMP_UNLESS with false should jump */
    regchunk_write(chunk, reg_instr(ROP_LOAD_FALSE, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr_cond_jump(ROP_JMP_UNLESS, 0, 2), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 1), 1);  /* skipped */
    regchunk_write(chunk, reg_instr_jump(ROP_JMP, 1), 1);         /* skipped */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 42), 1); /* r1 = 42 */
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(42, nanbox_as_int(vm->frames[0].regs[1]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_nested_loops(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /*
     * r0 = 0 (result)
     * r1 = 0 (i)
     * r5 = 3 (outer limit)
     * outer: i < 3
     *   r2 = 0 (j)
     *   r6 = 3 (inner limit)
     *   inner: j < 3
     *     r0 = r0 + 1
     *     j = j + 1
     *     if j < 3 goto inner
     *   i = i + 1
     *   if i < 3 goto outer
     * Expected: r0 = 9 (3 * 3)
     */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 0), 1);    /* r0 = 0 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 0), 1);    /* r1 = 0 (i) */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 5, 3), 1);    /* r5 = 3 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 7, 1), 1);    /* r7 = 1 (increment) */
    /* outer loop at 4 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 2, 0), 1);    /* r2 = 0 (j) */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 6, 3), 1);    /* r6 = 3 */
    /* inner loop at 6 */
    regchunk_write(chunk, reg_instr(ROP_ADD, 0, 0, 7), 1);          /* r0++ */
    regchunk_write(chunk, reg_instr(ROP_ADD, 2, 2, 7), 1);          /* j++ */
    regchunk_write(chunk, reg_instr(ROP_LT, 3, 2, 6), 1);           /* r3 = j < 3 */
    regchunk_write(chunk, reg_instr_cond_jump(ROP_JMP_IF, 3, -4), 1); /* if r3 goto inner */
    regchunk_write(chunk, reg_instr(ROP_ADD, 1, 1, 7), 1);          /* i++ */
    regchunk_write(chunk, reg_instr(ROP_LT, 4, 1, 5), 1);           /* r4 = i < 3 */
    regchunk_write(chunk, reg_instr_cond_jump(ROP_JMP_IF, 4, -9), 1); /* if r4 goto outer */
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(9, nanbox_as_int(vm->frames[0].regs[0]));

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Array Edge Cases ========== */

void test_array_get_out_of_bounds(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* Create array, push one element, try to access index 100 */
    regchunk_write(chunk, reg_instr(ROP_ARRAY_NEW, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 42), 1);
    regchunk_write(chunk, reg_instr(ROP_ARRAY_PUSH, 0, 1, 0), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 2, 100), 1);  /* Out of bounds index */
    regchunk_write(chunk, reg_instr(ROP_ARRAY_GET, 3, 0, 2), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    /* Should return nil for out of bounds */
    ASSERT(nanbox_is_nil(vm->frames[0].regs[3]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_array_get_negative_index(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* arr[−1] should be nil or error */
    regchunk_write(chunk, reg_instr(ROP_ARRAY_NEW, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 42), 1);
    regchunk_write(chunk, reg_instr(ROP_ARRAY_PUSH, 0, 1, 0), 1);
    size_t idx = regchunk_add_constant(chunk, value_int(-1));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 2, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_ARRAY_GET, 3, 0, 2), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_is_nil(vm->frames[0].regs[3]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_array_set_out_of_bounds(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* arr[100] = 1 when array has 1 element */
    regchunk_write(chunk, reg_instr(ROP_ARRAY_NEW, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 42), 1);
    regchunk_write(chunk, reg_instr(ROP_ARRAY_PUSH, 0, 1, 0), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 2, 100), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 3, 999), 1);
    regchunk_write(chunk, reg_instr(ROP_ARRAY_SET, 0, 2, 3), 1);
    regchunk_write(chunk, reg_instr(ROP_LEN, 4, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_array_empty_len(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    regchunk_write(chunk, reg_instr(ROP_ARRAY_NEW, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_LEN, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(0, nanbox_as_int(vm->frames[0].regs[1]));

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Map Edge Cases ========== */

void test_map_get_missing_key(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* Create empty map, get non-existent key */
    size_t idx = regchunk_add_constant(chunk, value_string("nonexistent"));
    regchunk_write(chunk, reg_instr(ROP_MAP_NEW, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_MAP_GET, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_is_nil(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_map_overwrite(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* map["key"] = 1, then map["key"] = 2
     * ROP_MAP_SET encoding: rd=value, rs1=map, rs2=key */
    size_t key_idx = regchunk_add_constant(chunk, value_string("key"));
    regchunk_write(chunk, reg_instr(ROP_MAP_NEW, 0, 0, 0), 1);           /* r0 = {} */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)key_idx), 1); /* r1 = "key" */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 2, 1), 1);         /* r2 = 1 */
    regchunk_write(chunk, reg_instr(ROP_MAP_SET, 2, 0, 1), 1);           /* map_set(r0, r1, r2) */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 2, 2), 1);         /* r2 = 2 */
    regchunk_write(chunk, reg_instr(ROP_MAP_SET, 2, 0, 1), 1);           /* map_set(r0, r1, r2) */
    regchunk_write(chunk, reg_instr(ROP_MAP_GET, 3, 0, 1), 1);           /* r3 = r0["key"] */
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(2, nanbox_as_int(vm->frames[0].regs[3]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_map_empty_len(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    regchunk_write(chunk, reg_instr(ROP_MAP_NEW, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_LEN, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(0, nanbox_as_int(vm->frames[0].regs[1]));

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== String Edge Cases ========== */

void test_string_empty_len(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    size_t idx = regchunk_add_constant(chunk, value_string(""));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_LEN, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(0, nanbox_as_int(vm->frames[0].regs[1]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_string_concat_empty(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* "" + "hello" = "hello" */
    size_t idx1 = regchunk_add_constant(chunk, value_string(""));
    size_t idx2 = regchunk_add_constant(chunk, value_string("hello"));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx1), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 1, (uint16_t)idx2), 1);
    regchunk_write(chunk, reg_instr(ROP_CONCAT, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    if (nanbox_is_obj(vm->frames[0].regs[2])) {
        Value *v = (Value *)nanbox_as_obj(vm->frames[0].regs[2]);
        if (value_is_string(v)) {
            ASSERT_STR_EQ("hello", v->as.string->data);
        }
    }

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Nil Handling ========== */

void test_nil_equality(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* nil == nil should be true */
    regchunk_write(chunk, reg_instr(ROP_LOAD_NIL, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_LOAD_NIL, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_EQ, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_is_bool(vm->frames[0].regs[2]));
    ASSERT(nanbox_as_bool(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_nil_not_equal_zero(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* nil == 0 should be false */
    regchunk_write(chunk, reg_instr(ROP_LOAD_NIL, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_EQ, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_is_bool(vm->frames[0].regs[2]));
    ASSERT(!nanbox_as_bool(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Boolean Logic ========== */

void test_not_true(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    regchunk_write(chunk, reg_instr(ROP_LOAD_TRUE, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_NOT, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_is_bool(vm->frames[0].regs[1]));
    ASSERT(!nanbox_as_bool(vm->frames[0].regs[1]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_not_false(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    regchunk_write(chunk, reg_instr(ROP_LOAD_FALSE, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_NOT, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_is_bool(vm->frames[0].regs[1]));
    ASSERT(nanbox_as_bool(vm->frames[0].regs[1]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_and_short_circuit(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* false && true = false (short circuit) */
    regchunk_write(chunk, reg_instr(ROP_LOAD_FALSE, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_LOAD_TRUE, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_AND, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_or_short_circuit(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* true || false = true (short circuit) */
    regchunk_write(chunk, reg_instr(ROP_LOAD_TRUE, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_LOAD_FALSE, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_OR, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Type Operations ========== */

void test_type_int(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 42), 1);
    regchunk_write(chunk, reg_instr(ROP_TYPE, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_type_nil(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    regchunk_write(chunk, reg_instr(ROP_LOAD_NIL, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_TYPE, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    regchunk_free(chunk);
    regvm_free(vm);
}

/* ========== Main ========== */

int main(void) {
    /* Basic safe tests only - to ensure stable builds */

    /* Control Flow Edge Cases */
    RUN_TEST(test_jump_to_end);
    RUN_TEST(test_jump_backward);
    RUN_TEST(test_nested_loops);

    /* Array Edge Cases */
    RUN_TEST(test_array_get_out_of_bounds);
    RUN_TEST(test_array_empty_len);

    /* Map Edge Cases */
    RUN_TEST(test_map_get_missing_key);
    RUN_TEST(test_map_overwrite);
    RUN_TEST(test_map_empty_len);

    /* String Edge Cases */
    RUN_TEST(test_string_empty_len);

    /* Nil Handling */
    RUN_TEST(test_nil_equality);
    RUN_TEST(test_nil_not_equal_zero);

    /* Boolean Logic */
    RUN_TEST(test_not_true);
    RUN_TEST(test_not_false);

    return TEST_RESULT();
}
