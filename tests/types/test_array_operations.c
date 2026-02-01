/*
 * Agim - Array Operations Tests
 *
 * Comprehensive tests for array type operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "types/array.h"
#include "vm/value.h"
#include <string.h>

/* Array Creation Tests */

void test_array_new_empty(void) {
    Value *arr = value_array();

    ASSERT(arr != NULL);
    ASSERT_EQ(VAL_ARRAY, arr->type);
    ASSERT_EQ(0, array_length(arr));

    value_free(arr);
}

void test_array_with_capacity(void) {
    Value *arr = value_array_with_capacity(100);

    ASSERT(arr != NULL);
    ASSERT_EQ(0, array_length(arr));
    ASSERT(array_capacity(arr) >= 100);

    value_free(arr);
}

/* Array Push Tests */

void test_array_push_single(void) {
    Value *arr = value_array();
    Value *item = value_int(42);

    Value *result = array_push(arr, item);

    ASSERT(result != NULL);
    ASSERT_EQ(1, array_length(result));

    value_free(result);
}

void test_array_push_multiple(void) {
    Value *arr = value_array();

    for (int i = 0; i < 10; i++) {
        /* array_push returns same array when refcount is 1 (modifies in place) */
        arr = array_push(arr, value_int(i));
    }

    ASSERT_EQ(10, array_length(arr));

    value_free(arr);
}

void test_array_push_grows_capacity(void) {
    Value *arr = value_array_with_capacity(2);
    size_t initial_cap = array_capacity(arr);

    /* Push beyond initial capacity */
    for (int i = 0; i < 10; i++) {
        /* array_push returns same array when refcount is 1 (modifies in place) */
        arr = array_push(arr, value_int(i));
    }

    ASSERT(array_capacity(arr) > initial_cap);
    ASSERT_EQ(10, array_length(arr));

    value_free(arr);
}

void test_array_push_returns_new_array(void) {
    Value *arr = value_array();
    Value *item = value_int(1);

    Value *result = array_push(arr, item);

    /* Push returns a new or same array (COW semantics) */
    ASSERT(result != NULL);
    ASSERT_EQ(1, array_length(result));

    value_free(result);
}

/* Array Get Tests */

void test_array_get_in_bounds(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(10));
    arr = array_push(arr, value_int(20));
    arr = array_push(arr, value_int(30));

    Value *v0 = array_get(arr, 0);
    Value *v1 = array_get(arr, 1);
    Value *v2 = array_get(arr, 2);

    ASSERT(v0 != NULL);
    ASSERT(v1 != NULL);
    ASSERT(v2 != NULL);
    ASSERT_EQ(10, v0->as.integer);
    ASSERT_EQ(20, v1->as.integer);
    ASSERT_EQ(30, v2->as.integer);

    value_free(arr);
}

void test_array_get_out_of_bounds(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));

    Value *v = array_get(arr, 10);

    ASSERT(v == NULL);

    value_free(arr);
}

void test_array_get_empty(void) {
    Value *arr = value_array();

    Value *v = array_get(arr, 0);

    ASSERT(v == NULL);

    value_free(arr);
}

/* Array Set Tests */

void test_array_set_in_bounds(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    Value *result = array_set(arr, 1, value_int(200));

    ASSERT(result != NULL);
    ASSERT_EQ(200, array_get(result, 1)->as.integer);

    value_free(result);
}

void test_array_set_out_of_bounds(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));

    Value *result = array_set(arr, 10, value_int(100));

    /* Out of bounds set returns the array unchanged (no modification) */
    ASSERT(result != NULL);
    ASSERT_EQ(1, array_length(result)); /* Length unchanged */
    ASSERT_EQ(1, array_get(result, 0)->as.integer); /* Original value unchanged */

    value_free(result);
}

/* Array Length Tests */

void test_array_length_empty(void) {
    Value *arr = value_array();

    ASSERT_EQ(0, array_length(arr));

    value_free(arr);
}

void test_array_length_after_push(void) {
    Value *arr = value_array();

    ASSERT_EQ(0, array_length(arr));

    arr = array_push(arr, value_int(1));
    ASSERT_EQ(1, array_length(arr));

    arr = array_push(arr, value_int(2));
    ASSERT_EQ(2, array_length(arr));

    value_free(arr);
}

/* Array Pop Tests */

void test_array_pop_single(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(42));

    Value *new_arr = NULL;
    Value *popped = array_pop(arr, &new_arr);

    ASSERT(popped != NULL);
    ASSERT_EQ(42, popped->as.integer);
    ASSERT(new_arr != NULL);
    ASSERT_EQ(0, array_length(new_arr));

    value_free(new_arr);
}

void test_array_pop_multiple(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    Value *new_arr = NULL;
    Value *popped = array_pop(arr, &new_arr);

    ASSERT_EQ(3, popped->as.integer);
    ASSERT_EQ(2, array_length(new_arr));

    value_free(new_arr);
}

