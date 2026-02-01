/*
 * Agim - String Property Tests
 *
 * Property-based tests for string operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "property_test.h"
#include "types/string.h"
#include "vm/value.h"

#include <string.h>

/* Property: concat length == len(a) + len(b) */
static bool prop_string_concat_length(void *ctx) {
    (void)ctx;

    char *str_a = prop_rand_string(50);
    char *str_b = prop_rand_string(50);
    PROP_ASSERT(str_a != NULL && str_b != NULL);

    size_t len_a = strlen(str_a);
    size_t len_b = strlen(str_b);

    Value *a = value_string(str_a);
    Value *b = value_string(str_b);
    PROP_ASSERT(a != NULL && b != NULL);

    Value *result = string_concat(a, b);
    PROP_ASSERT(result != NULL);

    size_t expected_len = len_a + len_b;
    size_t actual_len = string_length(result);
    PROP_ASSERT(actual_len == expected_len);

    value_free(result);
    value_free(a);
    value_free(b);
    free(str_a);
    free(str_b);

    return true;
}

/* Property: slice(0, len) == original */
static bool prop_string_slice_identity(void *ctx) {
    (void)ctx;

    char *str = prop_rand_string(50);
    PROP_ASSERT(str != NULL);

    size_t len = strlen(str);
    Value *original = value_string(str);
    PROP_ASSERT(original != NULL);

    Value *sliced = string_slice(original, 0, len);
    PROP_ASSERT(sliced != NULL);

    const char *orig_str = value_to_string(original);
    const char *slice_str = value_to_string(sliced);
    PROP_ASSERT(strcmp(orig_str, slice_str) == 0);

    value_free(sliced);
    value_free(original);
    free(str);

    return true;
}

/* Property: intern(s) == intern(s) content matches for same string */
static bool prop_string_intern_content_matches(void *ctx) {
    (void)ctx;

    char *str = prop_rand_alnum_string(30);
    PROP_ASSERT(str != NULL);

    Value *s1 = value_string(str);
    Value *s2 = value_string(str);
    PROP_ASSERT(s1 != NULL && s2 != NULL);

    const char *ptr1 = value_to_string(s1);
    const char *ptr2 = value_to_string(s2);

    /* Content should always match */
    PROP_ASSERT(strcmp(ptr1, ptr2) == 0);

    value_free(s1);
    value_free(s2);
    free(str);

    return true;
}

/* Property: length(s) matches strlen */
static bool prop_string_length_matches_strlen(void *ctx) {
    (void)ctx;

    char *str = prop_rand_string(100);
    PROP_ASSERT(str != NULL);

    size_t expected = strlen(str);
    Value *s = value_string(str);
    PROP_ASSERT(s != NULL);

    size_t actual = string_length(s);
    PROP_ASSERT(actual == expected);

    value_free(s);
    free(str);

    return true;
}

/* Property: slice(i, j) length == j - i for valid indices */
static bool prop_string_slice_length(void *ctx) {
    (void)ctx;

    char *str = prop_rand_string(50);
    PROP_ASSERT(str != NULL);

    size_t len = strlen(str);
    if (len == 0) {
        free(str);
        return true; /* Skip empty strings */
    }

    Value *s = value_string(str);
    PROP_ASSERT(s != NULL);

    size_t start = prop_rand_size(len - 1);
    size_t end = start + prop_rand_size(len - start);
    if (end > len) end = len;

    Value *sliced = string_slice(s, start, end);
    PROP_ASSERT(sliced != NULL);

    size_t expected = end - start;
    size_t actual = string_length(sliced);
    PROP_ASSERT(actual == expected);

    value_free(sliced);
    value_free(s);
    free(str);

    return true;
}

/* Property: upper preserves length */
static bool prop_string_upper_preserves_length(void *ctx) {
    (void)ctx;

    char *str = prop_rand_string(50);
    PROP_ASSERT(str != NULL);

    size_t expected = strlen(str);
    Value *s = value_string(str);
    PROP_ASSERT(s != NULL);

    Value *upper = string_upper(s);
    PROP_ASSERT(upper != NULL);
    PROP_ASSERT(string_length(upper) == expected);

    value_free(upper);
    value_free(s);
    free(str);

    return true;
}

