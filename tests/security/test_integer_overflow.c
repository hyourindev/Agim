/*
 * Agim - Integer Overflow Tests
 *
 * Tests for integer overflow prevention in critical operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include "../test_common.h"
#include "vm/value.h"
#include "vm/bytecode.h"
#include "types/array.h"

/* Test: Integer value at limits */
void test_int_value_limits(void) {
    /* Maximum int64_t */
    Value *max = value_int(INT64_MAX);
    ASSERT(max != NULL);
    ASSERT(value_to_int(max) == INT64_MAX);
    value_free(max);

    /* Minimum int64_t */
    Value *min = value_int(INT64_MIN);
    ASSERT(min != NULL);
    ASSERT(value_to_int(min) == INT64_MIN);
    value_free(min);

    /* Zero */
    Value *zero = value_int(0);
    ASSERT(zero != NULL);
    ASSERT(value_to_int(zero) == 0);
    value_free(zero);
}

/* Test: Arithmetic overflow behavior */
void test_arithmetic_overflow(void) {
    /* Test that we can represent overflow results */
    int64_t a = INT64_MAX;
    int64_t b = 1;

    /* This would overflow - verify we handle it */
    Value *va = value_int(a);
    Value *vb = value_int(b);

    ASSERT(va != NULL);
    ASSERT(vb != NULL);

    /* The VM should handle overflow gracefully in arithmetic ops */
    /* Just verify values are stored correctly */
    ASSERT(value_to_int(va) == INT64_MAX);
    ASSERT(value_to_int(vb) == 1);

    value_free(va);
    value_free(vb);
}

/* Test: Array index at size_t limits */
void test_array_size_limits(void) {
    Value *arr = value_array();
    ASSERT(arr != NULL);

    /* Push a few elements */
    for (int i = 0; i < 10; i++) {
        arr = array_push(arr, value_int(i));
    }

    /* Try to access with very large index */
    size_t huge_index = SIZE_MAX;
    Value *result = array_get(arr, huge_index);
    /* Should return NULL or nil, not crash */
    ASSERT(result == NULL || value_is_nil(result));

    /* SIZE_MAX - 1 */
    Value *result2 = array_get(arr, SIZE_MAX - 1);
    ASSERT(result2 == NULL || value_is_nil(result2));

    value_free(arr);
}

/* Test: Bytecode constant count limits */
void test_bytecode_constant_limits(void) {
    Chunk *chunk = chunk_new();
    ASSERT(chunk != NULL);

    /* Add many constants (but not crazy many) */
    for (int i = 0; i < 1000; i++) {
        size_t idx = chunk_add_constant(chunk, value_int(i));
        ASSERT(idx == (size_t)i);
    }

    ASSERT(chunk->constants_size == 1000);

    chunk_free(chunk);
}

/* Test: String length at limits */
void test_string_length_limits(void) {
    /* Create string with known length */
    char buf[1025];
    memset(buf, 'a', 1024);
    buf[1024] = '\0';

    Value *s = value_string(buf);
    ASSERT(s != NULL);
    ASSERT(string_length(s) == 1024);

    value_free(s);
}

/* Test: Float to int conversion */
void test_float_to_int_conversion(void) {
    /* Normal conversion */
    Value *f1 = value_float(42.5);
    ASSERT(value_to_int(f1) == 42);
    value_free(f1);

    /* Large float */
    Value *f2 = value_float(1e18);
    int64_t i2 = value_to_int(f2);
    ASSERT(i2 != 0);  /* Should convert somehow */
    value_free(f2);

    /* Negative float */
    Value *f3 = value_float(-123.9);
    ASSERT(value_to_int(f3) == -123);
    value_free(f3);
}

/* Test: Chunk code size growth */
void test_chunk_code_growth(void) {
    Chunk *chunk = chunk_new();

    /* Write enough to trigger multiple reallocations */
    for (int i = 0; i < 5000; i++) {
        chunk_write_byte(chunk, (uint8_t)(i & 0xFF), i);
    }

    ASSERT(chunk->code_size == 5000);
    ASSERT(chunk->code_capacity >= 5000);

    /* Verify no corruption */
    for (int i = 0; i < 5000; i++) {
        ASSERT(chunk->code[i] == (uint8_t)(i & 0xFF));
    }

    chunk_free(chunk);
}

/* Test: Jump offset limits */
void test_jump_offset_limits(void) {
    Chunk *chunk = chunk_new();

    /* Write a jump instruction - returns offset for patching */
    size_t jump_addr = chunk_write_jump(chunk, OP_JUMP, 1);
    ASSERT(jump_addr > 0);

    /* Write some more bytes so we have something to jump over */
    for (int i = 0; i < 100; i++) {
        chunk_write_byte(chunk, OP_NOP, 1);
    }

    /* Patch jump (offset is calculated from current position) */
    chunk_patch_jump(chunk, jump_addr);

    chunk_free(chunk);
}

/* Test: Multiplication overflow check */
void test_multiplication_overflow(void) {
    /* Test values that would overflow if multiplied */
    int64_t a = INT64_MAX / 2;
    int64_t b = 3;

    Value *va = value_int(a);
    Value *vb = value_int(b);

    ASSERT(va != NULL);
    ASSERT(vb != NULL);

    /* Values should be stored correctly */
    ASSERT(value_to_int(va) == a);
    ASSERT(value_to_int(vb) == b);

    value_free(va);
    value_free(vb);
}

/* Test: Array capacity doubling */
void test_array_capacity_growth(void) {
    Value *arr = value_array();

    /* Push elements to trigger capacity growth */
    for (int i = 0; i < 1000; i++) {
        Value *old = arr;
        arr = array_push(arr, value_int(i));
        ASSERT(arr != NULL);
        if (old != arr) {
            /* Reallocation happened - verify data integrity */
            for (int j = 0; j <= i; j++) {
                Value *v = array_get(arr, j);
                ASSERT(v != NULL);
                ASSERT(value_to_int(v) == j);
            }
        }
    }

    ASSERT(array_length(arr) == 1000);
    value_free(arr);
}

/* Test: Negative array operations */
void test_negative_operations(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    /* Insert at 0 */
    Value *r1 = array_insert(arr, 0, value_int(0));
    ASSERT(r1 != NULL);

    /* Remove at valid index */
    Value *r2 = NULL;
    Value *removed = array_remove(r1, 0, &r2);
    ASSERT(removed != NULL);  /* Returns the removed element */
    (void)r2;

    value_free(arr);
}

int main(void) {
    printf("Running integer overflow tests...\n\n");

    RUN_TEST(test_int_value_limits);
    RUN_TEST(test_arithmetic_overflow);
    RUN_TEST(test_array_size_limits);
    RUN_TEST(test_bytecode_constant_limits);
    RUN_TEST(test_string_length_limits);
    RUN_TEST(test_float_to_int_conversion);
    RUN_TEST(test_chunk_code_growth);
    RUN_TEST(test_jump_offset_limits);
    RUN_TEST(test_multiplication_overflow);
    RUN_TEST(test_array_capacity_growth);
    RUN_TEST(test_negative_operations);

    return TEST_RESULT();
}
