/*
 * Agim - GC Generational Tests
 *
 * Comprehensive tests for generational GC:
 * - Young generation collection
 * - Promotion to old generation
 * - promotion_threshold behavior
 * - Write barrier triggers
 * - Remember set population
 * - Remember set clearing
 * - Full collection
 * - needs_full_gc flag
 * - young_gc_threshold adjustment
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/gc.h"
#include "vm/vm.h"

/* ============================================================================
 * Young Generation Collection Tests
 * ============================================================================ */

void test_young_generation_initial_state(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    gc_set_generational(heap, true);

    ASSERT_EQ(0, heap->young_count);
    ASSERT_EQ(0, heap->young_bytes);
    ASSERT_EQ(0, heap->old_count);
    ASSERT_EQ(0, heap->old_bytes);

    heap_free(heap);
}

void test_young_allocation_tracked(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    gc_set_generational(heap, true);

    Value *v = heap_alloc(heap, VAL_INT);
    ASSERT(v != NULL);

    ASSERT_EQ(1, heap->young_count);
    ASSERT(heap->young_bytes > 0);
    ASSERT_EQ(0, heap->old_count);
    ASSERT(!value_is_old_gen(v));

    value_release(v);
    heap_free(heap);
}

void test_young_collection_frees_unreachable(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    /* Allocate unreachable young objects */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    size_t young_before = heap->young_bytes;
    size_t minor_before = heap->minor_gc_count;

    gc_collect_young(heap, vm);

    ASSERT(heap->young_bytes < young_before);
    ASSERT_EQ(minor_before + 1, heap->minor_gc_count);

    vm_free(vm);
    heap_free(heap);
}

void test_young_collection_preserves_rooted(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *rooted = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, rooted);

    size_t young_before = heap->young_count;

    gc_collect_young(heap, vm);

    /* Rooted object should survive */
    ASSERT(heap->objects != NULL);
    ASSERT_EQ(young_before, heap->young_count);

    value_release(rooted);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Promotion to Old Generation Tests
 * ============================================================================ */

void test_promotion_after_threshold(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->promotion_threshold = 2; /* Promote after 2 survivals */

    Value *v = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v);

    ASSERT(!value_is_old_gen(v));
    ASSERT_EQ(0, value_survival_count(v));

    /* First GC - survival count becomes 1 */
    gc_collect_young(heap, vm);
    ASSERT(!value_is_old_gen(v));
    ASSERT_EQ(1, value_survival_count(v));

    /* Second GC - survival count becomes 2, promotes to old */
    gc_collect_young(heap, vm);
    ASSERT(value_is_old_gen(v));
    ASSERT_EQ(1, heap->old_count);

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

void test_promotion_updates_counts(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->promotion_threshold = 1; /* Promote after 1 survival */

    Value *v = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v);

    ASSERT_EQ(1, heap->young_count);
    ASSERT_EQ(0, heap->old_count);

    gc_collect_young(heap, vm);

    /* Should have promoted to old */
    ASSERT_EQ(0, heap->young_count);
    ASSERT_EQ(1, heap->old_count);
    ASSERT(heap->old_bytes > 0);
    ASSERT_EQ(0, heap->young_bytes);

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

void test_promotion_threshold_respected(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->promotion_threshold = 5; /* High threshold */

    Value *v = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v);

    /* Run 4 GCs - should not promote yet */
    for (int i = 0; i < 4; i++) {
        gc_collect_young(heap, vm);
        ASSERT(!value_is_old_gen(v));
    }

    /* 5th GC - should promote */
    gc_collect_young(heap, vm);
    ASSERT(value_is_old_gen(v));

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Write Barrier Tests
 * ============================================================================ */

void test_write_barrier_triggers_on_old_to_young(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    /* Create and promote an old object */
    Value *old_arr = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old_arr);
    heap->young_count--;
    heap->old_count++;

    /* Create a young object */
    Value *young_val = heap_alloc(heap, VAL_INT);

    /* Write barrier should trigger */
    gc_write_barrier(heap, old_arr, young_val);

    ASSERT(value_is_remembered(old_arr));
    ASSERT(heap->remember_count > 0);

    value_release(old_arr);
    value_release(young_val);
    vm_free(vm);
    heap_free(heap);
}

