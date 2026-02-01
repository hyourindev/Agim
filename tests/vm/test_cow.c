/*
 * Agim - Copy-on-Write Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/value.h"
#include "types/array.h"
#include "types/map.h"
#include "types/string.h"

/* Reference Counting Tests */

void test_refcount_initial(void) {
    /* All values start with refcount 1 */
    Value *i = value_int(42);
    Value *s = value_string("hello");
    Value *arr = value_array();
    Value *m = value_map();

    ASSERT_EQ(1, i->refcount);
    ASSERT_EQ(1, s->refcount);
    ASSERT_EQ(1, arr->refcount);
    ASSERT_EQ(1, m->refcount);

    value_free(i);
    value_free(s);
    value_free(arr);
    value_free(m);
}

void test_refcount_retain_release(void) {
    Value *v = value_int(42);
    ASSERT_EQ(1, v->refcount);

    value_retain(v);
    ASSERT_EQ(2, v->refcount);

    value_retain(v);
    ASSERT_EQ(3, v->refcount);

    value_release(v);
    ASSERT_EQ(2, v->refcount);

    value_release(v);
    ASSERT_EQ(1, v->refcount);

    value_free(v);
}

void test_needs_cow(void) {
    Value *v = value_int(42);

    /* refcount 1 - no COW needed */
    ASSERT(!value_needs_cow(v));

    value_retain(v);
    /* refcount 2 - COW needed */
    ASSERT(value_needs_cow(v));

    value_release(v);
    /* refcount 1 again - no COW needed */
    ASSERT(!value_needs_cow(v));

    value_free(v);
}

/* Array COW Tests */

void test_array_cow_on_push(void) {
    /* Create an array with some values */
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));

    /* Save pointer to original value for comparison */
    Value *original_value = arr;

    /* Simulate sharing (bump refcount) */
    value_retain(arr);
    ASSERT_EQ(2, atomic_load(&arr->refcount));

    /* Push should trigger COW - returns a NEW Value */
    arr = array_push(arr, value_int(3));

    /* After COW, arr is a new Value with refcount 1 */
    ASSERT_EQ(1, atomic_load(&arr->refcount));
    /* The new Value is different from the original */
    ASSERT(arr != original_value);

    /* Verify data is correct */
    ASSERT_EQ(3, array_length(arr));
    ASSERT_EQ(1, array_get(arr, 0)->as.integer);
    ASSERT_EQ(2, array_get(arr, 1)->as.integer);
    ASSERT_EQ(3, array_get(arr, 2)->as.integer);

    value_free(arr);
    value_free(original_value);
}

void test_array_cow_on_set(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));

    /* Save original for comparison */
    Value *original_value = arr;

    /* Simulate sharing */
    value_retain(arr);

    /* Set should trigger COW - returns a NEW Value */
    arr = array_set(arr, 0, value_int(100));
    ASSERT(arr != NULL);
    ASSERT_EQ(1, atomic_load(&arr->refcount));
    ASSERT(arr != original_value);

    /* Verify mutation happened */
    ASSERT_EQ(100, array_get(arr, 0)->as.integer);
    ASSERT_EQ(2, array_get(arr, 1)->as.integer);

    value_free(arr);
    value_free(original_value);
}

void test_array_cow_on_pop(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));

    Value *original_value = arr;
    value_retain(arr);

    /* Pop should trigger COW - returns popped value and updates arr */
    Value *new_arr;
    Value *popped = array_pop(arr, &new_arr);
    arr = new_arr;

    ASSERT_EQ(1, atomic_load(&arr->refcount));
    ASSERT(arr != original_value);
    ASSERT_EQ(2, popped->as.integer);
    ASSERT_EQ(1, array_length(arr));

    value_free(arr);
    value_free(original_value);
}

void test_array_no_cow_when_unique(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_int(1));

    /* Not shared - refcount is 1 */
    ASSERT_EQ(1, atomic_load(&arr->refcount));
    Value *original_value = arr;

    /* Push should NOT trigger COW since refcount is 1 */
    arr = array_push(arr, value_int(2));

    /* Same Value returned (no COW triggered) */
    ASSERT(arr == original_value);

    value_free(arr);
}

/* Map COW Tests */

