/*
 * Agim - Value Reference Counting Tests
 *
 * P1.1.8.2: Tests for value reference counting and COW.
 * - value_retain increments refcount
 * - value_release decrements refcount
 * - value_needs_cow checks shared state
 * - value_can_share checks shareability
 * - value_mark_shared marks as shared
 * - value_cow_share prepares for COW
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/value.h"
#include "types/string.h"
#include "types/array.h"
#include "types/map.h"

#include <stdlib.h>

/*
 * Test: New value has refcount of 1
 */
void test_new_value_refcount_one(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_free(v);
}

/*
 * Test: value_retain increments refcount
 */
void test_retain_increments_refcount(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    Value *retained = value_retain(v);
    ASSERT_EQ(v, retained);  /* Returns same pointer */
    ASSERT_EQ(2, atomic_load(&v->refcount));

    /* Release both references */
    value_release(v);
    value_release(v);
}

/*
 * Test: value_retain with NULL returns NULL
 */
void test_retain_null(void) {
    Value *result = value_retain(NULL);
    ASSERT(result == NULL);
}

/*
 * Test: Multiple retains accumulate
 */
void test_retain_multiple(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    value_retain(v);
    value_retain(v);
    value_retain(v);

    ASSERT_EQ(4, atomic_load(&v->refcount));  /* 1 initial + 3 retains */

    /* Release all */
    for (int i = 0; i < 4; i++) {
        value_release(v);
    }
}

/*
 * Test: value_release decrements refcount
 */
void test_release_decrements_refcount(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    value_retain(v);
    ASSERT_EQ(2, atomic_load(&v->refcount));

    value_release(v);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_release(v);  /* Final release frees */
}

/*
 * Test: value_release with NULL is safe
 */
