/*
 * Agim - GC Allocation Tests
 *
 * Comprehensive tests for GC allocation operations:
 * - heap_alloc returns valid pointer
 * - heap_alloc fails at max_size
 * - heap_alloc triggers GC at threshold
 * - heap_alloc_with_gc behavior
 * - allocation of each value type
 * - allocation alignment
 * - allocation size tracking
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/gc.h"
#include "vm/vm.h"

/* ============================================================================
 * heap_alloc Basic Tests
 * ============================================================================ */

void test_heap_alloc_returns_valid_pointer(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_INT);

    ASSERT(v != NULL);
    ASSERT(v->type == VAL_INT);

    heap_free(heap);
}

void test_heap_alloc_multiple_allocations(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v1 = heap_alloc(heap, VAL_INT);
    Value *v2 = heap_alloc(heap, VAL_INT);
    Value *v3 = heap_alloc(heap, VAL_INT);

    ASSERT(v1 != NULL);
    ASSERT(v2 != NULL);
    ASSERT(v3 != NULL);

    /* All pointers should be different */
    ASSERT(v1 != v2);
    ASSERT(v2 != v3);
    ASSERT(v1 != v3);

    heap_free(heap);
}

void test_heap_alloc_links_to_object_list(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    ASSERT(heap->objects == NULL);

    Value *v1 = heap_alloc(heap, VAL_INT);
    ASSERT(heap->objects == v1);
    ASSERT(v1->next == NULL);

    Value *v2 = heap_alloc(heap, VAL_INT);
    ASSERT(heap->objects == v2);
    ASSERT(v2->next == v1);

    Value *v3 = heap_alloc(heap, VAL_INT);
    ASSERT(heap->objects == v3);
    ASSERT(v3->next == v2);
    ASSERT(v2->next == v1);

    heap_free(heap);
}

/* ============================================================================
 * heap_alloc Max Size Tests
 * ============================================================================ */

void test_heap_alloc_fails_at_max_size(void) {
    GCConfig config = gc_config_default();
    config.max_heap_size = 256; /* Very small heap */
    config.initial_heap_size = 128;
    Heap *heap = heap_new(&config);

    /* Allocate until we hit the limit */
    int count = 0;
    Value *v = NULL;
    for (int i = 0; i < 100; i++) {
        v = heap_alloc(heap, VAL_INT);
        if (!v) break;
        count++;
    }

    /* Should have failed before 100 allocations */
    ASSERT(count < 100);
    ASSERT(v == NULL);
    ASSERT(heap->bytes_allocated <= heap->max_size);

    heap_free(heap);
}

void test_heap_alloc_respects_max_size_exact(void) {
    GCConfig config = gc_config_default();
    config.max_heap_size = sizeof(Value) * 2; /* Exactly 2 values */
    config.initial_heap_size = sizeof(Value);
    Heap *heap = heap_new(&config);

    Value *v1 = heap_alloc(heap, VAL_INT);
    ASSERT(v1 != NULL);

    Value *v2 = heap_alloc(heap, VAL_INT);
    ASSERT(v2 != NULL);

    /* Third allocation should fail */
    Value *v3 = heap_alloc(heap, VAL_INT);
    ASSERT(v3 == NULL);

    heap_free(heap);
}

/* ============================================================================
 * heap_alloc GC Threshold Tests
 * ============================================================================ */

void test_heap_alloc_grows_threshold(void) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 64;
    config.max_heap_size = 4096;
    Heap *heap = heap_new(&config);

    size_t initial_threshold = heap->next_gc;
    ASSERT_EQ(64, initial_threshold);

    /* Allocate until we exceed the threshold - threshold grows when
     * bytes_allocated + size > next_gc */
    int allocations = 0;
    while (allocations < 20) {
        Value *v = heap_alloc(heap, VAL_INT);
        if (!v) break;
        allocations++;
        /* Check if threshold grew after exceeding it */
        if (heap->next_gc > initial_threshold) break;
    }

    /* Threshold should have grown once we exceeded initial_threshold */
    ASSERT(heap->next_gc > initial_threshold);

    heap_free(heap);
}

void test_heap_alloc_threshold_caps_at_max(void) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 64;
    config.max_heap_size = 128;
    Heap *heap = heap_new(&config);

    /* Allocate past the initial threshold */
    for (int i = 0; i < 10; i++) {
        heap_alloc(heap, VAL_INT);
    }

    /* Threshold should not exceed max_size */
    ASSERT(heap->next_gc <= heap->max_size);

    heap_free(heap);
}