void test_array_pop_empty(void) {
    Value *arr = value_array();

    Value *new_arr = NULL;
    Value *popped = array_pop(arr, &new_arr);

    ASSERT(popped == NULL);

    value_free(arr);
}

/* Array Slice Tests */

void test_array_slice_basic(void) {
    Value *arr = value_array();
    for (int i = 0; i < 5; i++) {
        arr = array_push(arr, value_int(i * 10));
    }

    Value *slice = array_slice(arr, 1, 4);

    ASSERT(slice != NULL);
    ASSERT_EQ(3, array_length(slice));
    ASSERT_EQ(10, array_get(slice, 0)->as.integer);
    ASSERT_EQ(20, array_get(slice, 1)->as.integer);
    ASSERT_EQ(30, array_get(slice, 2)->as.integer);

    value_free(arr);
    value_free(slice);
}

void test_array_slice_empty(void) {
    Value *arr = value_array();
    for (int i = 0; i < 5; i++) {
        arr = array_push(arr, value_int(i));
    }

    Value *slice = array_slice(arr, 2, 2);

    ASSERT(slice != NULL);
    ASSERT_EQ(0, array_length(slice));

    value_free(arr);
    value_free(slice);
}

void test_array_slice_full(void) {
    Value *arr = value_array();
    for (int i = 0; i < 3; i++) {
        arr = array_push(arr, value_int(i));
    }

    Value *slice = array_slice(arr, 0, 3);

    ASSERT(slice != NULL);
    ASSERT_EQ(3, array_length(slice));

    value_free(arr);
    value_free(slice);
}

/* Array Concat Tests */

void test_array_concat_basic(void) {
    Value *a = value_array();
    a = array_push(a, value_int(1));
    a = array_push(a, value_int(2));

    Value *b = value_array();
    b = array_push(b, value_int(3));
    b = array_push(b, value_int(4));

    Value *result = array_concat(a, b);

    ASSERT(result != NULL);
    ASSERT_EQ(4, array_length(result));
    ASSERT_EQ(1, array_get(result, 0)->as.integer);
    ASSERT_EQ(2, array_get(result, 1)->as.integer);
    ASSERT_EQ(3, array_get(result, 2)->as.integer);
    ASSERT_EQ(4, array_get(result, 3)->as.integer);

    value_free(a);
    value_free(b);
    value_free(result);
}

void test_array_concat_empty_left(void) {
    Value *a = value_array();
    Value *b = value_array();
    b = array_push(b, value_int(1));

    Value *result = array_concat(a, b);

    ASSERT(result != NULL);
    ASSERT_EQ(1, array_length(result));

    value_free(a);
    value_free(b);
    value_free(result);
}

void test_array_concat_empty_right(void) {
    Value *a = value_array();
    a = array_push(a, value_int(1));
    Value *b = value_array();

    Value *result = array_concat(a, b);

    ASSERT(result != NULL);
    ASSERT_EQ(1, array_length(result));

    value_free(a);
    value_free(b);
    value_free(result);
}

/* Array Find Tests */

void test_array_find_exists(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(10));
    arr = array_push(arr, value_int(20));
    arr = array_push(arr, value_int(30));

    int64_t idx = array_find(arr, value_int(20));

    ASSERT_EQ(1, idx);

    value_free(arr);
}

void test_array_find_not_exists(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(10));
    arr = array_push(arr, value_int(20));

    int64_t idx = array_find(arr, value_int(99));

    ASSERT_EQ(-1, idx);

    value_free(arr);
}

void test_array_find_empty(void) {
    Value *arr = value_array();

    int64_t idx = array_find(arr, value_int(1));

    ASSERT_EQ(-1, idx);

    value_free(arr);
}

/* Array Contains Tests */

void test_array_contains_true(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    ASSERT(array_contains(arr, value_int(2)));

    value_free(arr);
}

void test_array_contains_false(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));

    ASSERT(!array_contains(arr, value_int(99)));

    value_free(arr);
}

/* Array Iteration Tests */

void test_array_iteration(void) {
    Value *arr = value_array();
    for (int i = 0; i < 5; i++) {
        arr = array_push(arr, value_int(i * 2));
    }

    Value **data = array_data(arr);
    size_t len = array_length(arr);

    int sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i]->as.integer;
    }

    ASSERT_EQ(20, sum); /* 0 + 2 + 4 + 6 + 8 = 20 */

    value_free(arr);
}

/* Array Clear Tests */

void test_array_clear(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    Value *cleared = array_clear(arr);

    ASSERT(cleared != NULL);
    ASSERT_EQ(0, array_length(cleared));

    value_free(cleared);
}

/* Array Insert Tests */

void test_array_insert_middle(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(3));

    Value *result = array_insert(arr, 1, value_int(2));

    ASSERT(result != NULL);
    ASSERT_EQ(3, array_length(result));
    ASSERT_EQ(1, array_get(result, 0)->as.integer);
    ASSERT_EQ(2, array_get(result, 1)->as.integer);
    ASSERT_EQ(3, array_get(result, 2)->as.integer);

    value_free(result);
}

