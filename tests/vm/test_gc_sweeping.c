/*
 * Agim - GC Sweeping Tests
 *
 * Comprehensive tests for GC sweep phase:
 * - Unmarked objects freed
 * - Marked objects preserved
 * - Mark bit cleared after sweep
 * - bytes_allocated updated
 * - Object list maintained
 * - Sweep handles cycles
 * - Incremental sweeping
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/gc.h"
#include "vm/vm.h"

/* ============================================================================
 * Unmarked Objects Freed Tests
 * ============================================================================ */

void test_unmarked_objects_freed(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate objects but don't keep references */
    Value *v1 = heap_alloc(heap, VAL_INT);
    Value *v2 = heap_alloc(heap, VAL_INT);
    Value *v3 = heap_alloc(heap, VAL_INT);

    /* Release them so they can be collected */
    value_release(v1);
    value_release(v2);
    value_release(v3);

    size_t before = heap->bytes_allocated;
    ASSERT(before > 0);

    /* Run GC with empty VM (no roots) */
    gc_collect(heap, vm);

    /* All unreachable objects should be freed */
    ASSERT(heap->bytes_allocated < before);

    vm_free(vm);
    heap_free(heap);
}

void test_unreferenced_objects_collected(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create several unreferenced objects */
    for (int i = 0; i < 10; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    size_t before = heap->bytes_allocated;
    size_t before_freed = heap->total_freed;

    gc_collect(heap, vm);

    /* Should have freed memory */
    ASSERT(heap->bytes_allocated < before);
    ASSERT(heap->total_freed > before_freed);

    vm_free(vm);
    heap_free(heap);
}

void test_array_unreferenced_collected(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *arr = heap_alloc(heap, VAL_ARRAY);
    arr = array_push(arr, heap_alloc(heap, VAL_INT));
    arr = array_push(arr, heap_alloc(heap, VAL_INT));
    value_release(arr);

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    ASSERT(heap->bytes_allocated < before);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Marked Objects Preserved Tests
 * ============================================================================ */

void test_marked_objects_preserved(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate and push to stack (makes it a root) */
    Value *v = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v);

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    /* Object should still exist */
    ASSERT(heap->bytes_allocated == before);
    /* Object should still be in heap's object list */
    ASSERT(heap->objects != NULL);

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

void test_stack_roots_preserved(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Push multiple values to stack */
    Value *v1 = heap_alloc(heap, VAL_ARRAY);
    Value *v2 = heap_alloc(heap, VAL_MAP);
    Value *v3 = heap_alloc(heap, VAL_STRING);

    vm_push(vm, v1);
    vm_push(vm, v2);
    vm_push(vm, v3);

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    /* All rooted objects should be preserved */
    ASSERT(heap->bytes_allocated == before);

    value_release(v1);
    value_release(v2);
    value_release(v3);
    vm_free(vm);
    heap_free(heap);
}

void test_reachable_children_preserved(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create array with children */
    Value *arr = heap_alloc(heap, VAL_ARRAY);
    Value *c1 = heap_alloc(heap, VAL_STRING);
    Value *c2 = heap_alloc(heap, VAL_STRING);
    arr = array_push(arr, c1);
    arr = array_push(arr, c2);

    vm_push(vm, arr);

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    /* Array and children should be preserved */
    ASSERT(heap->bytes_allocated == before);

    value_release(arr);
    vm_free(vm);
    heap_free(heap);
}

void test_mixed_reachable_unreachable(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Some rooted, some not */
    Value *rooted = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, rooted);

    Value *unrooted1 = heap_alloc(heap, VAL_INT);
    Value *unrooted2 = heap_alloc(heap, VAL_INT);
    value_release(unrooted1);
    value_release(unrooted2);

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    /* Some memory should be freed, but not all */
    ASSERT(heap->bytes_allocated < before);
    ASSERT(heap->bytes_allocated > 0);

    value_release(rooted);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Mark Bit Cleared After Sweep Tests
 * ============================================================================ */

void test_mark_bit_cleared_after_sweep(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v);

    /* Mark bit should be clear initially */
    ASSERT(!value_is_marked(v));

    gc_collect(heap, vm);

    /* Mark bit should be cleared after sweep for surviving objects */
    ASSERT(!value_is_marked(v));

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

void test_mark_bit_cleared_multiple_gcs(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v);

    /* Run multiple GC cycles */
    for (int i = 0; i < 5; i++) {
        gc_collect(heap, vm);
        /* Mark bit should always be clear after GC */
        ASSERT(!value_is_marked(v));
    }

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * bytes_allocated Updated Tests
 * ============================================================================ */

void test_bytes_allocated_decreases_on_sweep(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate then release */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    ASSERT(heap->bytes_allocated < before);

    vm_free(vm);
    heap_free(heap);
}

void test_bytes_allocated_unchanged_if_all_rooted(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* All objects rooted */
    Value *v1 = heap_alloc(heap, VAL_ARRAY);
    Value *v2 = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v1);
    vm_push(vm, v2);

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    ASSERT_EQ(before, heap->bytes_allocated);

    value_release(v1);
    value_release(v2);
    vm_free(vm);
    heap_free(heap);
}

void test_total_freed_tracks_cumulative(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    ASSERT_EQ(0, heap->total_freed);

    /* First batch */
    for (int i = 0; i < 3; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }
    gc_collect(heap, vm);
    size_t first_freed = heap->total_freed;
    ASSERT(first_freed > 0);

    /* Second batch */
    for (int i = 0; i < 3; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }
    gc_collect(heap, vm);

    /* Should have freed more total */
    ASSERT(heap->total_freed > first_freed);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Object List Maintained Tests
 * ============================================================================ */

void test_object_list_maintained_after_sweep(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create some objects and root them */
    Value *v1 = heap_alloc(heap, VAL_ARRAY);
    Value *v2 = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v1);
    vm_push(vm, v2);

    /* Create some unreferenced */
    Value *u1 = heap_alloc(heap, VAL_INT);
    value_release(u1);

    gc_collect(heap, vm);

    /* Object list should still contain rooted objects */
    Value *obj = heap->objects;
    int count = 0;
    while (obj) {
        count++;
        obj = obj->next;
    }
    ASSERT_EQ(2, count); /* v1 and v2 */

    value_release(v1);
    value_release(v2);
    vm_free(vm);
    heap_free(heap);
}

void test_object_list_empty_after_full_sweep(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create objects but don't root them */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    gc_collect(heap, vm);

    /* Object list should be empty */
    ASSERT(heap->objects == NULL);
    ASSERT_EQ(0, heap->bytes_allocated);

    vm_free(vm);
    heap_free(heap);
}

void test_object_list_links_correct(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v1 = heap_alloc(heap, VAL_ARRAY);
    Value *v2 = heap_alloc(heap, VAL_ARRAY);
    Value *v3 = heap_alloc(heap, VAL_ARRAY);

    vm_push(vm, v1);
    vm_push(vm, v2);
    vm_push(vm, v3);

    /* Add some unreferenced in between */
    Value *u = heap_alloc(heap, VAL_INT);
    value_release(u);

    gc_collect(heap, vm);

    /* Walk the list and verify it's valid */
    Value *obj = heap->objects;
    int count = 0;
    while (obj && count < 100) { /* Prevent infinite loop */
        count++;
        obj = obj->next;
    }
    ASSERT_EQ(3, count);

    value_release(v1);
    value_release(v2);
    value_release(v3);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Sweep Handles Cycles Tests
 * ============================================================================ */

void test_sweep_handles_self_referencing_map(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create a self-referencing structure */
    Value *m = heap_alloc(heap, VAL_MAP);
    m = map_set(m, "self", m);

    /* Root it */
    vm_push(vm, m);

    gc_collect(heap, vm);

    /* Should still exist */
    ASSERT(heap->objects != NULL);

    value_release(m);
    vm_free(vm);
    heap_free(heap);
}

void test_sweep_handles_mutual_references(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *m1 = heap_alloc(heap, VAL_MAP);
    Value *m2 = heap_alloc(heap, VAL_MAP);

    m1 = map_set(m1, "other", m2);
    m2 = map_set(m2, "other", m1);

    vm_push(vm, m1);

    gc_collect(heap, vm);

    /* Both should survive (m2 reachable through m1) */
    int count = 0;
    Value *obj = heap->objects;
    while (obj) {
        count++;
        obj = obj->next;
    }
    ASSERT_EQ(2, count);

    value_release(m1);
    vm_free(vm);
    heap_free(heap);
}

void test_sweep_handles_unreachable_cycle(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create a cycle that isn't rooted */
    Value *m1 = heap_alloc(heap, VAL_MAP);
    Value *m2 = heap_alloc(heap, VAL_MAP);

    m1 = map_set(m1, "other", m2);
    m2 = map_set(m2, "other", m1);

    /* Release both - cycle is unreachable */
    value_release(m1);
    value_release(m2);

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    /* Cycle should be collected */
    ASSERT(heap->bytes_allocated < before);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Incremental Sweeping Tests
 * ============================================================================ */

void test_incremental_gc_sweeps(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create garbage */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    size_t before = heap->bytes_allocated;

    /* Run incremental GC */
    gc_start_incremental(heap, vm);
    while (gc_in_progress(heap)) {
        gc_step(heap, vm);
    }

    /* Should have collected garbage */
    ASSERT(heap->bytes_allocated < before);

    vm_free(vm);
    heap_free(heap);
}

void test_incremental_gc_preserves_roots(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *rooted = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, rooted);

    /* Also create garbage */
    Value *garbage = heap_alloc(heap, VAL_INT);
    value_release(garbage);

    gc_start_incremental(heap, vm);
    gc_complete(heap, vm);

    /* Rooted object should survive */
    ASSERT(heap->objects != NULL);

    value_release(rooted);
    vm_free(vm);
    heap_free(heap);
}

void test_gc_step_makes_progress(void) {
    GCConfig config = gc_config_default();
    config.incremental_step = 1; /* Small step size */
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Create objects */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    gc_start_incremental(heap, vm);

    /* Step should eventually complete */
    int steps = 0;
    while (gc_in_progress(heap) && steps < 1000) {
        gc_step(heap, vm);
        steps++;
    }

    ASSERT(!gc_in_progress(heap));
    ASSERT(steps < 1000);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * GC Statistics Tests
 * ============================================================================ */

void test_gc_count_increments(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    ASSERT_EQ(0, heap->gc_count);

    gc_collect(heap, vm);
    ASSERT_EQ(1, heap->gc_count);

    gc_collect(heap, vm);
    ASSERT_EQ(2, heap->gc_count);

    gc_collect(heap, vm);
    ASSERT_EQ(3, heap->gc_count);

    vm_free(vm);
    heap_free(heap);
}

void test_heap_stats_accurate(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate and release */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    gc_collect(heap, vm);

    HeapStats stats = heap_stats(heap);
    ASSERT(stats.bytes_freed > 0);
    /* Note: objects_freed is always 0 in current implementation */
    ASSERT_EQ(1, stats.gc_runs);
    ASSERT_EQ(0, stats.bytes_allocated); /* All freed */

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

void test_sweep_empty_heap(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* GC on empty heap should not crash */
    gc_collect(heap, vm);

    ASSERT_EQ(0, heap->bytes_allocated);
    ASSERT_EQ(1, heap->gc_count);

    vm_free(vm);
    heap_free(heap);
}

void test_sweep_single_object(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    Value *v = heap_alloc(heap, VAL_INT);
    value_release(v);

    gc_collect(heap, vm);

    ASSERT_EQ(0, heap->bytes_allocated);
    ASSERT(heap->objects == NULL);

    vm_free(vm);
    heap_free(heap);
}

void test_sweep_all_rooted(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* All objects rooted */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_ARRAY);
        vm_push(vm, v);
    }

    size_t before = heap->bytes_allocated;

    gc_collect(heap, vm);

    /* Nothing should be freed */
    ASSERT_EQ(before, heap->bytes_allocated);

    vm_free(vm);
    heap_free(heap);
}

void test_next_gc_updated_after_collect(void) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 128;
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate some objects */
    for (int i = 0; i < 3; i++) {
        Value *v = heap_alloc(heap, VAL_ARRAY);
        vm_push(vm, v);
    }

    gc_collect(heap, vm);

    /* next_gc should be updated based on current allocation */
    /* It's typically set to 2x current allocation */
    ASSERT(heap->next_gc >= heap->bytes_allocated);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    /* Unmarked objects freed */
    RUN_TEST(test_unmarked_objects_freed);
    RUN_TEST(test_unreferenced_objects_collected);
    RUN_TEST(test_array_unreferenced_collected);

    /* Marked objects preserved */
    RUN_TEST(test_marked_objects_preserved);
    RUN_TEST(test_stack_roots_preserved);
    RUN_TEST(test_reachable_children_preserved);
    RUN_TEST(test_mixed_reachable_unreachable);

    /* Mark bit cleared after sweep */
    RUN_TEST(test_mark_bit_cleared_after_sweep);
    RUN_TEST(test_mark_bit_cleared_multiple_gcs);

    /* bytes_allocated updated */
    RUN_TEST(test_bytes_allocated_decreases_on_sweep);
    RUN_TEST(test_bytes_allocated_unchanged_if_all_rooted);
    RUN_TEST(test_total_freed_tracks_cumulative);

    /* Object list maintained */
    RUN_TEST(test_object_list_maintained_after_sweep);
    RUN_TEST(test_object_list_empty_after_full_sweep);
    RUN_TEST(test_object_list_links_correct);

    /* Sweep handles cycles */
    RUN_TEST(test_sweep_handles_self_referencing_map);
    RUN_TEST(test_sweep_handles_mutual_references);
    RUN_TEST(test_sweep_handles_unreachable_cycle);

    /* Incremental sweeping */
    RUN_TEST(test_incremental_gc_sweeps);
    RUN_TEST(test_incremental_gc_preserves_roots);
    RUN_TEST(test_gc_step_makes_progress);

    /* GC statistics */
    RUN_TEST(test_gc_count_increments);
    RUN_TEST(test_heap_stats_accurate);

    /* Edge cases */
    RUN_TEST(test_sweep_empty_heap);
    RUN_TEST(test_sweep_single_object);
    RUN_TEST(test_sweep_all_rooted);
    RUN_TEST(test_next_gc_updated_after_collect);

    return TEST_RESULT();
}