/* ============================================================================
 * heap_alloc_with_gc Tests
 * ============================================================================ */

void test_heap_alloc_with_gc_returns_valid_pointer(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v = heap_alloc_with_gc(heap, VAL_INT, vm);

    ASSERT(v != NULL);
    ASSERT(v->type == VAL_INT);

    vm_free(vm);
    heap_free(heap);
}

void test_heap_alloc_with_gc_triggers_collection(void) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 128;
    config.max_heap_size = 256;
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate unreachable objects */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc_with_gc(heap, VAL_INT, vm);
        if (v) value_release(v); /* Mark as unreferenced */
    }

    size_t before = heap->gc_count;

    /* Force allocation past threshold - should trigger GC */
    while (heap->bytes_allocated < heap->next_gc + sizeof(Value)) {
        Value *v = heap_alloc_with_gc(heap, VAL_INT, vm);
        if (v) value_release(v);
    }

    /* GC should have run at least once */
    ASSERT(heap->gc_count > before || heap->minor_gc_count > 0 || heap->major_gc_count > 0);

    vm_free(vm);
    heap_free(heap);
}

void test_heap_alloc_with_gc_young_generation(void) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 256;
    config.max_heap_size = 1024;
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->young_gc_threshold = 64; /* Very low to trigger minor GC */

    size_t minor_before = heap->minor_gc_count;

    /* Allocate past young threshold */
    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc_with_gc(heap, VAL_INT, vm);
        if (v) value_release(v);
    }

    /* Should have triggered at least one minor GC */
    ASSERT(heap->minor_gc_count > minor_before);

    vm_free(vm);
    heap_free(heap);
}

void test_heap_alloc_with_gc_full_collection(void) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 128;
    config.max_heap_size = 256;
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->needs_full_gc = true;

    size_t major_before = heap->major_gc_count;

    Value *v = heap_alloc_with_gc(heap, VAL_INT, vm);
    ASSERT(v != NULL);

    /* Should have triggered full GC */
    ASSERT(heap->major_gc_count > major_before);
    ASSERT(!heap->needs_full_gc);

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Allocation of Each Value Type
 * ============================================================================ */

void test_alloc_val_nil(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_NIL);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_NIL);

    heap_free(heap);
}

void test_alloc_val_bool(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_BOOL);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_BOOL);
    ASSERT(v->as.boolean == false); /* Default value */

    heap_free(heap);
}

void test_alloc_val_int(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_INT);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_INT);
    ASSERT(v->as.integer == 0); /* Default value */

    heap_free(heap);
}

void test_alloc_val_float(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_FLOAT);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_FLOAT);
    ASSERT(v->as.floating == 0.0); /* Default value */

    heap_free(heap);
}

void test_alloc_val_string(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_STRING);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_STRING);
    ASSERT(v->as.string != NULL);
    ASSERT_EQ(0, v->as.string->length);

    heap_free(heap);
}

void test_alloc_val_array(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_ARRAY);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_ARRAY);
    ASSERT(v->as.array != NULL);
    ASSERT_EQ(0, v->as.array->length);

    heap_free(heap);
}

void test_alloc_val_map(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_MAP);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_MAP);
    ASSERT(v->as.map != NULL);
    ASSERT_EQ(0, v->as.map->size);

    heap_free(heap);
}

void test_alloc_val_pid(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_PID);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_PID);
    ASSERT(v->as.pid == 0); /* Default value */

    heap_free(heap);
}

void test_alloc_val_function(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_FUNCTION);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_FUNCTION);
    ASSERT(v->as.function != NULL);
    ASSERT(v->as.function->arity == 0);

    heap_free(heap);
}

void test_alloc_val_bytes(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_BYTES);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_BYTES);
    ASSERT(v->as.bytes != NULL);
    ASSERT(v->as.bytes->capacity >= 64);

    heap_free(heap);
}

void test_alloc_val_vector(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_VECTOR);
    ASSERT(v != NULL);
    ASSERT(v->type == VAL_VECTOR);
    ASSERT(v->as.vector != NULL);

    heap_free(heap);
}