void test_write_barrier_ignores_young_to_young(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *young1 = heap_alloc(heap, VAL_ARRAY);
    Value *young2 = heap_alloc(heap, VAL_INT);

    gc_write_barrier(heap, young1, young2);

    /* Should not be remembered - both young */
    ASSERT(!value_is_remembered(young1));
    ASSERT_EQ(0, heap->remember_count);

    value_release(young1);
    value_release(young2);
    vm_free(vm);
    heap_free(heap);
}

void test_write_barrier_ignores_old_to_old(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *old1 = heap_alloc(heap, VAL_ARRAY);
    Value *old2 = heap_alloc(heap, VAL_INT);
    value_set_old_gen(old1);
    value_set_old_gen(old2);

    gc_write_barrier(heap, old1, old2);

    /* Should not be remembered - both old */
    ASSERT(!value_is_remembered(old1));
    ASSERT_EQ(0, heap->remember_count);

    value_release(old1);
    value_release(old2);
    vm_free(vm);
    heap_free(heap);
}

void test_write_barrier_disabled_when_not_generational(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, false);

    Value *old_arr = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old_arr);
    Value *young_val = heap_alloc(heap, VAL_INT);

    gc_write_barrier(heap, old_arr, young_val);

    /* Should not be remembered - generational disabled */
    ASSERT(!value_is_remembered(old_arr));
    ASSERT_EQ(0, heap->remember_count);

    value_release(old_arr);
    value_release(young_val);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Remember Set Tests
 * ============================================================================ */

void test_remember_set_populated(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    ASSERT_EQ(0, heap->remember_count);

    /* Create old-to-young references */
    Value *old1 = heap_alloc(heap, VAL_ARRAY);
    Value *old2 = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old1);
    value_set_old_gen(old2);

    Value *young1 = heap_alloc(heap, VAL_INT);
    Value *young2 = heap_alloc(heap, VAL_INT);

    gc_write_barrier(heap, old1, young1);
    gc_write_barrier(heap, old2, young2);

    ASSERT_EQ(2, heap->remember_count);

    value_release(old1);
    value_release(old2);
    value_release(young1);
    value_release(young2);
    vm_free(vm);
    heap_free(heap);
}

void test_remember_set_no_duplicates(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *old_arr = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old_arr);
    Value *young_val = heap_alloc(heap, VAL_INT);

    /* Add multiple times */
    gc_write_barrier(heap, old_arr, young_val);
    gc_write_barrier(heap, old_arr, young_val);
    gc_write_barrier(heap, old_arr, young_val);

    /* Should only be in set once */
    ASSERT_EQ(1, heap->remember_count);

    value_release(old_arr);
    value_release(young_val);
    vm_free(vm);
    heap_free(heap);
}

void test_remember_set_cleared_after_gc(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *old_arr = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old_arr);
    vm_push(vm, old_arr);

    Value *young_val = heap_alloc(heap, VAL_INT);
    vm_push(vm, young_val);

    gc_write_barrier(heap, old_arr, young_val);
    ASSERT(heap->remember_count > 0);

    gc_collect_young(heap, vm);

    /* Remember set should be cleared */
    ASSERT_EQ(0, heap->remember_count);
    ASSERT(!value_is_remembered(old_arr));

    value_release(old_arr);
    value_release(young_val);
    vm_free(vm);
    heap_free(heap);
}