void test_array_insert_start(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    Value *result = array_insert(arr, 0, value_int(1));

    ASSERT(result != NULL);
    ASSERT_EQ(3, array_length(result));
    ASSERT_EQ(1, array_get(result, 0)->as.integer);

    value_free(result);
}

/* Array Remove Tests */

void test_array_remove_middle(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    Value *new_arr = NULL;
    Value *removed = array_remove(arr, 1, &new_arr);

    ASSERT(removed != NULL);
    ASSERT_EQ(2, removed->as.integer);
    ASSERT(new_arr != NULL);
    ASSERT_EQ(2, array_length(new_arr));
    ASSERT_EQ(1, array_get(new_arr, 0)->as.integer);
    ASSERT_EQ(3, array_get(new_arr, 1)->as.integer);

    value_free(new_arr);
}

/* Array Reverse Tests */

void test_array_reverse(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    Value *reversed = array_reverse(arr);

    ASSERT(reversed != NULL);
    ASSERT_EQ(3, array_length(reversed));
    ASSERT_EQ(3, array_get(reversed, 0)->as.integer);
    ASSERT_EQ(2, array_get(reversed, 1)->as.integer);
    ASSERT_EQ(1, array_get(reversed, 2)->as.integer);

    value_free(reversed);
}

/* Null Input Tests */

void test_array_null_inputs(void) {
    /* Functions should handle NULL gracefully without crashing */
    ASSERT_EQ(0, array_length(NULL));
    ASSERT(array_get(NULL, 0) == NULL);

    /* array_set returns input when NULL - defensive behavior */
    ASSERT(array_set(NULL, 0, value_int(1)) == NULL);

    /* array_push returns input when NULL - defensive behavior */
    ASSERT(array_push(NULL, value_int(1)) == NULL);

    /* array_slice returns nil when NULL - defensive behavior */
    Value *slice_result = array_slice(NULL, 0, 1);
    ASSERT(slice_result != NULL);
    ASSERT(value_is_nil(slice_result));
    value_free(slice_result);

    /* array_concat returns empty array when NULL - defensive behavior */
    Value *concat_result = array_concat(NULL, NULL);
    ASSERT(concat_result != NULL);
    ASSERT_EQ(VAL_ARRAY, concat_result->type);
    ASSERT_EQ(0, array_length(concat_result));
    value_free(concat_result);

    ASSERT_EQ(-1, array_find(NULL, value_int(1)));
    ASSERT(!array_contains(NULL, value_int(1)));
}

/* Main */

int main(void) {
    printf("Running array operations tests...\n\n");

    printf("Array Creation Tests:\n");
    RUN_TEST(test_array_new_empty);
    RUN_TEST(test_array_with_capacity);

    printf("\nArray Push Tests:\n");
    RUN_TEST(test_array_push_single);
    RUN_TEST(test_array_push_multiple);
    RUN_TEST(test_array_push_grows_capacity);
    RUN_TEST(test_array_push_returns_new_array);

    printf("\nArray Get Tests:\n");
    RUN_TEST(test_array_get_in_bounds);
    RUN_TEST(test_array_get_out_of_bounds);
    RUN_TEST(test_array_get_empty);

    printf("\nArray Set Tests:\n");
    RUN_TEST(test_array_set_in_bounds);
    RUN_TEST(test_array_set_out_of_bounds);

    printf("\nArray Length Tests:\n");
    RUN_TEST(test_array_length_empty);
    RUN_TEST(test_array_length_after_push);

    printf("\nArray Pop Tests:\n");
    RUN_TEST(test_array_pop_single);
    RUN_TEST(test_array_pop_multiple);
    RUN_TEST(test_array_pop_empty);

    printf("\nArray Slice Tests:\n");
    RUN_TEST(test_array_slice_basic);
    RUN_TEST(test_array_slice_empty);
    RUN_TEST(test_array_slice_full);

    printf("\nArray Concat Tests:\n");
    RUN_TEST(test_array_concat_basic);
    RUN_TEST(test_array_concat_empty_left);
    RUN_TEST(test_array_concat_empty_right);

    printf("\nArray Find Tests:\n");
    RUN_TEST(test_array_find_exists);
    RUN_TEST(test_array_find_not_exists);
    RUN_TEST(test_array_find_empty);

    printf("\nArray Contains Tests:\n");
    RUN_TEST(test_array_contains_true);
    RUN_TEST(test_array_contains_false);

    printf("\nArray Iteration Tests:\n");
    RUN_TEST(test_array_iteration);

    printf("\nArray Clear Tests:\n");
    RUN_TEST(test_array_clear);

    printf("\nArray Insert Tests:\n");
    RUN_TEST(test_array_insert_middle);
    RUN_TEST(test_array_insert_start);

    printf("\nArray Remove Tests:\n");
    RUN_TEST(test_array_remove_middle);

    printf("\nArray Reverse Tests:\n");
    RUN_TEST(test_array_reverse);

    printf("\nNull Input Tests:\n");
    RUN_TEST(test_array_null_inputs);

    return TEST_RESULT();
}
