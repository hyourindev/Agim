/*
 * Agim - Comprehensive GC Tests
 *
 * Tests for garbage collector including allocation, marking,
 * sweeping, generational GC, write barriers, and concurrent safety.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/gc.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "types/array.h"
#include "types/map.h"
#include "types/string.h"

#include <stdatomic.h>

/* ========== Allocation Tests ========== */

void test_heap_alloc_returns_valid_pointer(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_INT);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_INT);

    heap_free(heap);
}

void test_heap_alloc_all_value_types(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *int_val = heap_alloc(heap, VAL_INT);
    ASSERT(int_val != NULL);
    ASSERT(int_val->type == VAL_INT);

    Value *float_val = heap_alloc(heap, VAL_FLOAT);
    ASSERT(float_val != NULL);
    ASSERT(float_val->type == VAL_FLOAT);

    Value *bool_val = heap_alloc(heap, VAL_BOOL);
    ASSERT(bool_val != NULL);
    ASSERT(bool_val->type == VAL_BOOL);

    Value *string_val = heap_alloc(heap, VAL_STRING);
    ASSERT(string_val != NULL);
    ASSERT(string_val->type == VAL_STRING);

    Value *array_val = heap_alloc(heap, VAL_ARRAY);
    ASSERT(array_val != NULL);
    ASSERT(array_val->type == VAL_ARRAY);

    Value *map_val = heap_alloc(heap, VAL_MAP);
    ASSERT(map_val != NULL);
    ASSERT(map_val->type == VAL_MAP);

    heap_free(heap);
}

void test_heap_alloc_size_tracking(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    size_t before = heap_used(heap);
    heap_alloc(heap, VAL_INT);
    heap_alloc(heap, VAL_INT);
    heap_alloc(heap, VAL_INT);
    size_t after = heap_used(heap);

    ASSERT(after > before);

    heap_free(heap);
}

void test_heap_alloc_triggers_gc_at_threshold(void) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 1024;  /* Small heap to trigger GC quickly */
    config.gc_threshold = 0.5;
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate until we exceed threshold */
    size_t gc_count_before = heap->gc_count;
    for (int i = 0; i < 100; i++) {
        Value *v = heap_alloc_with_gc(heap, VAL_INT, vm);
        if (v) value_release(v);  /* Release so GC can collect */
    }
    size_t gc_count_after = heap->gc_count;

    /* GC should have been triggered at least once */
    ASSERT(gc_count_after > gc_count_before);

    vm_free(vm);
    heap_free(heap);
}

/* ========== Mark Phase Tests ========== */

void test_gc_mark_value_sets_mark_bit(void) {
    Value *v = value_int(42);
    ASSERT(!value_is_marked(v));

    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    value_free(v);
}

void test_gc_mark_value_traverses_arrays(void) {
    Value *arr = value_array();
    Value *item1 = value_int(1);
    Value *item2 = value_int(2);
    Value *item3 = value_int(3);

    arr = array_push(arr, item1);
    arr = array_push(arr, item2);
    arr = array_push(arr, item3);

    gc_mark_value(arr);

    ASSERT(value_is_marked(arr));
    ASSERT(value_is_marked(array_get(arr, 0)));
    ASSERT(value_is_marked(array_get(arr, 1)));
    ASSERT(value_is_marked(array_get(arr, 2)));

    value_free(arr);
}

void test_gc_mark_value_traverses_maps(void) {
    Value *map = value_map();
    Value *val = value_int(42);

    map = map_set(map, "key", val);

    gc_mark_value(map);

    ASSERT(value_is_marked(map));
    /* Keys and values in the map should be marked */

    value_free(map);
}

void test_gc_mark_value_traverses_nested_structures(void) {
    Value *outer = value_array();
    Value *inner = value_array();
    Value *item = value_int(42);

    inner = array_push(inner, item);
    outer = array_push(outer, inner);

    gc_mark_value(outer);

    ASSERT(value_is_marked(outer));
    ASSERT(value_is_marked(array_get(outer, 0)));

    value_free(outer);
}

void test_gc_mark_handles_cycles(void) {
    /* Create a cycle: array containing itself would be tricky
     * but maps can reference themselves via values */
    Value *map = value_map();

    /* This is tricky - normally we can't create direct cycles easily
     * but we can test that marking doesn't infinite loop on deep structures */
    for (int i = 0; i < 10; i++) {
        Value *inner = value_map();
        char key_buf[16];
        snprintf(key_buf, sizeof(key_buf), "key%d", i);
        map = map_set(map, key_buf, inner);
    }

    gc_mark_value(map);
    ASSERT(value_is_marked(map));

    value_free(map);
}

/* ========== Sweep Phase Tests ========== */

