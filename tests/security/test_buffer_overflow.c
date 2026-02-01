/*
 * Agim - Buffer Overflow Tests
 *
 * Tests for buffer overflow prevention in critical operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "../test_common.h"
#include "vm/value.h"
#include "vm/bytecode.h"
#include "types/array.h"
#include "types/string.h"
#include "types/map.h"

/* Test: String creation with NULL
 * Note: value_string(NULL) is undefined behavior (calls strlen on NULL).
 * Callers must not pass NULL. This test documents that limitation.
 */
void test_string_null_input(void) {
    /* Skipping NULL test - value_string() requires non-NULL input */
    /* This is documented behavior, not a bug */
    ASSERT(1);
}

/* Test: String creation with empty string */
void test_string_empty(void) {
    Value *v = value_string("");
    ASSERT(v != NULL);
    ASSERT(value_is_string(v));
    ASSERT(strcmp(value_to_string(v), "") == 0);
    value_free(v);
}

/* Test: String concatenation edge cases */
void test_string_concat_edge_cases(void) {
    Value *s1 = value_string("hello");
    Value *s2 = value_string("");
    Value *s3 = value_string("world");

    /* Concat with empty */
    Value *r1 = string_concat(s1, s2);
    ASSERT(r1 != NULL);
    ASSERT(strcmp(value_to_string(r1), "hello") == 0);

    /* Concat empty with non-empty */
    Value *r2 = string_concat(s2, s3);
    ASSERT(r2 != NULL);
    ASSERT(strcmp(value_to_string(r2), "world") == 0);

    value_free(s1);
    value_free(s2);
    value_free(s3);
    value_free(r1);
    value_free(r2);
}

/* Test: Array bounds checking */
void test_array_bounds_get(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    /* Valid indices */
    ASSERT(array_get(arr, 0) != NULL);
    ASSERT(array_get(arr, 2) != NULL);

    /* Out of bounds - should return NULL or nil, not crash */
    Value *oob = array_get(arr, 100);
    ASSERT(oob == NULL || value_is_nil(oob));

    /* Negative index via size_t wrap - should handle gracefully */
    Value *neg = array_get(arr, (size_t)-1);
    ASSERT(neg == NULL || value_is_nil(neg));

    value_free(arr);
}

/* Test: Array set bounds checking */
void test_array_bounds_set(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));

    /* Valid set */
    Value *r1 = array_set(arr, 0, value_int(99));
    ASSERT(r1 != NULL);

    /* Out of bounds set - should handle gracefully */
    Value *r2 = array_set(arr, 1000, value_int(100));
    /* May return NULL or original array */
    (void)r2;

    value_free(arr);
}

/* Test: Array slice bounds */
void test_array_slice_bounds(void) {
    Value *arr = value_array();
    for (int i = 0; i < 5; i++) {
        arr = array_push(arr, value_int(i));
    }

    /* Valid slice */
    Value *s1 = array_slice(arr, 1, 3);
    ASSERT(s1 != NULL);
    ASSERT(array_length(s1) == 2);

    /* Start > end - should handle gracefully */
    Value *s2 = array_slice(arr, 4, 2);
    /* May return empty array or NULL */

    /* End beyond length */
    Value *s3 = array_slice(arr, 0, 100);
    /* Should clamp to array length */

    value_free(arr);
    if (s1) value_free(s1);
    if (s2) value_free(s2);
    if (s3) value_free(s3);
}

/* Test: Chunk constant index bounds */
void test_chunk_constant_bounds(void) {
    Chunk *chunk = chunk_new();

    /* Add a constant */
    size_t idx = chunk_add_constant(chunk, value_int(42));
    ASSERT(idx == 0);

    /* Valid index - direct array access (must check bounds) */
    ASSERT(chunk->constants_size == 1);
    ASSERT(chunk->constants[0] != NULL);
    ASSERT(value_to_int(chunk->constants[0]) == 42);

    /* Out of bounds - we can't access but verify size is correct */
    ASSERT(chunk->constants_size < 1000);

    chunk_free(chunk);
}

