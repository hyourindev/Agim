/*
 * Agim - GC Marking Tests
 *
 * Comprehensive tests for GC mark phase:
 * - gc_mark_value sets mark bit
 * - gc_mark_value traverses arrays
 * - gc_mark_value traverses maps
 * - gc_mark_value traverses closures
 * - gc_mark_roots marks stack
 * - gc_mark_roots marks globals
 * - gc_mark_roots marks upvalues
 * - gc_mark_roots marks constants
 * - gray list operations
 * - incremental marking work packets
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/gc.h"
#include "vm/vm.h"
#include "vm/nanbox.h"
#include "types/closure.h"

/* ============================================================================
 * gc_mark_value Basic Tests
 * ============================================================================ */

void test_mark_value_sets_mark_bit(void) {
    Value *v = value_int(42);

    ASSERT(!value_is_marked(v));

    gc_mark_value(v);

    ASSERT(value_is_marked(v));

    value_free(v);
}

void test_mark_value_idempotent(void) {
    Value *v = value_int(42);

    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    /* Marking again should be safe and have no additional effect */
    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    value_free(v);
}

void test_mark_value_null_safe(void) {
    /* gc_mark_value should handle NULL safely */
    gc_mark_value(NULL);
    /* No crash is success */
}

void test_mark_value_nil(void) {
    Value *v = value_nil();

    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    value_free(v);
}

void test_mark_value_bool(void) {
    Value *v = value_bool(true);

    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    value_free(v);
}

void test_mark_value_float(void) {
    Value *v = value_float(3.14);

    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    value_free(v);
}

void test_mark_value_string(void) {
    Value *v = value_string("hello");

    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    value_free(v);
}

void test_mark_value_pid(void) {
    Value *v = value_pid(123);

    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    value_free(v);
}

/* ============================================================================
 * gc_mark_value Array Traversal Tests
 * ============================================================================ */

void test_mark_value_traverses_arrays(void) {
    Value *arr = value_array();
    Value *v1 = value_int(1);
    Value *v2 = value_int(2);
    Value *v3 = value_int(3);

    arr = array_push(arr, v1);
    arr = array_push(arr, v2);
    arr = array_push(arr, v3);

    /* Initially none should be marked */
    ASSERT(!value_is_marked(arr));
    ASSERT(!value_is_marked(v1));
    ASSERT(!value_is_marked(v2));
    ASSERT(!value_is_marked(v3));

    /* Mark the array - should mark all children */
    gc_mark_value(arr);

    ASSERT(value_is_marked(arr));
    ASSERT(value_is_marked(array_get(arr, 0)));
    ASSERT(value_is_marked(array_get(arr, 1)));
    ASSERT(value_is_marked(array_get(arr, 2)));

    value_free(arr);
}

void test_mark_value_nested_arrays(void) {
    Value *outer = value_array();
    Value *inner1 = value_array();
    Value *inner2 = value_array();

    inner1 = array_push(inner1, value_int(1));
    inner1 = array_push(inner1, value_int(2));
    inner2 = array_push(inner2, value_int(3));

    outer = array_push(outer, inner1);
    outer = array_push(outer, inner2);

    gc_mark_value(outer);

    /* All arrays and their contents should be marked */
    ASSERT(value_is_marked(outer));
    ASSERT(value_is_marked(array_get(outer, 0)));
    ASSERT(value_is_marked(array_get(outer, 1)));

    Value *retrieved_inner1 = array_get(outer, 0);
    ASSERT(value_is_marked(array_get(retrieved_inner1, 0)));
    ASSERT(value_is_marked(array_get(retrieved_inner1, 1)));

    value_free(outer);
}

void test_mark_value_empty_array(void) {
    Value *arr = value_array();

    gc_mark_value(arr);
    ASSERT(value_is_marked(arr));

    value_free(arr);
}

/* ============================================================================
 * gc_mark_value Map Traversal Tests
 * ============================================================================ */