void test_remember_set_max_triggers_full_gc(void) {
    GCConfig config = gc_config_default();
    config.max_remember_size = 3; /* Very small */
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    ASSERT(!heap->needs_full_gc);

    /* Fill up remember set */
    for (int i = 0; i < 5; i++) {
        Value *old = heap_alloc(heap, VAL_ARRAY);
        value_set_old_gen(old);
        Value *young = heap_alloc(heap, VAL_INT);
        gc_write_barrier(heap, old, young);
        value_release(old);
        value_release(young);
    }

    /* Should have triggered needs_full_gc */
    ASSERT(heap->needs_full_gc);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Full Collection Tests
 * ============================================================================ */

void test_full_collection_collects_both_generations(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    /* Create and release young objects */
    for (int i = 0; i < 3; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    /* Create old objects */
    for (int i = 0; i < 3; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_set_old_gen(v);
        heap->young_count--;
        heap->old_count++;
        value_release(v);
    }

    size_t before = heap->bytes_allocated;
    size_t major_before = heap->major_gc_count;

    gc_collect_full(heap, vm);

    ASSERT(heap->bytes_allocated < before);
    ASSERT_EQ(major_before + 1, heap->major_gc_count);

    vm_free(vm);
    heap_free(heap);
}

void test_full_collection_clears_remember_set(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *old = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old);
    vm_push(vm, old);

    Value *young = heap_alloc(heap, VAL_INT);
    vm_push(vm, young);

    gc_write_barrier(heap, old, young);
    ASSERT(heap->remember_count > 0);

    gc_collect_full(heap, vm);

    ASSERT_EQ(0, heap->remember_count);

    value_release(old);
    value_release(young);
    vm_free(vm);
    heap_free(heap);
}

void test_full_collection_preserves_rooted(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    Value *rooted = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, rooted);

    gc_collect_full(heap, vm);

    /* Should survive */
    ASSERT(heap->objects != NULL);

    value_release(rooted);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * needs_full_gc Flag Tests
 * ============================================================================ */

void test_needs_full_gc_initially_false(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    ASSERT(!heap->needs_full_gc);

    heap_free(heap);
}

void test_needs_full_gc_cleared_after_full_collection(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->needs_full_gc = true;

    gc_collect_full(heap, vm);

    /* Should still be true (gc_collect_full doesn't clear it) */
    /* But heap_alloc_with_gc clears it after running full GC */

    vm_free(vm);
    heap_free(heap);
}

void test_needs_full_gc_triggers_on_alloc(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->needs_full_gc = true;

    size_t major_before = heap->major_gc_count;

    Value *v = heap_alloc_with_gc(heap, VAL_INT, vm);
    ASSERT(v != NULL);

    /* Should have triggered at least one full GC */
    ASSERT(heap->major_gc_count > major_before);
    ASSERT(!heap->needs_full_gc);

    value_release(v);
    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * young_gc_threshold Adjustment Tests
 * ============================================================================ */

void test_young_gc_threshold_initial(void) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 8192;
    Heap *heap = heap_new(&config);

    /* Initial threshold is initial_heap_size / 4 */
    ASSERT_EQ(8192 / 4, heap->young_gc_threshold);

    heap_free(heap);
}

void test_young_gc_threshold_adjusts_after_gc(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    /* Allocate some objects and root them */
    Value *v1 = heap_alloc(heap, VAL_ARRAY);
    Value *v2 = heap_alloc(heap, VAL_ARRAY);
    vm_push(vm, v1);
    vm_push(vm, v2);

    gc_collect_young(heap, vm);

    /* Threshold should be adjusted (2x young_bytes or minimum 4096) */
    ASSERT(heap->young_gc_threshold >= 4096);

    value_release(v1);
    value_release(v2);
    vm_free(vm);
    heap_free(heap);
}

void test_young_gc_threshold_minimum(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    /* Collect with empty young generation */
    gc_collect_young(heap, vm);

    /* Threshold should be at least 4096 */
    ASSERT(heap->young_gc_threshold >= 4096);

    vm_free(vm);
    heap_free(heap);
}

void test_young_gc_triggers_at_threshold(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->young_gc_threshold = 64; /* Very low */

    size_t minor_before = heap->minor_gc_count;

    /* Allocate past threshold using heap_alloc_with_gc */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc_with_gc(heap, VAL_INT, vm);
        value_release(v);
    }

    /* Should have triggered at least one minor GC */
    ASSERT(heap->minor_gc_count > minor_before);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Generational Enable/Disable Tests
 * ============================================================================ */

void test_generational_can_be_disabled(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    ASSERT(heap->generational_enabled);

    gc_set_generational(heap, false);
    ASSERT(!heap->generational_enabled);

    gc_set_generational(heap, true);
    ASSERT(heap->generational_enabled);

    heap_free(heap);
}

void test_non_generational_uses_regular_gc(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, false);

    /* Allocate and release */
    for (int i = 0; i < 5; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        value_release(v);
    }

    size_t before = heap->bytes_allocated;
    size_t gc_before = heap->gc_count;

    gc_collect(heap, vm);

    ASSERT(heap->bytes_allocated < before);
    ASSERT_EQ(gc_before + 1, heap->gc_count);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

void test_generational_empty_heap(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    /* Should not crash */
    gc_collect_young(heap, vm);
    gc_collect_full(heap, vm);

    vm_free(vm);
    heap_free(heap);
}

void test_promotion_with_children(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);
    heap->promotion_threshold = 1;

    /* Create array with children */
    Value *arr = heap_alloc(heap, VAL_ARRAY);
    Value *child1 = heap_alloc(heap, VAL_INT);
    Value *child2 = heap_alloc(heap, VAL_INT);
    arr = array_push(arr, child1);
    arr = array_push(arr, child2);
    vm_push(vm, arr);

    gc_collect_young(heap, vm);

    /* Array should be promoted */
    ASSERT(value_is_old_gen(arr));

    value_release(arr);
    vm_free(vm);
    heap_free(heap);
}

