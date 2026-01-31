/*
 * Agim - GC Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/gc.h"
#include "vm/vm.h"

void test_heap_create(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    ASSERT(heap != NULL);
    ASSERT_EQ(0, heap_used(heap));

    heap_free(heap);
}

void test_heap_alloc(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    Value *v1 = heap_alloc(heap, VAL_INT);
    Value *v2 = heap_alloc(heap, VAL_STRING);
    Value *v3 = heap_alloc(heap, VAL_ARRAY);

    ASSERT(v1 != NULL);
    ASSERT(v2 != NULL);
    ASSERT(v3 != NULL);
    ASSERT(heap_used(heap) > 0);

    heap_free(heap);
}

void test_heap_stats(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    heap_alloc(heap, VAL_INT);
    heap_alloc(heap, VAL_INT);
    heap_alloc(heap, VAL_INT);

    HeapStats stats = heap_stats(heap);
    ASSERT(stats.bytes_allocated > 0);
    ASSERT_EQ(3, stats.objects_allocated);

    heap_free(heap);
}

void test_gc_mark(void) {
    Value *root = value_array();
    root = array_push(root, value_int(1));
    root = array_push(root, value_int(2));

    /* Mark the root */
    gc_mark_value(root);

    ASSERT(value_is_marked(root));
    /* Child values should also be marked */
    ASSERT(value_is_marked(array_get(root, 0)));
    ASSERT(value_is_marked(array_get(root, 1)));

    value_free(root);
}

void test_gc_collect(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate some values */
    Value *v1 = heap_alloc(heap, VAL_INT);
    Value *v2 = heap_alloc(heap, VAL_INT);
    Value *v3 = heap_alloc(heap, VAL_INT);

    /*
     * With COW, values start with refcount=1.
     * Release them to simulate no longer being owned.
     * GC only frees objects that are both unmarked AND refcount==0.
     */
    value_release(v1);
    value_release(v2);
    value_release(v3);

    size_t before = heap_used(heap);

    /* Run GC with empty VM (no roots) */
    gc_collect(heap, vm);

    size_t after = heap_used(heap);

    /* All unreachable objects should be freed */
    ASSERT(after < before);

    vm_free(vm);
    heap_free(heap);
}

/* Test gray list incremental marking */
void test_gc_mark_increment(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate some values to create work for incremental marking */
    Value *arr = heap_alloc(heap, VAL_ARRAY);
    Value *v1 = heap_alloc(heap, VAL_INT);
    Value *v2 = heap_alloc(heap, VAL_INT);

    ASSERT(heap != NULL);
    ASSERT(arr != NULL);

    /* Start incremental GC */
    bool started = gc_start_incremental(heap, vm);
    ASSERT(started);
    ASSERT(gc_in_progress(heap));

    /* Complete the incremental GC */
    gc_complete(heap, vm);
    ASSERT(!gc_in_progress(heap));

    /* Mark increment with empty gray list should return true (complete) */
    bool complete = gc_mark_increment(heap, 100);
    ASSERT(complete);  /* No work to do */

    value_release(arr);
    value_release(v1);
    value_release(v2);

    vm_free(vm);
    heap_free(heap);
}

/* Test card table in write barrier */
void test_gc_card_table(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Enable generational GC */
    gc_set_generational(heap, true);

    /* Allocate and promote an old object */
    Value *old_arr = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old_arr);

    /* Allocate a young object */
    Value *young_val = heap_alloc(heap, VAL_INT);

    /* Write barrier should mark the card as dirty */
    gc_write_barrier(heap, old_arr, young_val);

    /* Verify the write barrier recorded the reference */
    /* (card table is internal but we can check remember set) */
    ASSERT(value_is_remembered(old_arr));

    value_release(old_arr);
    value_release(young_val);

    vm_free(vm);
    heap_free(heap);
}

int main(void) {
    RUN_TEST(test_heap_create);
    RUN_TEST(test_heap_alloc);
    RUN_TEST(test_heap_stats);
    RUN_TEST(test_gc_mark);
    RUN_TEST(test_gc_collect);
    RUN_TEST(test_gc_mark_increment);
    RUN_TEST(test_gc_card_table);

    return TEST_RESULT();
}
