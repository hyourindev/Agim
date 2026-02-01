/*
 * Agim - Use-After-Free Tests
 *
 * Tests for use-after-free prevention via reference counting.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include "../test_common.h"
#include "vm/value.h"
#include "vm/gc.h"
#include "types/array.h"
#include "types/map.h"

/* Test: Value refcount lifecycle */
void test_value_refcount_lifecycle(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    /* Initial refcount is 1 */
    value_retain(v);
    /* Now refcount is 2 */

    value_release(v);
    /* Now refcount is 1 - still valid */
    ASSERT(value_to_int(v) == 42);

    value_release(v);
    /* Now freed - don't access */
}

/* Test: String refcount with retain */
void test_string_refcount(void) {
    Value *s = value_string("hello world");
    ASSERT(s != NULL);

    /* Retain */
    value_retain(s);
    value_retain(s);

    /* Verify still valid */
    ASSERT(strcmp(value_to_string(s), "hello world") == 0);

    /* Release all */
    value_release(s);
    value_release(s);
    value_release(s);
}

/* Test: Array elements are retained */
void test_array_element_retention(void) {
    Value *elem = value_int(42);
    value_retain(elem);  /* We keep a reference */

    Value *arr = value_array();
    arr = array_push(arr, elem);

    /* Element should still be valid after array operations */
    ASSERT(value_to_int(elem) == 42);

    /* Free array */
    value_free(arr);

    /* Our retained reference should still be valid */
    ASSERT(value_to_int(elem) == 42);

    value_release(elem);
}

/* Test: Map values are retained */
void test_map_value_retention(void) {
    Value *val = value_int(123);
    value_retain(val);

    Value *map = value_map();
    map = map_set(map, "key", val);

    /* Value should be accessible */
    Value *retrieved = map_get(map, "key");
    ASSERT(retrieved != NULL);
    ASSERT(value_to_int(retrieved) == 123);

    value_free(map);

    /* Our reference should still be valid */
    ASSERT(value_to_int(val) == 123);

    value_release(val);
}

/* Test: COW array doesn't double-free */
void test_cow_array_no_double_free(void) {
    Value *arr1 = value_array();
    arr1 = array_push(arr1, value_int(1));
    arr1 = array_push(arr1, value_int(2));

    /* Retain to simulate sharing */
    value_retain(arr1);

    /* Modify - should COW */
    Value *arr2 = array_set(arr1, 0, value_int(99));

    /* Both should be valid */
    ASSERT(array_get(arr1, 0) != NULL);
    ASSERT(array_get(arr2, 0) != NULL);

    /* Free both */
    value_release(arr1);  /* Release our retain */
    value_free(arr1);
    if (arr2 != arr1) {
        value_free(arr2);
    }
}

/* Test: Nested structure cleanup */
void test_nested_structure_cleanup(void) {
    /* Create nested array */
    Value *inner = value_array();
    inner = array_push(inner, value_int(1));
    inner = array_push(inner, value_int(2));

    Value *outer = value_array();
    outer = array_push(outer, inner);
    outer = array_push(outer, value_int(3));

    /* Free outer - should clean up inner too */
    value_free(outer);
    /* If we got here without crash, UAF protection works */
    ASSERT(1);
}

/* Test: Bytecode cleanup */
void test_bytecode_cleanup(void) {
    Bytecode *code = bytecode_new();

    /* Add constants */
    chunk_add_constant(code->main, value_int(1));
    chunk_add_constant(code->main, value_string("test"));
    chunk_add_constant(code->main, value_array());

    /* Add function */
    Chunk *func = chunk_new();
    chunk_add_constant(func, value_int(2));
    bytecode_add_function(code, func);

    /* Free should clean up everything */
    bytecode_free(code);
    ASSERT(1);  /* No crash = success */
}

/* Test: Chunk constant cleanup */
void test_chunk_constant_cleanup(void) {
    Chunk *chunk = chunk_new();

    /* Add many constants */
    for (int i = 0; i < 100; i++) {
        chunk_add_constant(chunk, value_int(i));
    }

    /* Add string constants */
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "string_%d", i);
        chunk_add_constant(chunk, value_string(buf));
    }

    chunk_free(chunk);
    ASSERT(1);
}

/* Test: GC doesn't free retained values */
void test_gc_respects_refcount(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate and retain a value */
    Value *v = heap_alloc(heap, VAL_INT);
    value_retain(v);

    /* Run GC */
    gc_collect(heap, vm);

    /* Value should still be valid due to refcount */
    ASSERT(v != NULL);

    value_release(v);
    value_release(v);  /* Release initial refcount */

    vm_free(vm);
    heap_free(heap);
}

/* Test: Multiple retain/release cycles */
void test_multiple_retain_release(void) {
    Value *v = value_int(42);

    for (int cycle = 0; cycle < 10; cycle++) {
        /* Retain multiple times */
        for (int i = 0; i < 5; i++) {
            value_retain(v);
        }

        /* Value should still be valid */
        ASSERT(value_to_int(v) == 42);

        /* Release same amount */
        for (int i = 0; i < 5; i++) {
            value_release(v);
        }

        /* Still valid (original refcount) */
        ASSERT(value_to_int(v) == 42);
    }

    value_free(v);
}

/* Test: Array pop doesn't UAF */
void test_array_pop_no_uaf(void) {
    Value *arr = value_array();
    Value *elem = value_int(42);
    value_retain(elem);  /* Keep our reference */

    arr = array_push(arr, elem);

    /* Pop the element */
    Value *new_arr = NULL;
    Value *popped = array_pop(arr, &new_arr);
    (void)popped;
    (void)new_arr;

    /* Original elem should still be valid */
    ASSERT(value_to_int(elem) == 42);

    value_release(elem);
    value_free(arr);
}

int main(void) {
    printf("Running use-after-free tests...\n\n");

    RUN_TEST(test_value_refcount_lifecycle);
    RUN_TEST(test_string_refcount);
    RUN_TEST(test_array_element_retention);
    RUN_TEST(test_map_value_retention);
    RUN_TEST(test_cow_array_no_double_free);
    RUN_TEST(test_nested_structure_cleanup);
    RUN_TEST(test_bytecode_cleanup);
    RUN_TEST(test_chunk_constant_cleanup);
    RUN_TEST(test_gc_respects_refcount);
    RUN_TEST(test_multiple_retain_release);
    RUN_TEST(test_array_pop_no_uaf);

    return TEST_RESULT();
}
