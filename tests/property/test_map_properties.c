/*
 * Agim - Map Property Tests
 *
 * Property-based tests for map operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "property_test.h"
#include "types/map.h"
#include "vm/value.h"

/* Property: Map set/get roundtrip */
static bool prop_map_set_get_roundtrip(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    char *key = prop_rand_alnum_string(20);
    PROP_ASSERT(key != NULL);

    int val = prop_rand_int_range(-10000, 10000);
    Value *val_val = value_int(val);

    map = map_set(map, key, val_val);
    PROP_ASSERT(map != NULL);

    Value *retrieved = map_get(map, key);
    PROP_ASSERT(retrieved != NULL);
    PROP_ASSERT(value_is_int(retrieved));
    PROP_ASSERT(value_to_int(retrieved) == val);

    free(key);
    value_free(map);
    return true;
}

/* Property: Map has returns true after set */
static bool prop_map_has_after_set(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    char *key = prop_rand_alnum_string(15);
    PROP_ASSERT(key != NULL);

    PROP_ASSERT(!map_has(map, key)); /* Not there initially */

    Value *val_val = value_int(42);
    map = map_set(map, key, val_val);
    PROP_ASSERT(map != NULL);

    PROP_ASSERT(map_has(map, key)); /* Now it's there */

    free(key);
    value_free(map);
    return true;
}

/* Property: Map delete removes key */
static bool prop_map_delete_removes_key(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    char *key = prop_rand_alnum_string(10);
    PROP_ASSERT(key != NULL);

    Value *val_val = value_int(123);
    map = map_set(map, key, val_val);
    PROP_ASSERT(map != NULL);
    PROP_ASSERT(map_has(map, key));

    map = map_delete(map, key);
    PROP_ASSERT(map != NULL);
    PROP_ASSERT(!map_has(map, key));

    free(key);
    value_free(map);
    return true;
}

/* Property: Map size increases after set with new key */
static bool prop_map_size_increases(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    size_t initial_size = map_size(map);

    /* Add unique keys */
    int count = prop_rand_int_range(1, 10);
    for (int i = 0; i < count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d_%d", i, prop_rand_int());
        Value *val_val = value_int(i);
        map = map_set(map, key, val_val);
        PROP_ASSERT(map != NULL);
    }

    PROP_ASSERT(map_size(map) == initial_size + (size_t)count);

    value_free(map);
    return true;
}

/* Property: Map size decreases after delete */
static bool prop_map_size_decreases_after_delete(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    /* Add a few keys */
    const char *keys[] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++) {
        Value *val_val = value_int(i);
        map = map_set(map, keys[i], val_val);
        PROP_ASSERT(map != NULL);
    }

    size_t size_before = map_size(map);
    PROP_ASSERT(size_before == 3);

    /* Delete one key */
    map = map_delete(map, "b");
    PROP_ASSERT(map != NULL);
    PROP_ASSERT(map_size(map) == size_before - 1);

    value_free(map);
    return true;
}

/* Property: Set with same key overwrites value */
static bool prop_map_set_overwrites(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    const char *key = "testkey";
    Value *val1 = value_int(100);
    Value *val2 = value_int(200);

    map = map_set(map, key, val1);
    PROP_ASSERT(map != NULL);

    map = map_set(map, key, val2);
    PROP_ASSERT(map != NULL);

    /* Size should still be 1 */
    PROP_ASSERT(map_size(map) == 1);

    /* Value should be the new one */
    Value *retrieved = map_get(map, key);
    PROP_ASSERT(retrieved != NULL);
    PROP_ASSERT(value_to_int(retrieved) == 200);

    value_free(map);
    return true;
}

/* Property: Empty map has size 0 */
static bool prop_map_empty_size_zero(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);
    PROP_ASSERT(map_size(map) == 0);

    value_free(map);
    return true;
}

/* Property: Get on missing key returns nil */
static bool prop_map_get_missing_returns_nil(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    char *key = prop_rand_alnum_string(10);
    PROP_ASSERT(key != NULL);

    Value *result = map_get(map, key);

    /* map_get may return NULL for missing keys */
    PROP_ASSERT(result == NULL || value_is_nil(result));

    free(key);
    value_free(map);
    return true;
}

/* Property: Keys are unique */
static bool prop_map_keys_unique(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    /* Add same key multiple times */
    const char *key = "duplicate";
    for (int i = 0; i < 5; i++) {
        Value *val = value_int(i);
        map = map_set(map, key, val);
        PROP_ASSERT(map != NULL);
    }

    /* Should only have one entry */
    PROP_ASSERT(map_size(map) == 1);

    value_free(map);
    return true;
}

/* Property: Map iteration visits all keys */
static bool prop_map_iteration_complete(void *ctx) {
    (void)ctx;

    Value *map = value_map();
    PROP_ASSERT(map != NULL);

    int count = prop_rand_int_range(1, 10);
    for (int i = 0; i < count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%d", i);
        Value *val_val = value_int(i);
        map = map_set(map, key, val_val);
        PROP_ASSERT(map != NULL);
    }

    /* Get keys and verify count matches */
    Value *keys = map_keys(map);
    PROP_ASSERT(keys != NULL);
    PROP_ASSERT(array_length(keys) == (size_t)count);

    value_free(keys);
    value_free(map);
    return true;
}

/* Main */
int main(void) {
    printf("Running map property tests...\n\n");

    prop_init(0); /* Use random seed */

    printf("Map Property Tests:\n");
    PROP_CHECK("set/get roundtrip", prop_map_set_get_roundtrip, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("has after set", prop_map_has_after_set, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("delete removes key", prop_map_delete_removes_key, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("size increases", prop_map_size_increases, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("size decreases after delete", prop_map_size_decreases_after_delete, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("set overwrites", prop_map_set_overwrites, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("empty size zero", prop_map_empty_size_zero, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("get missing returns nil", prop_map_get_missing_returns_nil, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("keys unique", prop_map_keys_unique, NULL, PROP_DEFAULT_ITERATIONS);
    PROP_CHECK("iteration complete", prop_map_iteration_complete, NULL, PROP_DEFAULT_ITERATIONS);

    PROP_SUMMARY();
    return PROP_RESULT();
}
