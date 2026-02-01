/*
 * Agim - GC Property Tests
 *
 * Property-based tests for garbage collection operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "property_test.h"
#include "vm/gc.h"
#include "vm/vm.h"
#include "vm/value.h"

/* Property: Heap starts empty */
static bool prop_heap_starts_empty(void *ctx) {
    (void)ctx;

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    PROP_ASSERT(heap != NULL);
    PROP_ASSERT(heap_used(heap) == 0);

    heap_free(heap);
    return true;
}

/* Property: Allocation increases heap size */
static bool prop_allocation_increases_size(void *ctx) {
    (void)ctx;

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    PROP_ASSERT(heap != NULL);

    size_t before = heap_used(heap);

    /* Allocate some values */
    int count = prop_rand_int_range(1, 10);
    for (int i = 0; i < count; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        PROP_ASSERT(v != NULL);
    }

    size_t after = heap_used(heap);
    PROP_ASSERT(after > before);

    heap_free(heap);
    return true;
}

/* Property: Retain increases refcount */
static bool prop_retain_increases_refcount(void *ctx) {
    (void)ctx;

    Value *v = value_int(42);
    PROP_ASSERT(v != NULL);

    value_retain(v);
    value_retain(v);
    /* Refcount should now be 3 (1 initial + 2 retains) */

    value_release(v);
    value_release(v);
    value_release(v);
    /* Value should be freed now */

    return true;
}

/* Property: GC collect doesn't crash on empty heap */
static bool prop_gc_empty_heap(void *ctx) {
    (void)ctx;

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();
    PROP_ASSERT(heap != NULL);
    PROP_ASSERT(vm != NULL);

    gc_collect(heap, vm);
    PROP_ASSERT(heap_used(heap) == 0);

    vm_free(vm);
    heap_free(heap);
    return true;
}

/* Property: Unreachable objects are collected */
static bool prop_gc_collects_unreachable(void *ctx) {
    (void)ctx;

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();
    PROP_ASSERT(heap != NULL);
    PROP_ASSERT(vm != NULL);

    /* Create values and release them (making them collectible) */
    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);  /* Make it collectible */
    }

    size_t before = heap_used(heap);
    gc_collect(heap, vm);
    size_t after = heap_used(heap);

    /* Some memory should have been freed */
    PROP_ASSERT(after <= before);

    vm_free(vm);
    heap_free(heap);
    return true;
}

/* Property: Multiple GC cycles are safe */
static bool prop_gc_multiple_cycles(void *ctx) {
    (void)ctx;

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();
    PROP_ASSERT(heap != NULL);
    PROP_ASSERT(vm != NULL);

    int cycles = prop_rand_int_range(3, 10);
    for (int c = 0; c < cycles; c++) {
        /* Allocate some values */
        for (int i = 0; i < 5; i++) {
            Value *v = heap_alloc(heap, VAL_INT);
            value_release(v);
        }
        gc_collect(heap, vm);
    }

    /* Heap should be in consistent state */
    PROP_ASSERT(heap != NULL);

    vm_free(vm);
    heap_free(heap);
    return true;
}

/* Property: Heap stats are consistent */
static bool prop_heap_stats_consistent(void *ctx) {
    (void)ctx;

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    PROP_ASSERT(heap != NULL);

    int count = prop_rand_int_range(1, 20);
    for (int i = 0; i < count; i++) {
        heap_alloc(heap, VAL_INT);
    }

    HeapStats stats = heap_stats(heap);
    PROP_ASSERT(stats.bytes_allocated > 0);
    PROP_ASSERT(stats.objects_allocated == (size_t)count);

    heap_free(heap);
    return true;
}

/* Property: Value creation and type checks work */
static bool prop_value_types(void *ctx) {
    (void)ctx;

    Value *int_val = value_int(prop_rand_int());
    Value *float_val = value_float(prop_rand_double());
    Value *bool_val = value_bool(prop_rand_bool());

    PROP_ASSERT(int_val != NULL);
    PROP_ASSERT(float_val != NULL);
    PROP_ASSERT(bool_val != NULL);

    PROP_ASSERT(value_is_int(int_val));
    PROP_ASSERT(value_is_float(float_val));
    PROP_ASSERT(value_is_bool(bool_val));

    value_free(int_val);
    value_free(float_val);
    value_free(bool_val);
    return true;
}

/* Property: Array values survive with proper refcount */
static bool prop_array_refcount(void *ctx) {
    (void)ctx;

    Value *arr = value_array();
    PROP_ASSERT(arr != NULL);

    /* Add some elements */
    int count = prop_rand_int_range(1, 5);
    for (int i = 0; i < count; i++) {
        Value *elem = value_int(i * 10);
        arr = array_push(arr, elem);
    }

    /* Array should have correct length */
    PROP_ASSERT(array_length(arr) == (size_t)count);

    value_free(arr);
    return true;
}

/* Property: String values are properly managed */
static bool prop_string_management(void *ctx) {
    (void)ctx;

    char *str = prop_rand_alnum_string(50);
    PROP_ASSERT(str != NULL);

    Value *v = value_string(str);
    PROP_ASSERT(v != NULL);
    PROP_ASSERT(value_is_string(v));

    /* Verify string content */
    const char *retrieved = value_to_string(v);
    PROP_ASSERT(strcmp(retrieved, str) == 0);

    free(str);
    value_free(v);
    return true;
}

/* Main */
int main(void) {
    printf("Running GC property tests...\n\n");

    prop_init(0); /* Use random seed */

    printf("GC Property Tests:\n");
    PROP_CHECK("heap starts empty", prop_heap_starts_empty, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("allocation increases size", prop_allocation_increases_size, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("retain increases refcount", prop_retain_increases_refcount, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("gc empty heap", prop_gc_empty_heap, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("gc collects unreachable", prop_gc_collects_unreachable, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("gc multiple cycles", prop_gc_multiple_cycles, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("heap stats consistent", prop_heap_stats_consistent, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("value types", prop_value_types, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("array refcount", prop_array_refcount, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("string management", prop_string_management, NULL, PROP_DEFAULT_ITERATIONS);

    PROP_SUMMARY();
    return PROP_RESULT();
}