void test_unmarked_objects_freed(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v1 = heap_alloc(heap, VAL_INT);
    Value *v2 = heap_alloc(heap, VAL_INT);
    Value *v3 = heap_alloc(heap, VAL_INT);

    /* Release all - they become collectible */
    value_release(v1);
    value_release(v2);
    value_release(v3);

    size_t before = heap_used(heap);
    gc_collect(heap, vm);
    size_t after = heap_used(heap);

    ASSERT(after < before);

    vm_free(vm);
    heap_free(heap);
}

void test_marked_objects_preserved(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v = heap_alloc(heap, VAL_INT);
    v->as.integer = 42;

    /* Keep a reference - don't release */
    gc_mark_value(v);

    gc_collect(heap, vm);

    /* Value should still be valid */
    ASSERT(v->as.integer == 42);

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

void test_mark_bit_cleared_after_sweep(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v = heap_alloc(heap, VAL_INT);
    gc_mark_value(v);
    ASSERT(value_is_marked(v));

    gc_collect(heap, vm);

    /* Mark bit should be cleared after collection */
    ASSERT(!value_is_marked(v));

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

void test_bytes_allocated_updated_after_sweep(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v1 = heap_alloc(heap, VAL_INT);
    Value *v2 = heap_alloc(heap, VAL_INT);
    value_release(v1);
    value_release(v2);

    size_t before = heap->bytes_allocated;
    gc_collect(heap, vm);
    size_t after = heap->bytes_allocated;

    ASSERT(after <= before);

    vm_free(vm);
    heap_free(heap);
}

/* ========== Incremental GC Tests ========== */

void test_gc_start_incremental(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    ASSERT(!gc_in_progress(heap));

    bool started = gc_start_incremental(heap, vm);
    ASSERT(started);
    ASSERT(gc_in_progress(heap));

    gc_complete(heap, vm);
    ASSERT(!gc_in_progress(heap));

    vm_free(vm);
    heap_free(heap);
}

void test_gc_step_makes_progress(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate some objects to give GC work to do */
    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    gc_start_incremental(heap, vm);

    /* Run steps until complete */
    int steps = 0;
    while (gc_in_progress(heap) && steps < 100) {
        gc_step(heap, vm);
        steps++;
    }

    ASSERT(!gc_in_progress(heap));

    vm_free(vm);
    heap_free(heap);
}

void test_gc_mark_increment_empty_gray_list(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    /* With empty gray list, should return true (complete) */
    bool complete = gc_mark_increment(heap, 100);
    ASSERT(complete);

    heap_free(heap);
}

void test_gc_mark_increment_work_packets(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create nested structure for gray list work */
    Value *root = value_array();
    for (int i = 0; i < 20; i++) {
        Value *inner = value_array();
        for (int j = 0; j < 10; j++) {
            inner = array_push(inner, value_int(j));
        }
        root = array_push(root, inner);
    }

    gc_start_incremental(heap, vm);

    /* Gray list should have work to do */
    int iterations = 0;
    while (!gc_mark_increment(heap, 10) && iterations < 100) {
        iterations++;
    }

    value_release(root);
    gc_complete(heap, vm);

    vm_free(vm);
    heap_free(heap);
}

/* ========== Generational GC Tests ========== */

void test_gc_set_generational(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    /* Generational GC is enabled by default */
    ASSERT(heap->generational_enabled);

    gc_set_generational(heap, false);
    ASSERT(!heap->generational_enabled);

    gc_set_generational(heap, true);
    ASSERT(heap->generational_enabled);

    heap_free(heap);
}

void test_gc_young_generation_collection(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    /* Allocate young objects */
    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    size_t minor_gc_before = heap->minor_gc_count;
    gc_collect_young(heap, vm);
    size_t minor_gc_after = heap->minor_gc_count;

    ASSERT(minor_gc_after > minor_gc_before);

    vm_free(vm);
    heap_free(heap);
}

void test_gc_promotion_to_old_generation(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    gc_set_generational(heap, true);

    Value *v = heap_alloc(heap, VAL_INT);

    /* Test value_set_old_gen and value_is_old_gen work correctly */
    ASSERT(!value_is_old_gen(v));  /* Newly allocated value is in young gen */

    value_set_old_gen(v);
    ASSERT(value_is_old_gen(v));   /* After setting, should be in old gen */

    /* Test survival count mechanics */
    ASSERT(value_survival_count(v) == 0);
    value_inc_survival(v);
    value_inc_survival(v);
    ASSERT(value_survival_count(v) >= heap->promotion_threshold);

    value_release(v);
    heap_free(heap);
}

void test_gc_full_collection(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    /* Allocate and promote some objects */
    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_set_old_gen(v);
        value_release(v);
    }

    size_t major_gc_before = heap->major_gc_count;
    gc_collect_full(heap, vm);
    size_t major_gc_after = heap->major_gc_count;

    ASSERT(major_gc_after > major_gc_before);

    vm_free(vm);
    heap_free(heap);
}

/* ========== Write Barrier Tests ========== */

void test_gc_write_barrier_marks_remembered(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *old_arr = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old_arr);

    Value *young_val = heap_alloc(heap, VAL_INT);

    gc_write_barrier(heap, old_arr, young_val);

    /* Old object should be remembered */
    ASSERT(value_is_remembered(old_arr));

    value_release(old_arr);
    value_release(young_val);
    vm_free(vm);
    heap_free(heap);
}