/* Property: lower preserves length */
static bool prop_string_lower_preserves_length(void *ctx) {
    (void)ctx;

    char *str = prop_rand_string(50);
    PROP_ASSERT(str != NULL);

    size_t expected = strlen(str);
    Value *s = value_string(str);
    PROP_ASSERT(s != NULL);

    Value *lower = string_lower(s);
    PROP_ASSERT(lower != NULL);
    PROP_ASSERT(string_length(lower) == expected);

    value_free(lower);
    value_free(s);
    free(str);

    return true;
}

/* Property: find returns valid index when needle is substring */
static bool prop_string_find_returns_valid(void *ctx) {
    (void)ctx;

    /* Create a string with at least some content */
    char *str = prop_rand_alnum_string(50);
    PROP_ASSERT(str != NULL);

    size_t len = strlen(str);
    if (len < 3) {
        free(str);
        return true; /* Skip very short strings */
    }

    /* Take a substring as needle */
    size_t needle_start = prop_rand_size(len / 2);
    size_t needle_len = 1 + prop_rand_size((len - needle_start) / 2);
    if (needle_len > 10) needle_len = 10;

    char *needle = malloc(needle_len + 1);
    PROP_ASSERT(needle != NULL);
    memcpy(needle, str + needle_start, needle_len);
    needle[needle_len] = '\0';

    Value *haystack = value_string(str);
    PROP_ASSERT(haystack != NULL);

    int64_t idx = string_find(haystack, needle);
    /* Should find it since needle is a substring */
    PROP_ASSERT(idx >= 0);
    PROP_ASSERT((size_t)idx <= len - needle_len);

    value_free(haystack);
    free(str);
    free(needle);

    return true;
}

/* Property: empty string concat is identity */
static bool prop_string_concat_empty_identity(void *ctx) {
    (void)ctx;

    char *str = prop_rand_string(50);
    PROP_ASSERT(str != NULL);

    Value *s = value_string(str);
    Value *empty = value_string("");
    PROP_ASSERT(s != NULL && empty != NULL);

    Value *result1 = string_concat(s, empty);
    Value *result2 = string_concat(empty, s);
    PROP_ASSERT(result1 != NULL && result2 != NULL);

    /* s + "" == s */
    PROP_ASSERT(strcmp(value_to_string(result1), str) == 0);
    /* "" + s == s */
    PROP_ASSERT(strcmp(value_to_string(result2), str) == 0);

    value_free(result1);
    value_free(result2);
    value_free(s);
    value_free(empty);
    free(str);

    return true;
}

/* Main */
int main(void) {
    printf("Running string property tests...\n\n");

    prop_init(12345); /* Fixed seed for reproducibility */

    PROP_CHECK("concat length == len(a) + len(b)",
               prop_string_concat_length, NULL, 500);

    PROP_CHECK("slice(0, len) == original",
               prop_string_slice_identity, NULL, 500);

    PROP_CHECK("intern(s) content matches",
               prop_string_intern_content_matches, NULL, 500);

    PROP_CHECK("length matches strlen",
               prop_string_length_matches_strlen, NULL, 500);

    PROP_CHECK("slice(i, j) length == j - i",
               prop_string_slice_length, NULL, 500);

    PROP_CHECK("upper preserves length",
               prop_string_upper_preserves_length, NULL, 500);

    PROP_CHECK("lower preserves length",
               prop_string_lower_preserves_length, NULL, 500);

    PROP_CHECK("find returns valid index for substring",
               prop_string_find_returns_valid, NULL, 300);

    PROP_CHECK("empty string concat is identity",
               prop_string_concat_empty_identity, NULL, 500);

    PROP_SUMMARY();
    return PROP_RESULT();
}