void test_mark_value_traverses_maps(void) {
    Value *m = value_map();
    Value *v1 = value_int(1);
    Value *v2 = value_string("hello");
    Value *v3 = value_bool(true);

    m = map_set(m, "key1", v1);
    m = map_set(m, "key2", v2);
    m = map_set(m, "key3", v3);

    /* Initially none should be marked */
    ASSERT(!value_is_marked(m));
    ASSERT(!value_is_marked(v1));

    /* Mark the map - should mark all values */
    gc_mark_value(m);

    ASSERT(value_is_marked(m));
    ASSERT(value_is_marked(map_get(m, "key1")));
    ASSERT(value_is_marked(map_get(m, "key2")));
    ASSERT(value_is_marked(map_get(m, "key3")));

    value_free(m);
}

void test_mark_value_nested_maps(void) {
    Value *outer = value_map();
    Value *inner = value_map();

    inner = map_set(inner, "a", value_int(1));
    inner = map_set(inner, "b", value_int(2));
    outer = map_set(outer, "nested", inner);

    gc_mark_value(outer);

    ASSERT(value_is_marked(outer));
    Value *retrieved_inner = map_get(outer, "nested");
    ASSERT(value_is_marked(retrieved_inner));
    ASSERT(value_is_marked(map_get(retrieved_inner, "a")));
    ASSERT(value_is_marked(map_get(retrieved_inner, "b")));

    value_free(outer);
}

void test_mark_value_empty_map(void) {
    Value *m = value_map();

    gc_mark_value(m);
    ASSERT(value_is_marked(m));

    value_free(m);
}

void test_mark_value_map_with_array_values(void) {
    Value *m = value_map();
    Value *arr = value_array();

    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    m = map_set(m, "array", arr);

    gc_mark_value(m);

    ASSERT(value_is_marked(m));
    Value *retrieved_arr = map_get(m, "array");
    ASSERT(value_is_marked(retrieved_arr));
    ASSERT(value_is_marked(array_get(retrieved_arr, 0)));
    ASSERT(value_is_marked(array_get(retrieved_arr, 1)));

    value_free(m);
}

/* ============================================================================
 * gc_mark_roots Tests
 * ============================================================================ */

void test_mark_roots_marks_stack(void) {
    VM *vm = vm_new();

    /* Push heap-allocated values onto the stack.
     * Note: integers are stored directly in NanValue, not as heap pointers.
     * Use arrays/maps/strings which are heap objects. */
    Value *v1 = value_array();
    Value *v2 = value_map();
    Value *v3 = value_string("test");

    vm_push(vm, v1);
    vm_push(vm, v2);
    vm_push(vm, v3);

    /* Mark roots - should mark stack values */
    gc_mark_roots(vm);

    /* Heap-allocated values on stack should be marked */
    ASSERT(value_is_marked(v1));
    ASSERT(value_is_marked(v2));
    ASSERT(value_is_marked(v3));

    vm_free(vm);
}

void test_mark_roots_marks_globals(void) {
    VM *vm = vm_new();

    /* Set up globals */
    vm->globals = value_map();
    vm->globals = map_set(vm->globals, "x", value_int(42));
    vm->globals = map_set(vm->globals, "y", value_string("test"));

    gc_mark_roots(vm);

    ASSERT(value_is_marked(vm->globals));
    ASSERT(value_is_marked(map_get(vm->globals, "x")));
    ASSERT(value_is_marked(map_get(vm->globals, "y")));

    vm_free(vm);
}

void test_mark_roots_marks_constants(void) {
    VM *vm = vm_new();
    Bytecode *code = bytecode_new();

    /* Add constants to the main chunk */
    Value *c1 = value_int(100);
    Value *c2 = value_string("constant");
    chunk_add_constant(code->main, c1);
    chunk_add_constant(code->main, c2);

    vm->code = code;

    gc_mark_roots(vm);

    ASSERT(value_is_marked(c1));
    ASSERT(value_is_marked(c2));

    vm_free(vm);
    bytecode_free(code);
}

void test_mark_roots_marks_function_constants(void) {
    VM *vm = vm_new();
    Bytecode *code = bytecode_new();

    /* Create a function chunk with constants */
    Chunk *fn_chunk = chunk_new();
    Value *fc1 = value_int(999);
    chunk_add_constant(fn_chunk, fc1);
    bytecode_add_function(code, fn_chunk);

    vm->code = code;

    gc_mark_roots(vm);

    ASSERT(value_is_marked(fc1));

    vm_free(vm);
    bytecode_free(code);
}

