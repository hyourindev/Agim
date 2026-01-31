/*
 * Agim - Inline Cache Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/ic.h"
#include "vm/value.h"
#include "types/map.h"
#include "util/hash.h"

/* Basic IC Tests */

void test_ic_init(void) {
    InlineCache ic;
    ic_init(&ic);

    ASSERT_EQ(IC_UNINITIALIZED, ic.state);
    ASSERT_EQ(0, ic.count);
}

void test_ic_mono_lookup(void) {
    InlineCache ic;
    ic_init(&ic);

    /* Create a map with some values */
    Value *map = value_map();
    map_set(map, "foo", value_int(42));
    map_set(map, "bar", value_int(100));

    /* First lookup - cache miss */
    Value *result = NULL;
    bool hit = ic_lookup(&ic, map, "foo", &result);
    ASSERT(!hit);  /* Cache miss on first access */

    /* Simulate update after miss */
    Map *m = map->as.map;
    size_t bucket = agim_hash_cstring("foo") % m->capacity;
    ic_update(&ic, map, bucket);

    /* Second lookup - should hit */
    hit = ic_lookup(&ic, map, "foo", &result);
    ASSERT(hit);
    ASSERT(result != NULL);
    ASSERT_EQ(42, result->as.integer);

    /* State should be monomorphic */
    ASSERT_EQ(IC_MONO, ic.state);
    ASSERT_EQ(1, ic.count);

    value_free(map);
}

void test_ic_poly_lookup(void) {
    InlineCache ic;
    ic_init(&ic);

    /* Create two different maps */
    Value *map1 = value_map();
    map_set(map1, "x", value_int(1));

    Value *map2 = value_map();
    map_set(map2, "x", value_int(2));

    /* Update cache with first map */
    Map *m1 = map1->as.map;
    size_t bucket1 = agim_hash_cstring("x") % m1->capacity;
    ic_update(&ic, map1, bucket1);
    ASSERT_EQ(IC_MONO, ic.state);

    /* Update cache with second map */
    Map *m2 = map2->as.map;
    size_t bucket2 = agim_hash_cstring("x") % m2->capacity;
    ic_update(&ic, map2, bucket2);
    ASSERT_EQ(IC_POLY, ic.state);
    ASSERT_EQ(2, ic.count);

    /* Both should hit now */
    Value *result = NULL;
    ASSERT(ic_lookup(&ic, map1, "x", &result));
    ASSERT_EQ(1, result->as.integer);

    ASSERT(ic_lookup(&ic, map2, "x", &result));
    ASSERT_EQ(2, result->as.integer);

    value_free(map1);
    value_free(map2);
}

void test_ic_mega(void) {
    InlineCache ic;
    ic_init(&ic);

    /* Create more maps than IC_MAX_ENTRIES */
    Value *maps[IC_MAX_ENTRIES + 2];
    for (int i = 0; i < IC_MAX_ENTRIES + 2; i++) {
        maps[i] = value_map();
        map_set(maps[i], "key", value_int(i));

        /* Update cache */
        Map *m = maps[i]->as.map;
        size_t bucket = agim_hash_cstring("key") % m->capacity;
        ic_update(&ic, maps[i], bucket);
    }

    /* Should be megamorphic */
    ASSERT_EQ(IC_MEGA, ic.state);

    /* Megamorphic caches always miss */
    Value *result = NULL;
    ASSERT(!ic_lookup(&ic, maps[0], "key", &result));

    for (int i = 0; i < IC_MAX_ENTRIES + 2; i++) {
        value_free(maps[i]);
    }
}

void test_ic_shape_id(void) {
    Value *map1 = value_map();
    Value *map2 = value_map();

    uint64_t id1 = ic_shape_id(map1);
    uint64_t id2 = ic_shape_id(map2);

    /* Different maps should have different shape IDs */
    ASSERT(id1 != id2);

    /* Same map should have consistent shape ID */
    ASSERT_EQ(id1, ic_shape_id(map1));

    value_free(map1);
    value_free(map2);
}

/* Test direct-mapped cache hash behavior */
void test_ic_direct_mapped(void) {
    InlineCache ic;
    ic_init(&ic);

    /* Create a map and update the cache */
    Value *map = value_map();
    map_set(map, "test", value_int(123));

    Map *m = map->as.map;
    size_t bucket = agim_hash_cstring("test") % m->capacity;
    ic_update(&ic, map, bucket);

    /* Verify O(1) lookup works */
    Value *result = NULL;
    bool hit = ic_lookup(&ic, map, "test", &result);
    ASSERT(hit);
    ASSERT(result != NULL);
    ASSERT_EQ(123, result->as.integer);

    /* Verify state is monomorphic after one shape */
    ASSERT_EQ(IC_MONO, ic.state);

    value_free(map);
}

/* Test that IC correctly transitions to mega state */
void test_ic_mega_transition(void) {
    InlineCache ic;
    ic_init(&ic);

    /* Keep all maps alive to ensure unique shape IDs */
    int num_maps = IC_MAX_ENTRIES + 5;
    Value *maps[IC_MAX_ENTRIES + 5];

    /* Create many maps to force megamorphic state */
    for (int i = 0; i < num_maps; i++) {
        maps[i] = value_map();
        map_set(maps[i], "k", value_int(i));

        Map *m = maps[i]->as.map;
        size_t bucket = agim_hash_cstring("k") % m->capacity;
        ic_update(&ic, maps[i], bucket);
    }

    /* Should be megamorphic after exceeding max entries */
    ASSERT_EQ(IC_MEGA, ic.state);

    /* Megamorphic cache should always miss */
    Value *test_map = value_map();
    map_set(test_map, "k", value_int(999));
    Value *result = NULL;
    ASSERT(!ic_lookup(&ic, test_map, "k", &result));

    value_free(test_map);
    for (int i = 0; i < num_maps; i++) {
        value_free(maps[i]);
    }
}

/* Main */

int main(void) {
    RUN_TEST(test_ic_init);
    RUN_TEST(test_ic_mono_lookup);
    RUN_TEST(test_ic_poly_lookup);
    RUN_TEST(test_ic_mega);
    RUN_TEST(test_ic_shape_id);
    RUN_TEST(test_ic_direct_mapped);
    RUN_TEST(test_ic_mega_transition);

    return TEST_RESULT();
}