void test_map_cow_on_set(void) {
    Value *m = value_map();
    m = map_set(m, "foo", value_int(1));
    m = map_set(m, "bar", value_int(2));

    /* Save original for comparison */
    Value *original_value = m;

    /* Simulate sharing */
    value_retain(m);
    ASSERT_EQ(2, atomic_load(&m->refcount));

    /* Set should trigger COW - returns NEW Value */
    m = map_set(m, "baz", value_int(3));

    ASSERT_EQ(1, atomic_load(&m->refcount));
    ASSERT(m != original_value);

    /* Verify data is correct */
    ASSERT_EQ(3, map_size(m));
    ASSERT_EQ(1, map_get(m, "foo")->as.integer);
    ASSERT_EQ(2, map_get(m, "bar")->as.integer);
    ASSERT_EQ(3, map_get(m, "baz")->as.integer);

    value_free(m);
    value_free(original_value);
}

void test_map_cow_on_delete(void) {
    Value *m = value_map();
    m = map_set(m, "foo", value_int(1));
    m = map_set(m, "bar", value_int(2));

    Value *original_value = m;
    value_retain(m);

    /* Delete should trigger COW - returns NEW Value */
    m = map_delete(m, "foo");
    ASSERT(m != NULL);
    ASSERT_EQ(1, atomic_load(&m->refcount));
    ASSERT(m != original_value);

    /* Verify deletion */
    ASSERT_EQ(1, map_size(m));
    ASSERT(!map_has(m, "foo"));
    ASSERT(map_has(m, "bar"));

    value_free(m);
    value_free(original_value);
}

void test_map_cow_on_clear(void) {
    Value *m = value_map();
    m = map_set(m, "foo", value_int(1));

    Value *original_value = m;
    value_retain(m);

    /* Clear should trigger COW - returns NEW Value */
    m = map_clear(m);
    ASSERT(m != NULL);
    ASSERT_EQ(1, atomic_load(&m->refcount));
    ASSERT(m != original_value);
    ASSERT_EQ(0, map_size(m));

    value_free(m);
    value_free(original_value);
}

void test_map_no_cow_when_unique(void) {
    Value *m = value_map();
    m = map_set(m, "foo", value_int(1));

    /* Not shared */
    ASSERT_EQ(1, atomic_load(&m->refcount));
    Value *original_value = m;

    /* Set should NOT trigger COW */
    m = map_set(m, "bar", value_int(2));

    /* Same Value returned (no COW triggered) */
    ASSERT(m == original_value);

    value_free(m);
}

/* Immutability Tests */

void test_string_immutable_flag(void) {
    Value *s = value_string("hello");

    /* Strings should have IMMUTABLE flag */
    ASSERT(s->flags & VALUE_IMMUTABLE);

    value_free(s);
}

void test_can_share(void) {
    Value *i = value_int(42);
    Value *s = value_string("hello");
    Value *arr = value_array();
    Value *m = value_map();

    /* All basic types can be shared */
    ASSERT(value_can_share(i));
    ASSERT(value_can_share(s));
    ASSERT(value_can_share(arr));
    ASSERT(value_can_share(m));

    value_free(i);
    value_free(s);
    value_free(arr);
    value_free(m);
}

void test_mark_shared(void) {
    Value *arr = value_array();

    /* Not shared initially */
    ASSERT(!(arr->flags & VALUE_COW_SHARED));

    value_mark_shared(arr);

    /* Now marked as shared */
    ASSERT(arr->flags & VALUE_COW_SHARED);

    value_free(arr);
}

/* Main */

int main(void) {
    /* Reference counting tests */
    RUN_TEST(test_refcount_initial);
    RUN_TEST(test_refcount_retain_release);
    RUN_TEST(test_needs_cow);

    /* Array COW tests */
    RUN_TEST(test_array_cow_on_push);
    RUN_TEST(test_array_cow_on_set);
    RUN_TEST(test_array_cow_on_pop);
    RUN_TEST(test_array_no_cow_when_unique);

    /* Map COW tests */
    RUN_TEST(test_map_cow_on_set);
    RUN_TEST(test_map_cow_on_delete);
    RUN_TEST(test_map_cow_on_clear);
    RUN_TEST(test_map_no_cow_when_unique);

    /* Immutability tests */
    RUN_TEST(test_string_immutable_flag);
    RUN_TEST(test_can_share);
    RUN_TEST(test_mark_shared);

    return TEST_RESULT();
}