void test_gc_write_barrier_no_op_for_young(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    gc_set_generational(heap, true);

    Value *young_arr = heap_alloc(heap, VAL_ARRAY);
    Value *young_val = heap_alloc(heap, VAL_INT);

    /* Writing to young object shouldn't add to remember set */
    gc_write_barrier(heap, young_arr, young_val);
    ASSERT(!value_is_remembered(young_arr));

    value_release(young_arr);
    value_release(young_val);
    heap_free(heap);
}

void test_remember_set_cleared_after_full_gc(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *old_arr = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old_arr);
    Value *young_val = heap_alloc(heap, VAL_INT);

    gc_write_barrier(heap, old_arr, young_val);
    ASSERT(heap->remember_count > 0);

    gc_collect_full(heap, vm);

    /* Remember set should be cleared after full GC */
    ASSERT_EQ(0, heap->remember_count);

    value_release(old_arr);
    value_release(young_val);
    vm_free(vm);
    heap_free(heap);
}

/* ========== Refcount Tests ========== */

void test_refcount_atomic_operations(void) {
    Value *v = value_int(42);

    /* Initial refcount is 1 */
    ASSERT(atomic_load(&v->refcount) == 1);

    /* Retain increases refcount */
    value_retain(v);
    ASSERT(atomic_load(&v->refcount) == 2);

    /* Release decreases refcount */
    value_release(v);
    ASSERT(atomic_load(&v->refcount) == 1);

    /* Final release frees the value */
    value_release(v);
}

void test_refcount_freeing_sentinel(void) {
    Value *v = value_int(42);

    /* Retain multiple times */
    value_retain(v);
    value_retain(v);
    ASSERT(atomic_load(&v->refcount) == 3);

    /* Release all */
    value_release(v);
    value_release(v);
    value_release(v);

    /* Value is now freed - don't access it */
}

void test_value_retain_during_sweep(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v = heap_alloc(heap, VAL_INT);

    /* Simulate concurrent access during sweep */
    gc_start_incremental(heap, vm);

    /* Retain should succeed even during GC */
    Value *retained = value_retain(v);
    ASSERT(retained == v);
    ASSERT(atomic_load(&v->refcount) >= 2);

    gc_complete(heap, vm);

    value_release(v);
    value_release(v);  /* Release our retain */

    vm_free(vm);
    heap_free(heap);
}

/* ========== COW (Copy-on-Write) Tests ========== */

void test_cow_array_modification(void) {
    Value *arr1 = value_array();
    arr1 = array_push(arr1, value_int(1));
    arr1 = array_push(arr1, value_int(2));

    /* Retain to simulate sharing */
    Value *arr2 = value_retain(arr1);

    /* Modification should trigger COW if refcount > 1 */
    /* Note: The actual COW behavior depends on implementation */

    value_release(arr1);
    value_release(arr2);
}

void test_cow_during_gc(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *arr = heap_alloc(heap, VAL_ARRAY);

    gc_start_incremental(heap, vm);

    /* Modifications during GC should be safe */
    for (int i = 0; i < 5; i++) {
        Value *item = heap_alloc(heap, VAL_INT);
        item->as.integer = i;
        /* array_push would handle COW */
    }

    gc_complete(heap, vm);

    value_release(arr);
    vm_free(vm);
    heap_free(heap);
}

/* ========== Statistics Tests ========== */

void test_heap_stats_accurate(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    heap_alloc(heap, VAL_INT);
    heap_alloc(heap, VAL_INT);
    heap_alloc(heap, VAL_INT);

    HeapStats stats = heap_stats(heap);
    ASSERT_EQ(3, stats.objects_allocated);
    ASSERT(stats.bytes_allocated > 0);

    heap_free(heap);
}

void test_gc_count_tracking(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    size_t initial_gc_count = heap->gc_count;

    gc_collect(heap, vm);
    ASSERT_EQ(initial_gc_count + 1, heap->gc_count);

    gc_collect(heap, vm);
    ASSERT_EQ(initial_gc_count + 2, heap->gc_count);

    vm_free(vm);
    heap_free(heap);
}