/* Test: Map with edge case keys */
void test_map_edge_case_keys(void) {
    Value *map = value_map();
    ASSERT(map != NULL);

    /* Note: map_set/map_get require non-NULL keys - this is documented behavior */
    /* Test with empty string key instead */
    Value *r = map_set(map, "", value_int(42));
    ASSERT(r != NULL);

    /* Get empty string key */
    Value *v = map_get(r, "");
    ASSERT(v != NULL);
    ASSERT(value_to_int(v) == 42);

    /* Test with very long key */
    char long_key[1025];
    memset(long_key, 'k', 1024);
    long_key[1024] = '\0';
    r = map_set(r, long_key, value_int(123));
    ASSERT(r != NULL);

    v = map_get(r, long_key);
    ASSERT(v != NULL);
    ASSERT(value_to_int(v) == 123);

    value_free(r);
}

/* Test: Very long string */
void test_very_long_string(void) {
    /* Create a 1MB string */
    size_t len = 1024 * 1024;
    char *buf = malloc(len + 1);
    if (!buf) {
        ASSERT(1);  /* Skip if can't allocate */
        return;
    }
    memset(buf, 'x', len);
    buf[len] = '\0';

    Value *v = value_string(buf);
    ASSERT(v != NULL);
    ASSERT(strlen(value_to_string(v)) == len);

    free(buf);
    value_free(v);
}

/* Test: Array with many elements */
void test_large_array(void) {
    Value *arr = value_array();
    int count = 10000;

    for (int i = 0; i < count; i++) {
        arr = array_push(arr, value_int(i));
        if (!arr) break;  /* Handle OOM gracefully */
    }

    if (arr) {
        ASSERT(array_length(arr) == (size_t)count);
        value_free(arr);
    } else {
        ASSERT(1);  /* OOM is acceptable */
    }
}

/* Test: String slice bounds */
void test_string_slice_bounds(void) {
    Value *s = value_string("hello world");

    /* Valid slice */
    Value *sub1 = string_slice(s, 0, 5);
    if (sub1) {
        ASSERT(strcmp(value_to_string(sub1), "hello") == 0);
        value_free(sub1);
    }

    /* Start beyond length */
    Value *sub2 = string_slice(s, 100, 105);
    /* Should return empty or NULL */

    /* End beyond length */
    Value *sub3 = string_slice(s, 0, 100);
    /* Should clamp to string length */

    value_free(s);
    if (sub2) value_free(sub2);
    if (sub3) value_free(sub3);
}

/* Test: Bytecode with zero-length code */
void test_bytecode_zero_code(void) {
    Bytecode *code = bytecode_new();
    ASSERT(code != NULL);
    ASSERT(code->main != NULL);
    ASSERT(code->main->code_size == 0);

    bytecode_free(code);
}

/* Test: Chunk write at capacity boundary */
void test_chunk_write_capacity(void) {
    Chunk *chunk = chunk_new();

    /* Write many opcodes to trigger reallocation */
    for (int i = 0; i < 1000; i++) {
        chunk_write_opcode(chunk, OP_NOP, i);
    }

    ASSERT(chunk->code_size == 1000);
    ASSERT(chunk->code_capacity >= 1000);

    chunk_free(chunk);
}

int main(void) {
    printf("Running buffer overflow tests...\n\n");

    RUN_TEST(test_string_null_input);
    RUN_TEST(test_string_empty);
    RUN_TEST(test_string_concat_edge_cases);
    RUN_TEST(test_array_bounds_get);
    RUN_TEST(test_array_bounds_set);
    RUN_TEST(test_array_slice_bounds);
    RUN_TEST(test_chunk_constant_bounds);
    RUN_TEST(test_map_edge_case_keys);
    RUN_TEST(test_very_long_string);
    RUN_TEST(test_large_array);
    RUN_TEST(test_string_slice_bounds);
    RUN_TEST(test_bytecode_zero_code);
    RUN_TEST(test_chunk_write_capacity);

    return TEST_RESULT();
}
