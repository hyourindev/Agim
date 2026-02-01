/*
 * Agim - Map Operations Tests
 *
 * Comprehensive tests for map type operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "types/map.h"
#include "vm/value.h"
#include <string.h>

/* Map Creation Tests */

void test_map_new_empty(void) {
    Value *m = value_map();

    ASSERT(m != NULL);
    ASSERT_EQ(VAL_MAP, m->type);
    ASSERT_EQ(0, map_size(m));

    value_free(m);
}

void test_map_with_capacity(void) {
    Value *m = value_map_with_capacity(100);

    ASSERT(m != NULL);
    ASSERT_EQ(0, map_size(m));
    ASSERT(map_capacity(m) >= 100);

    value_free(m);
}

/* Map Set Tests */

void test_map_set_new_key(void) {
    Value *m = value_map();

    Value *result = map_set(m, "key1", value_int(42));

    ASSERT(result != NULL);
    ASSERT_EQ(1, map_size(result));

    value_free(result);
}

void test_map_set_multiple_keys(void) {
    Value *m = value_map();

    m = map_set(m, "a", value_int(1));
    m = map_set(m, "b", value_int(2));
    m = map_set(m, "c", value_int(3));

    ASSERT_EQ(3, map_size(m));

    value_free(m);
}

void test_map_set_overwrite_key(void) {
    Value *m = value_map();

    m = map_set(m, "key", value_int(1));
    ASSERT_EQ(1, map_get(m, "key")->as.integer);

    m = map_set(m, "key", value_int(999));
    ASSERT_EQ(1, map_size(m));
    ASSERT_EQ(999, map_get(m, "key")->as.integer);

    value_free(m);
}

/* Map Get Tests */

void test_map_get_existing_key(void) {
    Value *m = value_map();
    m = map_set(m, "name", value_string("Alice"));

    Value *v = map_get(m, "name");

    ASSERT(v != NULL);
    ASSERT_EQ(VAL_STRING, v->type);
    ASSERT_STR_EQ("Alice", v->as.string->data);

    value_free(m);
}

void test_map_get_missing_key(void) {
    Value *m = value_map();
    m = map_set(m, "exists", value_int(1));

    Value *v = map_get(m, "missing");

    ASSERT(v == NULL);

    value_free(m);
}

void test_map_get_empty_map(void) {
    Value *m = value_map();

    Value *v = map_get(m, "anything");

    ASSERT(v == NULL);

    value_free(m);
}

/* Map Has Tests */

void test_map_has_existing(void) {
    Value *m = value_map();
    m = map_set(m, "key", value_int(1));

    ASSERT(map_has(m, "key"));

    value_free(m);
}

void test_map_has_missing(void) {
    Value *m = value_map();
    m = map_set(m, "key", value_int(1));

    ASSERT(!map_has(m, "other"));

    value_free(m);
}

void test_map_has_empty(void) {
    Value *m = value_map();

    ASSERT(!map_has(m, "anything"));

    value_free(m);
}

/* Map Delete Tests */

void test_map_delete_existing(void) {
    Value *m = value_map();
    m = map_set(m, "a", value_int(1));
    m = map_set(m, "b", value_int(2));
    m = map_set(m, "c", value_int(3));

    Value *result = map_delete(m, "b");

    ASSERT(result != NULL);
    ASSERT_EQ(2, map_size(result));
    ASSERT(!map_has(result, "b"));
    ASSERT(map_has(result, "a"));
    ASSERT(map_has(result, "c"));

    value_free(result);
}

void test_map_delete_missing(void) {
    Value *m = value_map();
    m = map_set(m, "key", value_int(1));

    Value *result = map_delete(m, "nonexistent");

    ASSERT(result != NULL);
    ASSERT_EQ(1, map_size(result));

    value_free(result);
}

void test_map_delete_only_key(void) {
    Value *m = value_map();
    m = map_set(m, "solo", value_int(42));

    Value *result = map_delete(m, "solo");

    ASSERT(result != NULL);
    ASSERT_EQ(0, map_size(result));

    value_free(result);
}

/* Map Size Tests */

void test_map_size_empty(void) {
    Value *m = value_map();

    ASSERT_EQ(0, map_size(m));

    value_free(m);
}

void test_map_size_after_operations(void) {
    Value *m = value_map();

    ASSERT_EQ(0, map_size(m));

    m = map_set(m, "a", value_int(1));
    ASSERT_EQ(1, map_size(m));

    m = map_set(m, "b", value_int(2));
    ASSERT_EQ(2, map_size(m));

    m = map_set(m, "a", value_int(10)); /* Overwrite */
    ASSERT_EQ(2, map_size(m));

    m = map_delete(m, "a");
    ASSERT_EQ(1, map_size(m));

    value_free(m);
}