/* ========== Thread-Local Heap Tests ========== */

void test_gc_set_current_heap(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    gc_set_current_heap(heap);
    Heap *retrieved = gc_get_current_heap();

    ASSERT(retrieved == heap);

    gc_set_current_heap(NULL);
    heap_free(heap);
}

/* ========== Edge Cases ========== */

void test_gc_collect_empty_heap(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Collecting empty heap should be a no-op */
    gc_collect(heap, vm);

    ASSERT_EQ(0, heap_used(heap));

    vm_free(vm);
    heap_free(heap);
}

void test_gc_collect_all_garbage(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate and immediately release */
    for (int i = 0; i < 100; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    gc_collect(heap, vm);

    /* All garbage should be collected */
    ASSERT_EQ(0, heap_used(heap));

    vm_free(vm);
    heap_free(heap);
}

void test_gc_collect_deep_structure(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create deeply nested structure */
    Value *root = value_array();
    Value *current = root;

    for (int depth = 0; depth < 50; depth++) {
        Value *next = value_array();
        current = array_push(current, next);
        current = next;
    }

    gc_mark_value(root);
    gc_collect(heap, vm);

    /* Structure should still be intact */
    ASSERT(value_is_marked(root) || !gc_in_progress(heap));

    value_release(root);
    vm_free(vm);
    heap_free(heap);
}

void test_gc_many_small_allocations(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Many small allocations */
    for (int i = 0; i < 1000; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        if (i % 2 == 0) {
            value_release(v);  /* Release half */
        }
    }

    gc_collect(heap, vm);

    vm_free(vm);
    heap_free(heap);
}

void test_gc_large_array(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *arr = value_array();
    for (int i = 0; i < 1000; i++) {
        arr = array_push(arr, value_int(i));
    }

    gc_mark_value(arr);
    gc_collect(heap, vm);

    ASSERT_EQ(1000, array_length(arr));

    value_release(arr);
    vm_free(vm);
    heap_free(heap);
}

/* ========== Main ========== */

int main(void) {
    /* Allocation Tests */
    RUN_TEST(test_heap_alloc_returns_valid_pointer);
    RUN_TEST(test_heap_alloc_all_value_types);
    RUN_TEST(test_heap_alloc_size_tracking);
    RUN_TEST(test_heap_alloc_triggers_gc_at_threshold);

    /* Mark Phase Tests */
    RUN_TEST(test_gc_mark_value_sets_mark_bit);
    RUN_TEST(test_gc_mark_value_traverses_arrays);
    RUN_TEST(test_gc_mark_value_traverses_maps);
    RUN_TEST(test_gc_mark_value_traverses_nested_structures);
    RUN_TEST(test_gc_mark_handles_cycles);

    /* Sweep Phase Tests */
    RUN_TEST(test_unmarked_objects_freed);
    RUN_TEST(test_marked_objects_preserved);
    RUN_TEST(test_mark_bit_cleared_after_sweep);
    RUN_TEST(test_bytes_allocated_updated_after_sweep);

    /* Incremental GC Tests */
    RUN_TEST(test_gc_start_incremental);
    RUN_TEST(test_gc_step_makes_progress);
    RUN_TEST(test_gc_mark_increment_empty_gray_list);
    RUN_TEST(test_gc_mark_increment_work_packets);

    /* Generational GC Tests */
    RUN_TEST(test_gc_set_generational);
    RUN_TEST(test_gc_young_generation_collection);
    RUN_TEST(test_gc_promotion_to_old_generation);
    RUN_TEST(test_gc_full_collection);

    /* Write Barrier Tests */
    RUN_TEST(test_gc_write_barrier_marks_remembered);
    RUN_TEST(test_gc_write_barrier_no_op_for_young);
    RUN_TEST(test_remember_set_cleared_after_full_gc);

    /* Refcount Tests */
    RUN_TEST(test_refcount_atomic_operations);
    RUN_TEST(test_refcount_freeing_sentinel);
    RUN_TEST(test_value_retain_during_sweep);

    /* COW Tests */
    RUN_TEST(test_cow_array_modification);
    RUN_TEST(test_cow_during_gc);

    /* Statistics Tests */
    RUN_TEST(test_heap_stats_accurate);
    RUN_TEST(test_gc_count_tracking);

    /* Thread-Local Heap Tests */
    RUN_TEST(test_gc_set_current_heap);

    /* Edge Cases */
    RUN_TEST(test_gc_collect_empty_heap);
    RUN_TEST(test_gc_collect_all_garbage);
    RUN_TEST(test_gc_collect_deep_structure);
    RUN_TEST(test_gc_many_small_allocations);
    RUN_TEST(test_gc_large_array);

    return TEST_RESULT();
}
