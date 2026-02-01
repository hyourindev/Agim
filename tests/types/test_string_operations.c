/*
 * Agim - String Operations Tests
 *
 * Comprehensive tests for string type operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "types/string.h"
#include "types/array.h"
#include "vm/value.h"
#include <string.h>

/* String Creation Tests */

void test_string_create_basic(void) {
    Value *s = value_string("hello");

    ASSERT(s != NULL);
    ASSERT_EQ(VAL_STRING, s->type);
    ASSERT_STR_EQ("hello", string_data(s));

    value_free(s);
}

void test_string_create_empty(void) {
    Value *s = value_string("");

    ASSERT(s != NULL);
    ASSERT_EQ(0, string_length(s));
    ASSERT_STR_EQ("", string_data(s));

    value_free(s);
}

void test_string_create_with_length(void) {
    Value *s = value_string_n("hello world", 5);

    ASSERT(s != NULL);
    ASSERT_EQ(5, string_length(s));
    ASSERT_STR_EQ("hello", string_data(s));

    value_free(s);
}

void test_string_create_with_nulls(void) {
    /* String with embedded null bytes */
    Value *s = value_string_n("hel\0lo", 6);

    ASSERT(s != NULL);
    ASSERT_EQ(6, string_length(s));

    value_free(s);
}

/* String Length Tests */

void test_string_length_basic(void) {
    Value *s = value_string("hello");

    ASSERT_EQ(5, string_length(s));

    value_free(s);
}

void test_string_length_empty(void) {
    Value *s = value_string("");

    ASSERT_EQ(0, string_length(s));

    value_free(s);
}

void test_string_length_unicode(void) {
    /* UTF-8: "héllo" - é is 2 bytes */
    Value *s = value_string("h\xc3\xa9llo");

    ASSERT_EQ(6, string_length(s)); /* Byte length */

    value_free(s);
}

/* String Compare Tests */

void test_string_compare_equal(void) {
    Value *a = value_string("hello");
    Value *b = value_string("hello");

    ASSERT_EQ(0, string_compare(a, b));
    ASSERT(string_equals(a, b));

    value_free(a);
    value_free(b);
}

void test_string_compare_less(void) {
    Value *a = value_string("apple");
    Value *b = value_string("banana");

    ASSERT(string_compare(a, b) < 0);
    ASSERT(!string_equals(a, b));

    value_free(a);
    value_free(b);
}

void test_string_compare_greater(void) {
    Value *a = value_string("zebra");
    Value *b = value_string("apple");

    ASSERT(string_compare(a, b) > 0);

    value_free(a);
    value_free(b);
}

void test_string_compare_prefix(void) {
    Value *a = value_string("hello");
    Value *b = value_string("hello world");

    ASSERT(string_compare(a, b) < 0);

    value_free(a);
    value_free(b);
}

void test_string_compare_empty(void) {
    Value *a = value_string("");
    Value *b = value_string("hello");
    Value *c = value_string("");

    ASSERT(string_compare(a, b) < 0);
    ASSERT_EQ(0, string_compare(a, c));

    value_free(a);
    value_free(b);
    value_free(c);
}

/* String Concat Tests */

void test_string_concat_basic(void) {
    Value *a = value_string("hello");
    Value *b = value_string(" world");

    Value *result = string_concat(a, b);

    ASSERT(result != NULL);
    ASSERT_STR_EQ("hello world", string_data(result));

    value_free(a);
    value_free(b);
    value_free(result);
}

void test_string_concat_empty_left(void) {
    Value *a = value_string("");
    Value *b = value_string("hello");

    Value *result = string_concat(a, b);

    ASSERT_STR_EQ("hello", string_data(result));

    value_free(a);
    value_free(b);
    value_free(result);
}

void test_string_concat_empty_right(void) {
    Value *a = value_string("hello");
    Value *b = value_string("");

    Value *result = string_concat(a, b);

    ASSERT_STR_EQ("hello", string_data(result));

    value_free(a);
    value_free(b);
    value_free(result);
}