void test_alloc_unsupported_types_return_null(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    /* These types are not supported for direct heap allocation */
    Value *v1 = heap_alloc(heap, VAL_CLOSURE);
    Value *v2 = heap_alloc(heap, VAL_RESULT);
    Value *v3 = heap_alloc(heap, VAL_OPTION);
    Value *v4 = heap_alloc(heap, VAL_STRUCT);
    Value *v5 = heap_alloc(heap, VAL_ENUM);

    ASSERT(v1 == NULL);
    ASSERT(v2 == NULL);
    ASSERT(v3 == NULL);
    ASSERT(v4 == NULL);
    ASSERT(v5 == NULL);

    heap_free(heap);
}

/* ============================================================================
 * Allocation Alignment Tests
 * ============================================================================ */

void test_alloc_pointer_alignment(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        ASSERT(v != NULL);

        /* Pointer should be aligned to at least sizeof(void*) */
        uintptr_t addr = (uintptr_t)v;
        ASSERT((addr % sizeof(void*)) == 0);
    }

    heap_free(heap);
}

void test_alloc_mixed_types_alignment(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v1 = heap_alloc(heap, VAL_INT);
    Value *v2 = heap_alloc(heap, VAL_STRING);
    Value *v3 = heap_alloc(heap, VAL_ARRAY);
    Value *v4 = heap_alloc(heap, VAL_MAP);
    Value *v5 = heap_alloc(heap, VAL_FLOAT);

    /* All pointers should be properly aligned */
    ASSERT(((uintptr_t)v1 % sizeof(void*)) == 0);
    ASSERT(((uintptr_t)v2 % sizeof(void*)) == 0);
    ASSERT(((uintptr_t)v3 % sizeof(void*)) == 0);
    ASSERT(((uintptr_t)v4 % sizeof(void*)) == 0);
    ASSERT(((uintptr_t)v5 % sizeof(void*)) == 0);

    heap_free(heap);
}

/* ============================================================================
 * Allocation Size Tracking Tests
 * ============================================================================ */

void test_alloc_size_tracking_basic(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    ASSERT_EQ(0, heap->bytes_allocated);
    ASSERT_EQ(0, heap->total_allocated);

    Value *v = heap_alloc(heap, VAL_INT);
    ASSERT(v != NULL);
    ASSERT(heap->bytes_allocated > 0);
    ASSERT(heap->total_allocated > 0);
    ASSERT_EQ(heap->bytes_allocated, heap->total_allocated);

    heap_free(heap);
}

void test_alloc_size_tracking_accumulates(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    size_t prev_allocated = 0;
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        ASSERT(v != NULL);
        ASSERT(heap->bytes_allocated > prev_allocated);
        prev_allocated = heap->bytes_allocated;
    }

    heap_free(heap);
}

void test_alloc_size_tracking_per_type(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    size_t base = heap->bytes_allocated;

    Value *v_int = heap_alloc(heap, VAL_INT);
    size_t int_size = heap->bytes_allocated - base;

    base = heap->bytes_allocated;
    Value *v_str = heap_alloc(heap, VAL_STRING);
    size_t str_size = heap->bytes_allocated - base;

    base = heap->bytes_allocated;
    Value *v_arr = heap_alloc(heap, VAL_ARRAY);
    size_t arr_size = heap->bytes_allocated - base;

    ASSERT(v_int != NULL);
    ASSERT(v_str != NULL);
    ASSERT(v_arr != NULL);

    /* All types should have some size */
    ASSERT(int_size > 0);
    ASSERT(str_size > 0);
    ASSERT(arr_size > 0);

    /* Complex types should be at least as large as simple types */
    ASSERT(str_size >= int_size);
    ASSERT(arr_size >= int_size);

    heap_free(heap);
}

void test_alloc_generational_tracking(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    gc_set_generational(heap, true);

    ASSERT_EQ(0, heap->young_count);
    ASSERT_EQ(0, heap->young_bytes);

    Value *v = heap_alloc(heap, VAL_INT);
    ASSERT(v != NULL);

    ASSERT_EQ(1, heap->young_count);
    ASSERT(heap->young_bytes > 0);

    Value *v2 = heap_alloc(heap, VAL_STRING);
    ASSERT(v2 != NULL);

    ASSERT_EQ(2, heap->young_count);

    heap_free(heap);
}

void test_alloc_stats_accurate(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    for (int i = 0; i < 10; i++) {
        heap_alloc(heap, VAL_INT);
    }

    HeapStats stats = heap_stats(heap);

    ASSERT_EQ(10, stats.objects_allocated);
    ASSERT(stats.bytes_allocated > 0);
    ASSERT_EQ(heap->bytes_allocated, stats.bytes_allocated);

    heap_free(heap);
}