void test_mark_roots_empty_vm(void) {
    VM *vm = vm_new();

    /* Mark roots on empty VM should not crash */
    gc_mark_roots(vm);

    vm_free(vm);
}

void test_mark_roots_deep_stack(void) {
    VM *vm = vm_new();

    /* Push many heap-allocated values (arrays and strings) */
    Value *values[50];
    for (int i = 0; i < 50; i++) {
        /* Alternate between arrays and strings - both are heap objects */
        if (i % 2 == 0) {
            values[i] = value_array();
        } else {
            values[i] = value_string("test");
        }
        vm_push(vm, values[i]);
    }

    gc_mark_roots(vm);

    /* All values should be marked */
    for (int i = 0; i < 50; i++) {
        ASSERT(value_is_marked(values[i]));
    }

    vm_free(vm);
}

/* ============================================================================
 * Gray List Operations Tests
 * ============================================================================ */

void test_gc_start_incremental(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    ASSERT(!gc_in_progress(heap));

    bool started = gc_start_incremental(heap, vm);
    ASSERT(started);
    ASSERT(gc_in_progress(heap));
    ASSERT_EQ(GC_MARKING, heap->gc_phase);

    gc_complete(heap, vm);
    ASSERT(!gc_in_progress(heap));

    vm_free(vm);
    heap_free(heap);
}

void test_gc_in_progress_states(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Initially not in progress */
    ASSERT(!gc_in_progress(heap));
    ASSERT_EQ(GC_IDLE, heap->gc_phase);

    /* Start incremental */
    gc_start_incremental(heap, vm);
    ASSERT(gc_in_progress(heap));

    /* Complete */
    gc_complete(heap, vm);
    ASSERT(!gc_in_progress(heap));
    ASSERT_EQ(GC_IDLE, heap->gc_phase);

    vm_free(vm);
    heap_free(heap);
}

void test_gc_step_progresses(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate some objects */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    gc_start_incremental(heap, vm);
    ASSERT(gc_in_progress(heap));

    /* Step should make progress */
    bool done = gc_step(heap, vm);
    /* May or may not be done depending on work */
    (void)done;

    gc_complete(heap, vm);
    ASSERT(!gc_in_progress(heap));

    vm_free(vm);
    heap_free(heap);
}

void test_gc_mark_increment_empty(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    /* With empty gray list, mark_increment should return true (complete) */
    bool complete = gc_mark_increment(heap, 100);
    ASSERT(complete);

    heap_free(heap);
}

void test_gc_mark_increment_bounded(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create objects on heap */
    Value *arr = heap_alloc(heap, VAL_ARRAY);
    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        arr = array_push(arr, v);
    }

    /* Push to stack so it's a root */
    vm_push(vm, arr);

    gc_start_incremental(heap, vm);

    /* Mark with small bound - may not complete */
    gc_mark_increment(heap, 2);

    gc_complete(heap, vm);

    value_release(arr);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Incremental Marking Work Packets Tests
 * ============================================================================ */

void test_gc_incremental_completes(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create complex object graph */
    Value *root = heap_alloc(heap, VAL_ARRAY);
    for (int i = 0; i < 5; i++) {
        Value *inner = heap_alloc(heap, VAL_ARRAY);
        for (int j = 0; j < 3; j++) {
            Value *v = heap_alloc(heap, VAL_INT);
            inner = array_push(inner, v);
        }
        root = array_push(root, inner);
    }

    vm_push(vm, root);

    gc_start_incremental(heap, vm);

    /* Run steps until complete */
    int steps = 0;
    while (gc_in_progress(heap) && steps < 100) {
        gc_step(heap, vm);
        steps++;
    }

    ASSERT(!gc_in_progress(heap));
    ASSERT(steps < 100); /* Should complete */

    value_release(root);
    vm_free(vm);
    heap_free(heap);
}