void test_string_concat_both_empty(void) {
    Value *a = value_string("");
    Value *b = value_string("");

    Value *result = string_concat(a, b);

    ASSERT_STR_EQ("", string_data(result));
    ASSERT_EQ(0, string_length(result));

    value_free(a);
    value_free(b);
    value_free(result);
}

/* String Slice Tests */

void test_string_slice_basic(void) {
    Value *s = value_string("hello world");

    Value *slice = string_slice(s, 0, 5);

    ASSERT(slice != NULL);
    ASSERT_STR_EQ("hello", string_data(slice));

    value_free(s);
    value_free(slice);
}

void test_string_slice_middle(void) {
    Value *s = value_string("hello world");

    Value *slice = string_slice(s, 6, 11);

    ASSERT_STR_EQ("world", string_data(slice));

    value_free(s);
    value_free(slice);
}

void test_string_slice_empty(void) {
    Value *s = value_string("hello");

    Value *slice = string_slice(s, 2, 2);

    ASSERT(slice != NULL);
    ASSERT_EQ(0, string_length(slice));

    value_free(s);
    value_free(slice);
}

void test_string_slice_full(void) {
    Value *s = value_string("hello");

    Value *slice = string_slice(s, 0, 5);

    ASSERT_STR_EQ("hello", string_data(slice));

    value_free(s);
    value_free(slice);
}

/* String Find Tests */

void test_string_find_exists(void) {
    Value *s = value_string("hello world");

    int64_t idx = string_find(s, "world");

    ASSERT_EQ(6, idx);

    value_free(s);
}

void test_string_find_at_start(void) {
    Value *s = value_string("hello world");

    int64_t idx = string_find(s, "hello");

    ASSERT_EQ(0, idx);

    value_free(s);
}

void test_string_find_not_exists(void) {
    Value *s = value_string("hello world");

    int64_t idx = string_find(s, "xyz");

    ASSERT_EQ(-1, idx);

    value_free(s);
}

void test_string_find_empty_needle(void) {
    Value *s = value_string("hello");

    int64_t idx = string_find(s, "");

    ASSERT_EQ(0, idx); /* Empty string found at start */

    value_free(s);
}

/* String Split Tests */

void test_string_split_basic(void) {
    Value *s = value_string("a,b,c");

    Value *parts = string_split(s, ",");

    ASSERT(parts != NULL);
    ASSERT_EQ(VAL_ARRAY, parts->type);
    ASSERT_EQ(3, array_length(parts));
    ASSERT_STR_EQ("a", string_data(array_get(parts, 0)));
    ASSERT_STR_EQ("b", string_data(array_get(parts, 1)));
    ASSERT_STR_EQ("c", string_data(array_get(parts, 2)));

    value_free(s);
    value_free(parts);
}

void test_string_split_no_delimiter(void) {
    Value *s = value_string("hello");

    Value *parts = string_split(s, ",");

    ASSERT(parts != NULL);
    ASSERT_EQ(1, array_length(parts));
    ASSERT_STR_EQ("hello", string_data(array_get(parts, 0)));

    value_free(s);
    value_free(parts);
}

void test_string_split_empty(void) {
    Value *s = value_string("");

    Value *parts = string_split(s, ",");

    ASSERT(parts != NULL);
    ASSERT_EQ(1, array_length(parts));
    ASSERT_STR_EQ("", string_data(array_get(parts, 0)));

    value_free(s);
    value_free(parts);
}

void test_string_split_consecutive_delimiters(void) {
    Value *s = value_string("a,,b");

    Value *parts = string_split(s, ",");

    ASSERT(parts != NULL);
    ASSERT_EQ(3, array_length(parts));
    ASSERT_STR_EQ("a", string_data(array_get(parts, 0)));
    ASSERT_STR_EQ("", string_data(array_get(parts, 1)));
    ASSERT_STR_EQ("b", string_data(array_get(parts, 2)));

    value_free(s);
    value_free(parts);
}