/* ============================================================================
 * GC State Initialization Tests
 * ============================================================================ */

void test_alloc_gc_state_initialized(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v = heap_alloc(heap, VAL_INT);
    ASSERT(v != NULL);

    /* gc_state should be 0 (unmarked, not remembered, young gen) */
    ASSERT_EQ(0, v->gc_state);
    ASSERT(!value_is_marked(v));
    ASSERT(!value_is_remembered(v));
    ASSERT(!value_is_old_gen(v));

    heap_free(heap);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

void test_alloc_zero_max_size(void) {
    GCConfig config = gc_config_default();
    config.max_heap_size = 0;
    config.initial_heap_size = 0;
    Heap *heap = heap_new(&config);

    /* Should fail immediately - can't allocate with zero max size */
    Value *v = heap_alloc(heap, VAL_INT);
    ASSERT(v == NULL);

    heap_free(heap);
}

void test_alloc_after_heap_exhaustion(void) {
    GCConfig config = gc_config_default();
    config.max_heap_size = sizeof(Value);
    config.initial_heap_size = sizeof(Value);
    Heap *heap = heap_new(&config);

    Value *v1 = heap_alloc(heap, VAL_INT);
    ASSERT(v1 != NULL);

    /* Second allocation should fail */
    Value *v2 = heap_alloc(heap, VAL_INT);
    ASSERT(v2 == NULL);

    /* Further allocations should continue to fail */
    Value *v3 = heap_alloc(heap, VAL_INT);
    ASSERT(v3 == NULL);

    heap_free(heap);
}

void test_heap_new_with_null_config(void) {
    /* heap_new should work with NULL config using defaults */
    Heap *heap = heap_new(NULL);

    ASSERT(heap != NULL);
    ASSERT(heap->max_size > 0);
    ASSERT(heap->next_gc > 0);

    Value *v = heap_alloc(heap, VAL_INT);
    ASSERT(v != NULL);

    heap_free(heap);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    /* heap_alloc basic tests */
    RUN_TEST(test_heap_alloc_returns_valid_pointer);
    RUN_TEST(test_heap_alloc_multiple_allocations);
    RUN_TEST(test_heap_alloc_links_to_object_list);

    /* heap_alloc max size tests */
    RUN_TEST(test_heap_alloc_fails_at_max_size);
    RUN_TEST(test_heap_alloc_respects_max_size_exact);

    /* heap_alloc GC threshold tests */
    RUN_TEST(test_heap_alloc_grows_threshold);
    RUN_TEST(test_heap_alloc_threshold_caps_at_max);

    /* heap_alloc_with_gc tests */
    RUN_TEST(test_heap_alloc_with_gc_returns_valid_pointer);
    RUN_TEST(test_heap_alloc_with_gc_triggers_collection);
    RUN_TEST(test_heap_alloc_with_gc_young_generation);
    RUN_TEST(test_heap_alloc_with_gc_full_collection);

    /* Allocation of each value type */
    RUN_TEST(test_alloc_val_nil);
    RUN_TEST(test_alloc_val_bool);
    RUN_TEST(test_alloc_val_int);
    RUN_TEST(test_alloc_val_float);
    RUN_TEST(test_alloc_val_string);
    RUN_TEST(test_alloc_val_array);
    RUN_TEST(test_alloc_val_map);
    RUN_TEST(test_alloc_val_pid);
    RUN_TEST(test_alloc_val_function);
    RUN_TEST(test_alloc_val_bytes);
    RUN_TEST(test_alloc_val_vector);
    RUN_TEST(test_alloc_unsupported_types_return_null);

    /* Alignment tests */
    RUN_TEST(test_alloc_pointer_alignment);
    RUN_TEST(test_alloc_mixed_types_alignment);

    /* Size tracking tests */
    RUN_TEST(test_alloc_size_tracking_basic);
    RUN_TEST(test_alloc_size_tracking_accumulates);
    RUN_TEST(test_alloc_size_tracking_per_type);
    RUN_TEST(test_alloc_generational_tracking);
    RUN_TEST(test_alloc_stats_accurate);

    /* GC state initialization */
    RUN_TEST(test_alloc_gc_state_initialized);

    /* Edge cases */
    RUN_TEST(test_alloc_zero_max_size);
    RUN_TEST(test_alloc_after_heap_exhaustion);
    RUN_TEST(test_heap_new_with_null_config);

    return TEST_RESULT();
}
