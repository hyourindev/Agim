/*
 * Agim - Array Property Tests
 *
 * Property-based tests for array operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "property_test.h"
#include "types/array.h"
#include "vm/value.h"

/* Property: Array length increases by 1 after push */
static bool prop_array_push_increases_length(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    size_t initial_len = array_length(arr);

    int val = prop_rand_int_range(-1000, 1000);
    Value *elem = value_int(val);
    arr = array_push(arr, elem);

    PROP_ASSERT(arr != NULL);
    PROP_ASSERT(array_length(arr) == initial_len + 1);

    value_free(arr);
    return true;
}

/* Property: Array get after push returns pushed value */
static bool prop_array_push_get_roundtrip(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    int val = prop_rand_int_range(-10000, 10000);
    Value *elem = value_int(val);
    arr = array_push(arr, elem);

    PROP_ASSERT(arr != NULL);

    size_t last_idx = array_length(arr) - 1;
    Value *retrieved = array_get(arr, last_idx);

    PROP_ASSERT(retrieved != NULL);
    PROP_ASSERT(value_is_int(retrieved));
    PROP_ASSERT(value_to_int(retrieved) == val);

    value_free(retrieved);
    value_free(arr);
    return true;
}

/* Property: Array set then get returns set value */
static bool prop_array_set_get_roundtrip(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    /* Build array with some elements */
    int count = prop_rand_int_range(1, 10);
    for (int i = 0; i < count; i++) {
        Value *elem = value_int(i);
        arr = array_push(arr, elem);
    }

    /* Pick random index and set new value */
    size_t idx = prop_rand_size(count - 1);
    int new_val = prop_rand_int_range(-1000, 1000);
    Value *new_elem = value_int(new_val);
    Value *result = array_set(arr, idx, new_elem);

    PROP_ASSERT(result != NULL);

    /* Get should return the new value */
    Value *retrieved = array_get(result, idx);
    PROP_ASSERT(retrieved != NULL);
    PROP_ASSERT(value_to_int(retrieved) == new_val);

    value_free(retrieved);
    value_free(result);
    return true;
}

/* Property: Array length is preserved after set */
static bool prop_array_set_preserves_length(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    int count = prop_rand_int_range(1, 20);
    for (int i = 0; i < count; i++) {
        Value *elem = value_int(i);
        arr = array_push(arr, elem);
    }

    size_t len_before = array_length(arr);
    size_t idx = prop_rand_size(count - 1);
    Value *new_elem = value_int(999);
    Value *result = array_set(arr, idx, new_elem);

    PROP_ASSERT(result != NULL);
    PROP_ASSERT(array_length(result) == len_before);

    value_free(result);
    return true;
}

/* Property: Slice length equals end - start */
static bool prop_array_slice_length(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    int count = prop_rand_int_range(5, 20);
    for (int i = 0; i < count; i++) {
        Value *elem = value_int(i);
        arr = array_push(arr, elem);
    }

    /* Generate valid slice bounds */
    size_t start = prop_rand_size(count - 1);
    size_t end = start + prop_rand_size(count - start);
    if (end > (size_t)count) end = (size_t)count;

    Value *slice = array_slice(arr, start, end);
    PROP_ASSERT(slice != NULL);
    PROP_ASSERT(array_length(slice) == end - start);

    value_free(slice);
    value_free(arr);
    return true;
}

/* Property: Concat length equals sum of input lengths */
static bool prop_array_concat_length(void *ctx) {
    (void)ctx;

    Value *arr1 = value_array();
    Value *arr2 = value_array();
    PROP_ASSERT(arr1 != NULL && arr2 != NULL);

    int count1 = prop_rand_int_range(0, 10);
    int count2 = prop_rand_int_range(0, 10);

    for (int i = 0; i < count1; i++) {
        Value *elem = value_int(i);
        arr1 = array_push(arr1, elem);
    }

    for (int i = 0; i < count2; i++) {
        Value *elem = value_int(i + 100);
        arr2 = array_push(arr2, elem);
    }

    Value *concat = array_concat(arr1, arr2);
    PROP_ASSERT(concat != NULL);
    PROP_ASSERT(array_length(concat) == (size_t)(count1 + count2));

    value_free(concat);
    value_free(arr1);
    value_free(arr2);
    return true;
}

/* Property: Pop returns last element */
static bool prop_array_pop_returns_last(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    /* Push a single known value */
    int last_val = prop_rand_int_range(-1000, 1000);
    Value *elem = value_int(last_val);
    arr = array_push(arr, elem);
    PROP_ASSERT(arr != NULL);
    PROP_ASSERT(array_length(arr) == 1);

    /* Pop it */
    Value *result_arr = NULL;
    Value *popped = array_pop(arr, &result_arr);
    PROP_ASSERT(popped != NULL);
    PROP_ASSERT(value_is_int(popped));

    int popped_val = value_to_int(popped);
    PROP_ASSERT(popped_val == last_val);

    value_free(arr);
    return true;
}

/* Property: Contains returns true for pushed value */
static bool prop_array_contains_after_push(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    int val = prop_rand_int_range(-1000, 1000);
    Value *elem = value_int(val);
    arr = array_push(arr, elem);

    PROP_ASSERT(arr != NULL);

    Value *search = value_int(val);
    bool found = array_contains(arr, search);

    PROP_ASSERT(found);

    value_free(search);
    value_free(arr);
    return true;
}

/* Property: Empty array has length 0 */
static bool prop_array_empty_length_zero(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);
    PROP_ASSERT(array_length(arr) == 0);

    value_free(arr);
    return true;
}

/* Property: Multiple pushes maintain correct length */
static bool prop_array_multiple_push_length(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    int count = prop_rand_int_range(1, 50);
    for (int i = 0; i < count; i++) {
        Value *elem = value_int(i);
        arr = array_push(arr, elem);
    }

    PROP_ASSERT(array_length(arr) == (size_t)count);

    value_free(arr);
    return true;
}

/* Main */
int main(void) {
    printf("Running array property tests...\n\n");

    prop_init(0); /* Use random seed */

    printf("Array Property Tests:\n");
    PROP_CHECK("push increases length", prop_array_push_increases_length, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("push/get roundtrip", prop_array_push_get_roundtrip, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("set/get roundtrip", prop_array_set_get_roundtrip, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("set preserves length", prop_array_set_preserves_length, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("slice length", prop_array_slice_length, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("concat length", prop_array_concat_length, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("pop returns last", prop_array_pop_returns_last, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("contains after push", prop_array_contains_after_push, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("empty length zero", prop_array_empty_length_zero, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("multiple push length", prop_array_multiple_push_length, NULL, PROP_DEFAULT_ITERATIONS);

    PROP_SUMMARY();
    return PROP_RESULT();
}