/* String Join Tests */

void test_string_join_basic(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_string("a"));
    arr = array_push(arr, value_string("b"));
    arr = array_push(arr, value_string("c"));

    Value *result = string_join(arr, ",");

    ASSERT(result != NULL);
    ASSERT_STR_EQ("a,b,c", string_data(result));

    value_free(arr);
    value_free(result);
}

void test_string_join_single(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_string("hello"));

    Value *result = string_join(arr, ",");

    ASSERT_STR_EQ("hello", string_data(result));

    value_free(arr);
    value_free(result);
}

void test_string_join_empty_array(void) {
    Value *arr = value_array();

    Value *result = string_join(arr, ",");

    ASSERT(result != NULL);
    ASSERT_EQ(0, string_length(result));

    value_free(arr);
    value_free(result);
}

void test_string_join_empty_separator(void) {
    Value *arr = value_array();
    arr = array_push(arr, value_string("a"));
    arr = array_push(arr, value_string("b"));
    arr = array_push(arr, value_string("c"));

    Value *result = string_join(arr, "");

    ASSERT_STR_EQ("abc", string_data(result));

    value_free(arr);
    value_free(result);
}

/* String Trim Tests */

void test_string_trim_spaces(void) {
    Value *s = value_string("  hello  ");

    Value *trimmed = string_trim(s);

    ASSERT(trimmed != NULL);
    ASSERT_STR_EQ("hello", string_data(trimmed));

    value_free(s);
    value_free(trimmed);
}

void test_string_trim_no_spaces(void) {
    Value *s = value_string("hello");

    Value *trimmed = string_trim(s);

    ASSERT_STR_EQ("hello", string_data(trimmed));

    value_free(s);
    value_free(trimmed);
}

void test_string_trim_only_spaces(void) {
    Value *s = value_string("   ");

    Value *trimmed = string_trim(s);

    ASSERT_EQ(0, string_length(trimmed));

    value_free(s);
    value_free(trimmed);
}

void test_string_trim_tabs_newlines(void) {
    Value *s = value_string("\t\nhello\r\n");

    Value *trimmed = string_trim(s);

    ASSERT_STR_EQ("hello", string_data(trimmed));

    value_free(s);
    value_free(trimmed);
}

/* String Upper/Lower Tests */

void test_string_upper_basic(void) {
    Value *s = value_string("hello");

    Value *upper = string_upper(s);

    ASSERT(upper != NULL);
    ASSERT_STR_EQ("HELLO", string_data(upper));

    value_free(s);
    value_free(upper);
}

void test_string_upper_mixed(void) {
    Value *s = value_string("HeLLo WoRLd");

    Value *upper = string_upper(s);

    ASSERT_STR_EQ("HELLO WORLD", string_data(upper));

    value_free(s);
    value_free(upper);
}

void test_string_lower_basic(void) {
    Value *s = value_string("HELLO");

    Value *lower = string_lower(s);

    ASSERT(lower != NULL);
    ASSERT_STR_EQ("hello", string_data(lower));

    value_free(s);
    value_free(lower);
}

void test_string_lower_mixed(void) {
    Value *s = value_string("HeLLo WoRLd");

    Value *lower = string_lower(s);

    ASSERT_STR_EQ("hello world", string_data(lower));

    value_free(s);
    value_free(lower);
}

void test_string_upper_lower_with_numbers(void) {
    Value *s = value_string("Hello123World");

    Value *upper = string_upper(s);
    Value *lower = string_lower(s);

    ASSERT_STR_EQ("HELLO123WORLD", string_data(upper));
    ASSERT_STR_EQ("hello123world", string_data(lower));

    value_free(s);
    value_free(upper);
    value_free(lower);
}

/* String Replace Tests */

void test_string_replace_basic(void) {
    Value *s = value_string("hello world");

    Value *replaced = string_replace(s, "world", "there");

    ASSERT(replaced != NULL);
    ASSERT_STR_EQ("hello there", string_data(replaced));

    value_free(s);
    value_free(replaced);
}