/* Map Clear Tests */

void test_map_clear(void) {
    Value *m = value_map();
    m = map_set(m, "a", value_int(1));
    m = map_set(m, "b", value_int(2));
    m = map_set(m, "c", value_int(3));

    Value *cleared = map_clear(m);

    ASSERT(cleared != NULL);
    ASSERT_EQ(0, map_size(cleared));

    value_free(cleared);
}

/* Map Growth Tests */

void test_map_growth(void) {
    Value *m = value_map_with_capacity(4);
    size_t initial_cap = map_capacity(m);

    /* Add many keys to trigger growth */
    char key[32];
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        m = map_set(m, key, value_int(i));
    }

    ASSERT_EQ(100, map_size(m));
    ASSERT(map_capacity(m) > initial_cap);

    /* Verify all keys are accessible */
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        Value *v = map_get(m, key);
        ASSERT(v != NULL);
        ASSERT_EQ(i, v->as.integer);
    }

    value_free(m);
}

/* Map Iteration Tests */

void test_map_keys(void) {
    Value *m = value_map();
    m = map_set(m, "x", value_int(1));
    m = map_set(m, "y", value_int(2));
    m = map_set(m, "z", value_int(3));

    Value *keys = map_keys(m);

    ASSERT(keys != NULL);
    ASSERT_EQ(VAL_ARRAY, keys->type);
    ASSERT_EQ(3, keys->as.array->length);

    value_free(m);
    value_free(keys);
}

void test_map_values(void) {
    Value *m = value_map();
    m = map_set(m, "a", value_int(10));
    m = map_set(m, "b", value_int(20));

    Value *values = map_values(m);

    ASSERT(values != NULL);
    ASSERT_EQ(VAL_ARRAY, values->type);
    ASSERT_EQ(2, values->as.array->length);

    /* Sum of values should be 30 */
    int sum = 0;
    for (size_t i = 0; i < values->as.array->length; i++) {
        sum += values->as.array->items[i]->as.integer;
    }
    ASSERT_EQ(30, sum);

    value_free(m);
    value_free(values);
}

void test_map_entries(void) {
    Value *m = value_map();
    m = map_set(m, "key", value_int(42));

    Value *entries = map_entries(m);

    ASSERT(entries != NULL);
    ASSERT_EQ(VAL_ARRAY, entries->type);
    ASSERT_EQ(1, entries->as.array->length);

    value_free(m);
    value_free(entries);
}

void test_map_iteration_empty(void) {
    Value *m = value_map();

    Value *keys = map_keys(m);
    Value *values = map_values(m);
    Value *entries = map_entries(m);

    ASSERT(keys != NULL);
    ASSERT(values != NULL);
    ASSERT(entries != NULL);
    ASSERT_EQ(0, keys->as.array->length);
    ASSERT_EQ(0, values->as.array->length);
    ASSERT_EQ(0, entries->as.array->length);

    value_free(m);
    value_free(keys);
    value_free(values);
    value_free(entries);
}

/* Hash Collision Tests */

void test_map_hash_collision_handling(void) {
    Value *m = value_map_with_capacity(2); /* Small capacity to force collisions */

    /* Add many keys - some will collide */
    for (int i = 0; i < 20; i++) {
        char key[32];
        snprintf(key, sizeof(key), "k%d", i);
        m = map_set(m, key, value_int(i * 10));
    }

    ASSERT_EQ(20, map_size(m));

    /* Verify all keys are retrievable */
    for (int i = 0; i < 20; i++) {
        char key[32];
        snprintf(key, sizeof(key), "k%d", i);
        Value *v = map_get(m, key);
        ASSERT(v != NULL);
        ASSERT_EQ(i * 10, v->as.integer);
    }

    value_free(m);
}

/* Value Types Tests */

void test_map_with_various_value_types(void) {
    Value *m = value_map();

    m = map_set(m, "int", value_int(42));
    m = map_set(m, "float", value_float(3.14));
    m = map_set(m, "bool", value_bool(true));
    m = map_set(m, "nil", value_nil());
    m = map_set(m, "string", value_string("hello"));
    m = map_set(m, "array", value_array());

    ASSERT_EQ(6, map_size(m));
    ASSERT_EQ(42, map_get(m, "int")->as.integer);
    ASSERT(map_get(m, "float")->as.floating > 3.13);
    ASSERT(map_get(m, "bool")->as.boolean);
    ASSERT(value_is_nil(map_get(m, "nil")));
    ASSERT_STR_EQ("hello", map_get(m, "string")->as.string->data);
    ASSERT_EQ(VAL_ARRAY, map_get(m, "array")->type);

    value_free(m);
}

/* Null Input Tests */