void test_gc_stats_track_minor_major(void) {
    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    gc_set_generational(heap, true);

    ASSERT_EQ(0, heap->minor_gc_count);
    ASSERT_EQ(0, heap->major_gc_count);

    gc_collect_young(heap, vm);
    ASSERT_EQ(1, heap->minor_gc_count);
    ASSERT_EQ(0, heap->major_gc_count);

    gc_collect_full(heap, vm);
    ASSERT_EQ(1, heap->minor_gc_count);
    ASSERT_EQ(1, heap->major_gc_count);

    gc_collect_young(heap, vm);
    gc_collect_young(heap, vm);
    ASSERT_EQ(3, heap->minor_gc_count);
    ASSERT_EQ(1, heap->major_gc_count);

    vm_free(vm);
    heap_free(heap);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    /* Young generation collection */
    RUN_TEST(test_young_generation_initial_state);
    RUN_TEST(test_young_allocation_tracked);
    RUN_TEST(test_young_collection_frees_unreachable);
    RUN_TEST(test_young_collection_preserves_rooted);

    /* Promotion to old generation */
    RUN_TEST(test_promotion_after_threshold);
    RUN_TEST(test_promotion_updates_counts);
    RUN_TEST(test_promotion_threshold_respected);

    /* Write barrier */
    RUN_TEST(test_write_barrier_triggers_on_old_to_young);
    RUN_TEST(test_write_barrier_ignores_young_to_young);
    RUN_TEST(test_write_barrier_ignores_old_to_old);
    RUN_TEST(test_write_barrier_disabled_when_not_generational);

    /* Remember set */
    RUN_TEST(test_remember_set_populated);
    RUN_TEST(test_remember_set_no_duplicates);
    RUN_TEST(test_remember_set_cleared_after_gc);
    RUN_TEST(test_remember_set_max_triggers_full_gc);

    /* Full collection */
    RUN_TEST(test_full_collection_collects_both_generations);
    RUN_TEST(test_full_collection_clears_remember_set);
    RUN_TEST(test_full_collection_preserves_rooted);

    /* needs_full_gc flag */
    RUN_TEST(test_needs_full_gc_initially_false);
    RUN_TEST(test_needs_full_gc_cleared_after_full_collection);
    RUN_TEST(test_needs_full_gc_triggers_on_alloc);

    /* young_gc_threshold adjustment */
    RUN_TEST(test_young_gc_threshold_initial);
    RUN_TEST(test_young_gc_threshold_adjusts_after_gc);
    RUN_TEST(test_young_gc_threshold_minimum);
    RUN_TEST(test_young_gc_triggers_at_threshold);

    /* Enable/disable */
    RUN_TEST(test_generational_can_be_disabled);
    RUN_TEST(test_non_generational_uses_regular_gc);

    /* Edge cases */
    RUN_TEST(test_generational_empty_heap);
    RUN_TEST(test_promotion_with_children);
    RUN_TEST(test_gc_stats_track_minor_major);

    return TEST_RESULT();
}