void test_string_replace_multiple(void) {
    Value *s = value_string("aaa");

    Value *replaced = string_replace(s, "a", "b");

    ASSERT_STR_EQ("bbb", string_data(replaced));

    value_free(s);
    value_free(replaced);
}

void test_string_replace_not_found(void) {
    Value *s = value_string("hello");

    Value *replaced = string_replace(s, "xyz", "abc");

    ASSERT_STR_EQ("hello", string_data(replaced));

    value_free(s);
    value_free(replaced);
}

void test_string_replace_empty_old(void) {
    Value *s = value_string("hello");

    Value *replaced = string_replace(s, "", "x");

    /* Replacing empty string - implementation specific */
    ASSERT(replaced != NULL);

    value_free(s);
    value_free(replaced);
}

/* String Starts/Ends With Tests */

void test_string_starts_with_true(void) {
    Value *s = value_string("hello world");

    ASSERT(string_starts_with(s, "hello"));

    value_free(s);
}

void test_string_starts_with_false(void) {
    Value *s = value_string("hello world");

    ASSERT(!string_starts_with(s, "world"));

    value_free(s);
}

void test_string_starts_with_empty(void) {
    Value *s = value_string("hello");

    ASSERT(string_starts_with(s, ""));

    value_free(s);
}

void test_string_ends_with_true(void) {
    Value *s = value_string("hello world");

    ASSERT(string_ends_with(s, "world"));

    value_free(s);
}

void test_string_ends_with_false(void) {
    Value *s = value_string("hello world");

    ASSERT(!string_ends_with(s, "hello"));

    value_free(s);
}

void test_string_ends_with_empty(void) {
    Value *s = value_string("hello");

    ASSERT(string_ends_with(s, ""));

    value_free(s);
}

/* String Hash Tests */

void test_string_hash_consistent(void) {
    Value *a = value_string("hello");
    Value *b = value_string("hello");

    ASSERT_EQ(string_hash(a), string_hash(b));

    value_free(a);
    value_free(b);
}

void test_string_hash_different(void) {
    Value *a = value_string("hello");
    Value *b = value_string("world");

    ASSERT(string_hash(a) != string_hash(b));

    value_free(a);
    value_free(b);
}

/* Null Input Tests */

void test_string_null_inputs(void) {
    /* Functions should handle NULL gracefully without crashing */
    ASSERT_EQ(0, string_length(NULL));
    ASSERT(string_data(NULL) == NULL);
    ASSERT_EQ(0, string_hash(NULL));
    ASSERT_EQ(-1, string_find(NULL, "x"));
    ASSERT(!string_equals(NULL, NULL));
    ASSERT(!string_starts_with(NULL, "x"));
    ASSERT(!string_ends_with(NULL, "x"));

    /* string_concat returns nil when NULL - defensive behavior */
    Value *concat_result = string_concat(NULL, NULL);
    ASSERT(concat_result != NULL);
    ASSERT(value_is_nil(concat_result));
    value_free(concat_result);

    /* string_slice returns nil when NULL - defensive behavior */
    Value *slice_result = string_slice(NULL, 0, 1);
    ASSERT(slice_result != NULL);
    ASSERT(value_is_nil(slice_result));
    value_free(slice_result);

    /* string_split returns empty array when NULL - defensive behavior */
    Value *split_result = string_split(NULL, ",");
    ASSERT(split_result != NULL);
    ASSERT_EQ(VAL_ARRAY, split_result->type);
    ASSERT_EQ(0, array_length(split_result));
    value_free(split_result);

    /* string_trim returns nil when NULL - defensive behavior */
    Value *trim_result = string_trim(NULL);
    ASSERT(trim_result != NULL);
    ASSERT(value_is_nil(trim_result));
    value_free(trim_result);

    /* string_upper returns nil when NULL - defensive behavior */
    Value *upper_result = string_upper(NULL);
    ASSERT(upper_result != NULL);
    ASSERT(value_is_nil(upper_result));
    value_free(upper_result);

    /* string_lower returns nil when NULL - defensive behavior */
    Value *lower_result = string_lower(NULL);
    ASSERT(lower_result != NULL);
    ASSERT(value_is_nil(lower_result));
    value_free(lower_result);
}