void test_map_null_inputs(void) {
    /* Functions should handle NULL gracefully without crashing */
    ASSERT_EQ(0, map_size(NULL));
    ASSERT(map_get(NULL, "key") == NULL);
    ASSERT(!map_has(NULL, "key"));

    Value *unused_val = value_int(1);
    ASSERT(map_set(NULL, "key", unused_val) == NULL);
    value_free(unused_val);

    ASSERT(map_delete(NULL, "key") == NULL);

    /* map_keys returns empty array when NULL - defensive behavior */
    Value *keys = map_keys(NULL);
    ASSERT(keys != NULL);
    ASSERT_EQ(VAL_ARRAY, keys->type);
    ASSERT_EQ(0, array_length(keys));
    value_free(keys);

    /* map_values returns empty array when NULL - defensive behavior */
    Value *values = map_values(NULL);
    ASSERT(values != NULL);
    ASSERT_EQ(VAL_ARRAY, values->type);
    ASSERT_EQ(0, array_length(values));
    value_free(values);

    /* map_entries returns empty array when NULL - defensive behavior */
    Value *entries = map_entries(NULL);
    ASSERT(entries != NULL);
    ASSERT_EQ(VAL_ARRAY, entries->type);
    ASSERT_EQ(0, array_length(entries));
    value_free(entries);
}

/* Edge Cases */

void test_map_empty_string_key(void) {
    Value *m = value_map();
    m = map_set(m, "", value_int(100));

    ASSERT_EQ(1, map_size(m));
    ASSERT(map_has(m, ""));
    ASSERT_EQ(100, map_get(m, "")->as.integer);

    value_free(m);
}

void test_map_long_key(void) {
    Value *m = value_map();
    char long_key[1024];
    memset(long_key, 'a', sizeof(long_key) - 1);
    long_key[sizeof(long_key) - 1] = '\0';

    m = map_set(m, long_key, value_int(999));

    ASSERT_EQ(1, map_size(m));
    ASSERT(map_has(m, long_key));
    ASSERT_EQ(999, map_get(m, long_key)->as.integer);

    value_free(m);
}

void test_map_special_chars_key(void) {
    Value *m = value_map();

    m = map_set(m, "hello\nworld", value_int(1));
    m = map_set(m, "tab\there", value_int(2));
    m = map_set(m, "unicode: \xc3\xa9", value_int(3));

    ASSERT_EQ(3, map_size(m));
    ASSERT_EQ(1, map_get(m, "hello\nworld")->as.integer);
    ASSERT_EQ(2, map_get(m, "tab\there")->as.integer);
    ASSERT_EQ(3, map_get(m, "unicode: \xc3\xa9")->as.integer);

    value_free(m);
}

/* Main */

int main(void) {
    printf("Running map operations tests...\n\n");

    printf("Map Creation Tests:\n");
    RUN_TEST(test_map_new_empty);
    RUN_TEST(test_map_with_capacity);

    printf("\nMap Set Tests:\n");
    RUN_TEST(test_map_set_new_key);
    RUN_TEST(test_map_set_multiple_keys);
    RUN_TEST(test_map_set_overwrite_key);

    printf("\nMap Get Tests:\n");
    RUN_TEST(test_map_get_existing_key);
    RUN_TEST(test_map_get_missing_key);
    RUN_TEST(test_map_get_empty_map);

    printf("\nMap Has Tests:\n");
    RUN_TEST(test_map_has_existing);
    RUN_TEST(test_map_has_missing);
    RUN_TEST(test_map_has_empty);

    printf("\nMap Delete Tests:\n");
    RUN_TEST(test_map_delete_existing);
    RUN_TEST(test_map_delete_missing);
    RUN_TEST(test_map_delete_only_key);

    printf("\nMap Size Tests:\n");
    RUN_TEST(test_map_size_empty);
    RUN_TEST(test_map_size_after_operations);

    printf("\nMap Clear Tests:\n");
    RUN_TEST(test_map_clear);

    printf("\nMap Growth Tests:\n");
    RUN_TEST(test_map_growth);

    printf("\nMap Iteration Tests:\n");
    RUN_TEST(test_map_keys);
    RUN_TEST(test_map_values);
    RUN_TEST(test_map_entries);
    RUN_TEST(test_map_iteration_empty);

    printf("\nHash Collision Tests:\n");
    RUN_TEST(test_map_hash_collision_handling);

    printf("\nValue Types Tests:\n");
    RUN_TEST(test_map_with_various_value_types);

    printf("\nNull Input Tests:\n");
    RUN_TEST(test_map_null_inputs);

    printf("\nEdge Cases:\n");
    RUN_TEST(test_map_empty_string_key);
    RUN_TEST(test_map_long_key);
    RUN_TEST(test_map_special_chars_key);

    return TEST_RESULT();
}