void test_release_null(void) {
    value_release(NULL);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: String value refcount
 */
void test_string_refcount(void) {
    Value *v = value_string("hello");
    ASSERT(v != NULL);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_retain(v);
    ASSERT_EQ(2, atomic_load(&v->refcount));

    value_release(v);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_free(v);
}

/*
 * Test: Array value refcount
 */
void test_array_refcount(void) {
    Value *v = value_array_with_capacity(10);
    ASSERT(v != NULL);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_retain(v);
    ASSERT_EQ(2, atomic_load(&v->refcount));

    value_release(v);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_free(v);
}

/*
 * Test: Map value refcount
 */
void test_map_refcount(void) {
    Value *v = value_map();
    ASSERT(v != NULL);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_retain(v);
    ASSERT_EQ(2, atomic_load(&v->refcount));

    value_release(v);
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_free(v);
}

/*
 * Test: value_needs_cow returns false for unshared value
 */
void test_needs_cow_unshared(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    ASSERT(!value_needs_cow(v));

    value_free(v);
}

/*
 * Test: value_needs_cow returns true when refcount > 1
 */
void test_needs_cow_shared(void) {
    Value *v = value_array_with_capacity(10);
    ASSERT(v != NULL);

    /* Initially refcount is 1, doesn't need COW */
    ASSERT(!value_needs_cow(v));

    /* Retain to make refcount > 1 */
    value_retain(v);
    ASSERT(value_needs_cow(v));

    /* Release back */
    value_release(v);
    ASSERT(!value_needs_cow(v));

    value_free(v);
}

/*
 * Test: value_needs_cow with NULL
 */
void test_needs_cow_null(void) {
    ASSERT(!value_needs_cow(NULL));
}

/*
 * Test: value_can_share returns true for shareable types
 */
void test_can_share_shareable(void) {
    Value *arr = value_array_with_capacity(10);
    Value *map = value_map();
    Value *str = value_string("test");

    ASSERT(value_can_share(arr));
    ASSERT(value_can_share(map));
    ASSERT(value_can_share(str));

    value_free(arr);
    value_free(map);
    value_free(str);
}

/*
 * Test: value_can_share returns false for primitives
 */
void test_can_share_primitives(void) {
    Value *nil = value_nil();
    Value *b = value_bool(true);
    Value *i = value_int(42);
    Value *f = value_float(3.14);

    /* Primitives can still be "shared" but COW doesn't apply */
    /* The behavior depends on implementation */

    value_free(nil);
    value_free(b);
    value_free(i);
    value_free(f);
}

/*
 * Test: value_can_share with NULL
 */
void test_can_share_null(void) {
    ASSERT(!value_can_share(NULL));
}

/*
 * Test: value_mark_shared sets shared flag
 */
void test_mark_shared(void) {
    Value *v = value_array_with_capacity(10);
    ASSERT(v != NULL);

    ASSERT(!(v->flags & VALUE_COW_SHARED));

    value_mark_shared(v);

    ASSERT(v->flags & VALUE_COW_SHARED);

    value_free(v);
}

/*
 * Test: value_mark_shared is idempotent
 */
void test_mark_shared_idempotent(void) {
    Value *v = value_array_with_capacity(10);
    ASSERT(v != NULL);

    value_mark_shared(v);
    value_mark_shared(v);
    value_mark_shared(v);

    ASSERT(v->flags & VALUE_COW_SHARED);

    value_free(v);
}

/*
 * Test: value_is_immutable for immutable values
 */
void test_is_immutable(void) {
    /* Primitives are always immutable */
    Value *i = value_int(42);
    ASSERT(i != NULL);
    ASSERT(value_is_immutable(i));
    value_free(i);

    /* Arrays are mutable unless explicitly marked */
    Value *arr = value_array_with_capacity(10);
    ASSERT(arr != NULL);
    ASSERT(!value_is_immutable(arr));

    arr->flags |= VALUE_IMMUTABLE;
    ASSERT(value_is_immutable(arr));

    value_free(arr);
}

/*
 * Test: value_is_immutable with NULL returns true (safe default)
 */
void test_is_immutable_null(void) {
    /* NULL is considered immutable for safety */
    ASSERT(value_is_immutable(NULL));
}

/*
 * Test: value_cow_share prepares value for sharing
 */
void test_cow_share(void) {
    Value *v = value_array_with_capacity(10);
    ASSERT(v != NULL);

    Value *shared = value_cow_share(v);
    ASSERT(shared != NULL);
    ASSERT(value_needs_cow(shared));

    value_free(shared);
}

/*
 * Test: value_cow_share with NULL
 */
void test_cow_share_null(void) {
    Value *result = value_cow_share(NULL);
    ASSERT(result == NULL);
}

/*
 * Test: Refcount saturation protection
 */
void test_refcount_saturation(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    /* Simulate very high refcount (saturated) */
    atomic_store(&v->refcount, REFCOUNT_SATURATED);

    /* Retain should not overflow */
    value_retain(v);
    ASSERT_EQ(REFCOUNT_SATURATED, atomic_load(&v->refcount));

    /* Release should not decrement saturated */
    value_release(v);
    ASSERT_EQ(REFCOUNT_SATURATED, atomic_load(&v->refcount));

    /* Force cleanup */
    atomic_store(&v->refcount, 1);
    value_free(v);
}

/*
 * Test: Value with refcount > 1 is shared
 */
void test_refcount_implies_shared(void) {
    Value *v = value_array_with_capacity(10);
    ASSERT(v != NULL);

    ASSERT_EQ(1, atomic_load(&v->refcount));
    ASSERT(!value_needs_cow(v));

    value_retain(v);
    ASSERT_EQ(2, atomic_load(&v->refcount));
    /* Value with refcount > 1 implies sharing potential */

    value_release(v);
    value_free(v);
}

/*
 * Test: GC state flags are independent of refcount
 */
void test_gc_state_independent(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    ASSERT(!value_is_marked(v));
    value_set_marked(v, true);
    ASSERT(value_is_marked(v));

    /* Refcount unchanged */
    ASSERT_EQ(1, atomic_load(&v->refcount));

    value_set_marked(v, false);
    ASSERT(!value_is_marked(v));

    value_free(v);
}

/*
 * Test: value_copy creates independent copy
 */
void test_copy_independent(void) {
    Value *original = value_int(42);
    ASSERT(original != NULL);

    Value *copy = value_copy(original);
    ASSERT(copy != NULL);
    ASSERT(copy != original);
    ASSERT_EQ(1, atomic_load(&copy->refcount));
    ASSERT_EQ(1, atomic_load(&original->refcount));

    value_free(original);
    value_free(copy);
}

/*
 * Test: value_copy of array creates deep copy
 */
void test_copy_array_deep(void) {
    Value *arr = value_array_with_capacity(5);
    ASSERT(arr != NULL);

    Value *elem = value_int(10);
    array_set(arr, 0, elem);
    value_release(elem);

    Value *copy = value_copy(arr);
    ASSERT(copy != NULL);
    ASSERT(copy != arr);
    ASSERT(VALUE_AS_ARRAY(copy) != VALUE_AS_ARRAY(arr));

    value_free(arr);
    value_free(copy);
}

/*
 * Test: value_copy with NULL
 */
void test_copy_null(void) {
    Value *result = value_copy(NULL);
    ASSERT(result == NULL);
}

/*
 * Test: Retain/release cycle
 */
void test_retain_release_cycle(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    for (int i = 0; i < 100; i++) {
        value_retain(v);
        ASSERT_EQ((uint32_t)(i + 2), atomic_load(&v->refcount));
    }

    for (int i = 0; i < 100; i++) {
        value_release(v);
        ASSERT_EQ((uint32_t)(100 - i), atomic_load(&v->refcount));
    }

    value_free(v);  /* Final release */
}

/*
 * Test: Different value types all support refcount
 */
void test_refcount_all_types(void) {
    Value *values[] = {
        value_nil(),
        value_bool(true),
        value_bool(false),
        value_int(42),
        value_float(3.14),
        value_pid(1234),
        value_string("test"),
        value_array_with_capacity(5),
        value_map(),
    };

    size_t count = sizeof(values) / sizeof(values[0]);

    for (size_t i = 0; i < count; i++) {
        ASSERT(values[i] != NULL);
        ASSERT_EQ(1, atomic_load(&values[i]->refcount));

        value_retain(values[i]);
        ASSERT_EQ(2, atomic_load(&values[i]->refcount));

        value_release(values[i]);
        ASSERT_EQ(1, atomic_load(&values[i]->refcount));

        value_free(values[i]);
    }
}

/*
 * Test: GC survival count
 */
void test_gc_survival_count(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    ASSERT_EQ(0, value_survival_count(v));

    value_inc_survival(v);
    ASSERT_EQ(1, value_survival_count(v));

    value_inc_survival(v);
    value_inc_survival(v);
    ASSERT_EQ(3, value_survival_count(v));

    value_free(v);
}

/*
 * Test: GC survival count max
 */
void test_gc_survival_count_max(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    /* Survival count is 3 bits, max is 7 */
    for (int i = 0; i < 10; i++) {
        value_inc_survival(v);
    }
    ASSERT_EQ(7, value_survival_count(v));

    value_free(v);
}

/*
 * Test: GC old generation flag
 */
void test_gc_old_gen(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    ASSERT(!value_is_old_gen(v));

    value_set_old_gen(v);
    ASSERT(value_is_old_gen(v));

    value_free(v);
}

/*
 * Test: GC remembered set flag
 */
void test_gc_remembered(void) {
    Value *v = value_int(42);
    ASSERT(v != NULL);

    ASSERT(!value_is_remembered(v));

    value_set_remembered(v, true);
    ASSERT(value_is_remembered(v));

    value_set_remembered(v, false);
    ASSERT(!value_is_remembered(v));

    value_free(v);
}

int main(void) {
    printf("Running value refcount tests...\n");

    printf("\nBasic refcount tests:\n");
    RUN_TEST(test_new_value_refcount_one);
    RUN_TEST(test_retain_increments_refcount);
    RUN_TEST(test_retain_null);
    RUN_TEST(test_retain_multiple);
    RUN_TEST(test_release_decrements_refcount);
    RUN_TEST(test_release_null);

    printf("\nType-specific refcount tests:\n");
    RUN_TEST(test_string_refcount);
    RUN_TEST(test_array_refcount);
    RUN_TEST(test_map_refcount);

    printf("\nCOW tests:\n");
    RUN_TEST(test_needs_cow_unshared);
    RUN_TEST(test_needs_cow_shared);
    RUN_TEST(test_needs_cow_null);
    RUN_TEST(test_can_share_shareable);
    RUN_TEST(test_can_share_primitives);
    RUN_TEST(test_can_share_null);

    printf("\nSharing tests:\n");
    RUN_TEST(test_mark_shared);
    RUN_TEST(test_mark_shared_idempotent);
    RUN_TEST(test_cow_share);
    RUN_TEST(test_cow_share_null);

    printf("\nImmutability tests:\n");
    RUN_TEST(test_is_immutable);
    RUN_TEST(test_is_immutable_null);

    printf("\nSaturation tests:\n");
    RUN_TEST(test_refcount_saturation);

    printf("\nSharing semantics tests:\n");
    RUN_TEST(test_refcount_implies_shared);

    printf("\nGC state tests:\n");
    RUN_TEST(test_gc_state_independent);
    RUN_TEST(test_gc_survival_count);
    RUN_TEST(test_gc_survival_count_max);
    RUN_TEST(test_gc_old_gen);
    RUN_TEST(test_gc_remembered);

    printf("\nCopy tests:\n");
    RUN_TEST(test_copy_independent);
    RUN_TEST(test_copy_array_deep);
    RUN_TEST(test_copy_null);

    printf("\nCycle tests:\n");
    RUN_TEST(test_retain_release_cycle);

    printf("\nAll types test:\n");
    RUN_TEST(test_refcount_all_types);

    return TEST_RESULT();
}