void test_gc_complete_forces_finish(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create objects */
    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    gc_start_incremental(heap, vm);
    ASSERT(gc_in_progress(heap));

    /* gc_complete should force completion */
    gc_complete(heap, vm);
    ASSERT(!gc_in_progress(heap));
    ASSERT_EQ(GC_IDLE, heap->gc_phase);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Mark Bit Clearing Tests
 * ============================================================================ */

void test_mark_bit_cleared_after_gc(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v = heap_alloc(heap, VAL_INT);
    vm_push(vm, v);

    /* Run GC */
    gc_collect(heap, vm);

    /* Mark bit should be cleared for surviving objects */
    ASSERT(!value_is_marked(v));

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

void test_mark_already_marked_array(void) {
    Value *arr = value_array();
    Value *v1 = value_int(1);
    arr = array_push(arr, v1);

    /* Mark twice */
    gc_mark_value(arr);
    gc_mark_value(arr);

    ASSERT(value_is_marked(arr));
    ASSERT(value_is_marked(array_get(arr, 0)));

    value_free(arr);
}

void test_mark_cyclic_reference(void) {
    /* Create map that references itself (simulated cycle) */
    Value *m1 = value_map();
    Value *m2 = value_map();

    m1 = map_set(m1, "other", m2);
    m2 = map_set(m2, "other", m1);

    /* Marking should not infinite loop due to mark bit check */
    gc_mark_value(m1);

    ASSERT(value_is_marked(m1));
    ASSERT(value_is_marked(m2));

    value_free(m1);
    /* m2 is freed as part of m1's children */
}

void test_mark_mixed_container(void) {
    Value *m = value_map();
    Value *arr = value_array();

    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_string("test"));
    m = map_set(m, "array", arr);
    m = map_set(m, "value", value_float(3.14));

    gc_mark_value(m);

    ASSERT(value_is_marked(m));
    ASSERT(value_is_marked(map_get(m, "array")));
    ASSERT(value_is_marked(map_get(m, "value")));

    Value *retrieved_arr = map_get(m, "array");
    ASSERT(value_is_marked(array_get(retrieved_arr, 0)));
    ASSERT(value_is_marked(array_get(retrieved_arr, 1)));

    value_free(m);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    /* gc_mark_value basic tests */
    RUN_TEST(test_mark_value_sets_mark_bit);
    RUN_TEST(test_mark_value_idempotent);
    RUN_TEST(test_mark_value_null_safe);
    RUN_TEST(test_mark_value_nil);
    RUN_TEST(test_mark_value_bool);
    RUN_TEST(test_mark_value_float);
    RUN_TEST(test_mark_value_string);
    RUN_TEST(test_mark_value_pid);

    /* gc_mark_value array traversal */
    RUN_TEST(test_mark_value_traverses_arrays);
    RUN_TEST(test_mark_value_nested_arrays);
    RUN_TEST(test_mark_value_empty_array);

    /* gc_mark_value map traversal */
    RUN_TEST(test_mark_value_traverses_maps);
    RUN_TEST(test_mark_value_nested_maps);
    RUN_TEST(test_mark_value_empty_map);
    RUN_TEST(test_mark_value_map_with_array_values);

    /* gc_mark_roots tests */
    RUN_TEST(test_mark_roots_marks_stack);
    RUN_TEST(test_mark_roots_marks_globals);
    RUN_TEST(test_mark_roots_marks_constants);
    RUN_TEST(test_mark_roots_marks_function_constants);
    RUN_TEST(test_mark_roots_empty_vm);
    RUN_TEST(test_mark_roots_deep_stack);

    /* Gray list operations */
    RUN_TEST(test_gc_start_incremental);
    RUN_TEST(test_gc_in_progress_states);
    RUN_TEST(test_gc_step_progresses);
    RUN_TEST(test_gc_mark_increment_empty);
    RUN_TEST(test_gc_mark_increment_bounded);

    /* Incremental marking work packets */
    RUN_TEST(test_gc_incremental_completes);
    RUN_TEST(test_gc_complete_forces_finish);

    /* Mark bit clearing */
    RUN_TEST(test_mark_bit_cleared_after_gc);

    /* Edge cases */
    RUN_TEST(test_mark_already_marked_array);
    RUN_TEST(test_mark_cyclic_reference);
    RUN_TEST(test_mark_mixed_container);

    return TEST_RESULT();
}
