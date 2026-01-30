/*
 * Agim - Register Compiler Tests
 *
 * Tests the register bytecode compiler by building AST nodes directly.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "lang/ast.h"
#include "lang/regcompiler.h"
#include "vm/regvm.h"
#include "vm/value.h"

#include <math.h>

/*============================================================================
 * Helper Functions
 *============================================================================*/

static NanValue run_chunk(RegChunk *chunk) {
    RegVM *vm = regvm_new();
    RegVMResult result = regvm_run(vm, chunk);

    NanValue ret = NANBOX_NIL;
    if (result == REGVM_HALT || result == REGVM_OK) {
        /* Get result from register 0 of the last frame */
        if (vm->frame_count > 0) {
            ret = vm->frames[vm->frame_count - 1].regs[0];
        } else {
            ret = vm->frames[0].regs[0];
        }
    }

    regvm_free(vm);
    return ret;
}

/*============================================================================
 * Expression Tests
 *============================================================================*/

void test_compile_int_literal(void) {
    AstNode *ast = ast_int(42, 1);
    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_int(result));
    ASSERT_EQ(42, nanbox_as_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_large_int(void) {
    /* Test integer that doesn't fit in 16-bit immediate */
    AstNode *ast = ast_int(100000, 1);
    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_int(result));
    ASSERT_EQ(100000, nanbox_as_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_float_literal(void) {
    AstNode *ast = ast_float(3.14, 1);
    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    /* Float might be stored as double in nanbox */
    double val = nanbox_to_float(result);
    ASSERT(fabs(val - 3.14) < 0.001);

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_bool_true(void) {
    AstNode *ast = ast_bool(true, 1);
    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_bool(result));
    ASSERT(nanbox_is_true(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_bool_false(void) {
    AstNode *ast = ast_bool(false, 1);
    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_bool(result));
    ASSERT(nanbox_is_false(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_nil_literal(void) {
    AstNode *ast = ast_nil(1);
    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_nil(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_add(void) {
    /* 2 + 3 */
    AstNode *left = ast_int(2, 1);
    AstNode *right = ast_int(3, 1);
    AstNode *ast = ast_binary(TOK_PLUS, left, right, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_int(result));
    ASSERT_EQ(5, nanbox_as_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_subtract(void) {
    /* 10 - 3 */
    AstNode *left = ast_int(10, 1);
    AstNode *right = ast_int(3, 1);
    AstNode *ast = ast_binary(TOK_MINUS, left, right, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_int(result));
    ASSERT_EQ(7, nanbox_as_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_multiply(void) {
    /* 6 * 7 */
    AstNode *left = ast_int(6, 1);
    AstNode *right = ast_int(7, 1);
    AstNode *ast = ast_binary(TOK_STAR, left, right, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_int(result));
    ASSERT_EQ(42, nanbox_as_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_divide(void) {
    /* 20 / 4 */
    AstNode *left = ast_int(20, 1);
    AstNode *right = ast_int(4, 1);
    AstNode *ast = ast_binary(TOK_SLASH, left, right, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    /* Division might return float or int depending on implementation */
    ASSERT_EQ(5, nanbox_to_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_complex_arithmetic(void) {
    /* (2 + 3) * 4 = 20 */
    AstNode *two = ast_int(2, 1);
    AstNode *three = ast_int(3, 1);
    AstNode *four = ast_int(4, 1);

    AstNode *add = ast_binary(TOK_PLUS, two, three, 1);
    AstNode *ast = ast_binary(TOK_STAR, add, four, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_int(result));
    ASSERT_EQ(20, nanbox_as_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_comparison_lt(void) {
    /* 5 < 10 */
    AstNode *left = ast_int(5, 1);
    AstNode *right = ast_int(10, 1);
    AstNode *ast = ast_binary(TOK_LT, left, right, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_bool(result));
    ASSERT(nanbox_is_true(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_comparison_gt(void) {
    /* 5 > 10 */
    AstNode *left = ast_int(5, 1);
    AstNode *right = ast_int(10, 1);
    AstNode *ast = ast_binary(TOK_GT, left, right, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_bool(result));
    ASSERT(nanbox_is_false(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_equality(void) {
    /* 5 == 5 */
    AstNode *left = ast_int(5, 1);
    AstNode *right = ast_int(5, 1);
    AstNode *ast = ast_binary(TOK_EQ, left, right, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_bool(result));
    ASSERT(nanbox_is_true(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_inequality(void) {
    /* 5 != 3 */
    AstNode *left = ast_int(5, 1);
    AstNode *right = ast_int(3, 1);
    AstNode *ast = ast_binary(TOK_NE, left, right, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_bool(result));
    ASSERT(nanbox_is_true(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_negation(void) {
    /* -42 */
    AstNode *operand = ast_int(42, 1);
    AstNode *ast = ast_unary(TOK_MINUS, operand, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_int(result));
    ASSERT_EQ(-42, nanbox_as_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_compile_not(void) {
    /* not true */
    AstNode *operand = ast_bool(true, 1);
    AstNode *ast = ast_unary(TOK_NOT, operand, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_bool(result));
    ASSERT(nanbox_is_false(result));

    regchunk_free(chunk);
    ast_free(ast);
}

/*============================================================================
 * Register Usage Tests
 *============================================================================*/

void test_registers_allocated(void) {
    /* 1 + 2 + 3 + 4 - should use registers efficiently */
    AstNode *one = ast_int(1, 1);
    AstNode *two = ast_int(2, 1);
    AstNode *three = ast_int(3, 1);
    AstNode *four = ast_int(4, 1);

    AstNode *add1 = ast_binary(TOK_PLUS, one, two, 1);
    AstNode *add2 = ast_binary(TOK_PLUS, add1, three, 1);
    AstNode *ast = ast_binary(TOK_PLUS, add2, four, 1);

    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    /* Check that registers are being used efficiently */
    ASSERT(chunk->num_regs > 0);
    ASSERT(chunk->num_regs <= 10);  /* Should not use too many */

    NanValue result = run_chunk(chunk);
    ASSERT(nanbox_is_int(result));
    ASSERT_EQ(10, nanbox_as_int(result));

    regchunk_free(chunk);
    ast_free(ast);
}

void test_code_generated(void) {
    AstNode *ast = ast_int(42, 1);
    RegChunk *chunk = regcompile_expr(ast);
    ASSERT(chunk != NULL);

    /* Should have at least 2 instructions: LOAD_INT and RET */
    ASSERT(chunk->code_size >= 2);

    regchunk_free(chunk);
    ast_free(ast);
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    printf("=== Register Compiler Tests ===\n");

    RUN_TEST(test_compile_int_literal);
    RUN_TEST(test_compile_large_int);
    RUN_TEST(test_compile_float_literal);
    RUN_TEST(test_compile_bool_true);
    RUN_TEST(test_compile_bool_false);
    RUN_TEST(test_compile_nil_literal);
    RUN_TEST(test_compile_add);
    RUN_TEST(test_compile_subtract);
    RUN_TEST(test_compile_multiply);
    RUN_TEST(test_compile_divide);
    RUN_TEST(test_compile_complex_arithmetic);
    RUN_TEST(test_compile_comparison_lt);
    RUN_TEST(test_compile_comparison_gt);
    RUN_TEST(test_compile_equality);
    RUN_TEST(test_compile_inequality);
    RUN_TEST(test_compile_negation);
    RUN_TEST(test_compile_not);
    RUN_TEST(test_registers_allocated);
    RUN_TEST(test_code_generated);

    return TEST_RESULT();
}