/* Main */

int main(void) {
    printf("Running string operations tests...\n\n");

    printf("String Creation Tests:\n");
    RUN_TEST(test_string_create_basic);
    RUN_TEST(test_string_create_empty);
    RUN_TEST(test_string_create_with_length);
    RUN_TEST(test_string_create_with_nulls);

    printf("\nString Length Tests:\n");
    RUN_TEST(test_string_length_basic);
    RUN_TEST(test_string_length_empty);
    RUN_TEST(test_string_length_unicode);

    printf("\nString Compare Tests:\n");
    RUN_TEST(test_string_compare_equal);
    RUN_TEST(test_string_compare_less);
    RUN_TEST(test_string_compare_greater);
    RUN_TEST(test_string_compare_prefix);
    RUN_TEST(test_string_compare_empty);

    printf("\nString Concat Tests:\n");
    RUN_TEST(test_string_concat_basic);
    RUN_TEST(test_string_concat_empty_left);
    RUN_TEST(test_string_concat_empty_right);
    RUN_TEST(test_string_concat_both_empty);

    printf("\nString Slice Tests:\n");
    RUN_TEST(test_string_slice_basic);
    RUN_TEST(test_string_slice_middle);
    RUN_TEST(test_string_slice_empty);
    RUN_TEST(test_string_slice_full);

    printf("\nString Find Tests:\n");
    RUN_TEST(test_string_find_exists);
    RUN_TEST(test_string_find_at_start);
    RUN_TEST(test_string_find_not_exists);
    RUN_TEST(test_string_find_empty_needle);

    printf("\nString Split Tests:\n");
    RUN_TEST(test_string_split_basic);
    RUN_TEST(test_string_split_no_delimiter);
    RUN_TEST(test_string_split_empty);
    RUN_TEST(test_string_split_consecutive_delimiters);

    printf("\nString Join Tests:\n");
    RUN_TEST(test_string_join_basic);
    RUN_TEST(test_string_join_single);
    RUN_TEST(test_string_join_empty_array);
    RUN_TEST(test_string_join_empty_separator);

    printf("\nString Trim Tests:\n");
    RUN_TEST(test_string_trim_spaces);
    RUN_TEST(test_string_trim_no_spaces);
    RUN_TEST(test_string_trim_only_spaces);
    RUN_TEST(test_string_trim_tabs_newlines);

    printf("\nString Upper/Lower Tests:\n");
    RUN_TEST(test_string_upper_basic);
    RUN_TEST(test_string_upper_mixed);
    RUN_TEST(test_string_lower_basic);
    RUN_TEST(test_string_lower_mixed);
    RUN_TEST(test_string_upper_lower_with_numbers);

    printf("\nString Replace Tests:\n");
    RUN_TEST(test_string_replace_basic);
    RUN_TEST(test_string_replace_multiple);
    RUN_TEST(test_string_replace_not_found);
    RUN_TEST(test_string_replace_empty_old);

    printf("\nString Starts/Ends With Tests:\n");
    RUN_TEST(test_string_starts_with_true);
    RUN_TEST(test_string_starts_with_false);
    RUN_TEST(test_string_starts_with_empty);
    RUN_TEST(test_string_ends_with_true);
    RUN_TEST(test_string_ends_with_false);
    RUN_TEST(test_string_ends_with_empty);

    printf("\nString Hash Tests:\n");
    RUN_TEST(test_string_hash_consistent);
    RUN_TEST(test_string_hash_different);

    printf("\nNull Input Tests:\n");
    RUN_TEST(test_string_null_inputs);

    return TEST_RESULT();
}
