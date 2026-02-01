/*
 * Agim - Value Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/value.h"
#include "types/string.h"

void test_nil(void) {
    Value *v = value_nil();
    ASSERT(v != NULL);
    ASSERT(value_is_nil(v));
    ASSERT(!value_is_truthy(v));
    value_free(v);
}

void test_bool(void) {
    Value *t = value_bool(true);
    Value *f = value_bool(false);

    ASSERT(value_is_bool(t));
    ASSERT(value_is_bool(f));
    ASSERT(value_is_truthy(t));
    ASSERT(!value_is_truthy(f));

    value_free(t);
    value_free(f);
}

void test_int(void) {
    Value *v = value_int(42);
    ASSERT(value_is_int(v));
    ASSERT_EQ(42, v->as.integer);
    ASSERT(value_is_truthy(v));

    Value *zero = value_int(0);
    ASSERT(!value_is_truthy(zero));

    value_free(v);
    value_free(zero);
}

void test_float(void) {
    Value *v = value_float(3.14);
    ASSERT(value_is_float(v));
    ASSERT(v->as.floating > 3.13 && v->as.floating < 3.15);

    value_free(v);
}

void test_string(void) {
    Value *v = value_string("hello");
    ASSERT(value_is_string(v));
    ASSERT_STR_EQ("hello", v->as.string->data);
    ASSERT_EQ(5, string_length(v));

    value_free(v);
}

void test_string_concat(void) {
    Value *a = value_string("hello");
    Value *b = value_string(" world");
    Value *c = string_concat(a, b);

    ASSERT_STR_EQ("hello world", c->as.string->data);

    value_free(a);
    value_free(b);
    value_free(c);
}

void test_array(void) {
    Value *arr = value_array();
    ASSERT(value_is_array(arr));
    ASSERT_EQ(0, array_length(arr));

    arr = array_push(arr, value_int(1));
    arr = array_push(arr, value_int(2));
    arr = array_push(arr, value_int(3));

    ASSERT_EQ(3, array_length(arr));
    ASSERT_EQ(1, array_get(arr, 0)->as.integer);
    ASSERT_EQ(2, array_get(arr, 1)->as.integer);
    ASSERT_EQ(3, array_get(arr, 2)->as.integer);

    Value *new_arr;
    Value *popped = array_pop(arr, &new_arr);
    ASSERT(popped != NULL);
    arr = new_arr;
    ASSERT_EQ(3, popped->as.integer);
    ASSERT_EQ(2, array_length(arr));

    value_free(popped);
    value_free(arr);
}

void test_map(void) {
    Value *m = value_map();
    ASSERT(value_is_map(m));
    ASSERT_EQ(0, map_size(m));

    map_set(m, "foo", value_int(42));
    map_set(m, "bar", value_string("hello"));

    ASSERT_EQ(2, map_size(m));
    ASSERT(map_has(m, "foo"));
    ASSERT(map_has(m, "bar"));
    ASSERT(!map_has(m, "baz"));

    Value *foo = map_get(m, "foo");
    ASSERT(foo != NULL);
    ASSERT_EQ(42, foo->as.integer);

    value_free(m);
}

void test_equality(void) {
    Value *a = value_int(42);
    Value *b = value_int(42);
    Value *c = value_int(43);

    ASSERT(value_equals(a, b));
    ASSERT(!value_equals(a, c));

    value_free(a);
    value_free(b);
    value_free(c);

    Value *s1 = value_string("test");
    Value *s2 = value_string("test");
    Value *s3 = value_string("other");

    ASSERT(value_equals(s1, s2));
    ASSERT(!value_equals(s1, s3));

    value_free(s1);
    value_free(s2);
    value_free(s3);
}

void test_copy(void) {
    Value *orig = value_string("original");
    Value *copy = value_copy(orig);

    ASSERT(value_equals(orig, copy));
    ASSERT(orig != copy);
    ASSERT(orig->as.string != copy->as.string);

    value_free(orig);
    value_free(copy);
}

/* Test string interning cache */
void test_string_intern(void) {
    const char *test_str = "hello_intern";
    size_t len = strlen(test_str);

    /* First intern should create a new string */
    Value *s1 = string_intern(test_str, len);
    ASSERT(s1 != NULL);
    ASSERT(value_is_string(s1));
    ASSERT_STR_EQ(test_str, s1->as.string->data);

    /* Second intern of same string should hit cache */
    Value *s2 = string_intern(test_str, len);
    ASSERT(s2 != NULL);
    ASSERT(value_is_string(s2));
    ASSERT_STR_EQ(test_str, s2->as.string->data);

    /* Different string should not collide (usually) */
    const char *other_str = "different_string";
    Value *s3 = string_intern(other_str, strlen(other_str));
    ASSERT(s3 != NULL);
    ASSERT_STR_EQ(other_str, s3->as.string->data);

    value_free(s1);
    value_free(s2);
    value_free(s3);
}

int main(void) {
    RUN_TEST(test_nil);
    RUN_TEST(test_bool);
    RUN_TEST(test_int);
    RUN_TEST(test_float);
    RUN_TEST(test_string);
    RUN_TEST(test_string_concat);
    RUN_TEST(test_array);
    RUN_TEST(test_map);
    RUN_TEST(test_equality);
    RUN_TEST(test_copy);
    RUN_TEST(test_string_intern);

    return TEST_RESULT();
}
