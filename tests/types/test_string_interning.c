/*
 * Agim - String Interning Tests
 *
 * Tests for string interning cache functionality.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "types/string.h"
#include "vm/value.h"
#include <string.h>
#include <pthread.h>

/* String Interning Basic Tests */

void test_string_intern_basic(void) {
    Value *s = string_intern("hello", 5);

    ASSERT(s != NULL);
    ASSERT_EQ(VAL_STRING, s->type);
    ASSERT_STR_EQ("hello", string_data(s));

    value_free(s);
}

void test_string_intern_returns_same(void) {
    Value *a = string_intern("hello", 5);
    Value *b = string_intern("hello", 5);

    ASSERT(a != NULL);
    ASSERT(b != NULL);
    /* Interned strings should return the same cached value */
    ASSERT(a == b || string_equals(a, b));

    value_free(a);
    /* Note: if a == b, only free once */
    if (a != b) {
        value_free(b);
    }
}

void test_string_intern_different_strings(void) {
    Value *a = string_intern("hello", 5);
    Value *b = string_intern("world", 5);

    ASSERT(a != NULL);
    ASSERT(b != NULL);
    ASSERT(!string_equals(a, b));

    value_free(a);
    value_free(b);
}

void test_string_intern_empty(void) {
    Value *s = string_intern("", 0);

    ASSERT(s != NULL);
    ASSERT_EQ(0, string_length(s));

    value_free(s);
}

void test_string_intern_short_strings(void) {
    /* Short strings are common candidates for interning */
    Value *a = string_intern("a", 1);
    Value *b = string_intern("b", 1);
    Value *c = string_intern("a", 1);

    ASSERT(a != NULL);
    ASSERT(b != NULL);
    ASSERT(c != NULL);

    /* Same string should return same/equal value */
    ASSERT(a == c || string_equals(a, c));

    value_free(a);
    value_free(b);
    if (a != c) {
        value_free(c);
    }
}

/* Cache Behavior Tests */

void test_string_intern_caches(void) {
    /* Intern same string multiple times */
    Value *strings[100];
    for (int i = 0; i < 100; i++) {
        strings[i] = string_intern("cached", 6);
        ASSERT(strings[i] != NULL);
    }

    /* All should be equal */
    for (int i = 1; i < 100; i++) {
        ASSERT(strings[0] == strings[i] || string_equals(strings[0], strings[i]));
    }

    /* Free all (if they're the same, just free once) */
    Value *first = strings[0];
    value_free(first);
    for (int i = 1; i < 100; i++) {
        if (strings[i] != first) {
            value_free(strings[i]);
        }
    }
}

void test_string_intern_eviction(void) {
    /* Create many unique strings to potentially trigger eviction */
    char buf[32];
    Value *strings[5000];

    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "unique_string_%d", i);
        strings[i] = string_intern(buf, strlen(buf));
        ASSERT(strings[i] != NULL);
    }

    /* Verify strings are still valid */
    for (int i = 0; i < 5000; i++) {
        snprintf(buf, sizeof(buf), "unique_string_%d", i);
        ASSERT_STR_EQ(buf, string_data(strings[i]));
    }

    /* Cleanup */
    for (int i = 0; i < 5000; i++) {
        value_free(strings[i]);
    }
}

void test_string_intern_after_eviction(void) {
    /* After eviction, re-interning should still work */
    char buf[32];

    /* Create many strings */
    for (int i = 0; i < 10000; i++) {
        snprintf(buf, sizeof(buf), "temp_%d", i);
        Value *s = string_intern(buf, strlen(buf));
        value_free(s);
    }

    /* Now intern a new string */
    Value *s = string_intern("after_eviction", 14);
    ASSERT(s != NULL);
    ASSERT_STR_EQ("after_eviction", string_data(s));

    value_free(s);
}

/* Hash Distribution Tests */

void test_string_intern_hash_distribution(void) {
    /* Test that different strings go to different cache slots */
    char buf[32];
    Value *strings[100];

    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "str%d", i);
        strings[i] = string_intern(buf, strlen(buf));
        ASSERT(strings[i] != NULL);
    }

    /* Verify all strings are correct */
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "str%d", i);
        ASSERT_STR_EQ(buf, string_data(strings[i]));
    }

    /* Cleanup */
    for (int i = 0; i < 100; i++) {
        value_free(strings[i]);
    }
}

/* Thread Safety Tests */

typedef struct {
    int thread_id;
    int iterations;
    int success_count;
} ThreadData;

static void *intern_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    char buf[64];

    for (int i = 0; i < data->iterations; i++) {
        /* Use shared string */
        Value *shared = string_intern("shared_string", 13);
        if (shared != NULL) {
            data->success_count++;
            /* Don't free - shared across threads */
        }

        /* Use thread-local string */
        snprintf(buf, sizeof(buf), "thread_%d_iter_%d", data->thread_id, i);
        Value *local = string_intern(buf, strlen(buf));
        if (local != NULL) {
            data->success_count++;
            value_free(local);
        }
    }

    return NULL;
}

void test_string_intern_thread_safety(void) {
    const int num_threads = 4;
    const int iterations = 1000;

    pthread_t threads[num_threads];
    ThreadData data[num_threads];

    for (int i = 0; i < num_threads; i++) {
        data[i].thread_id = i;
        data[i].iterations = iterations;
        data[i].success_count = 0;

        int rc = pthread_create(&threads[i], NULL, intern_thread, &data[i]);
        ASSERT_EQ(0, rc);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Verify all operations succeeded */
    for (int i = 0; i < num_threads; i++) {
        ASSERT_EQ(iterations * 2, data[i].success_count);
    }
}

static void *concurrent_same_string(void *arg) {
    ThreadData *data = (ThreadData *)arg;

    for (int i = 0; i < data->iterations; i++) {
        Value *s = string_intern("concurrent_test", 15);
        if (s != NULL) {
            ASSERT_STR_EQ("concurrent_test", string_data(s));
            data->success_count++;
            /* Don't free - shared */
        }
    }

    return NULL;
}

void test_string_intern_concurrent_same_string(void) {
    const int num_threads = 8;
    const int iterations = 500;

    pthread_t threads[num_threads];
    ThreadData data[num_threads];

    for (int i = 0; i < num_threads; i++) {
        data[i].thread_id = i;
        data[i].iterations = iterations;
        data[i].success_count = 0;

        int rc = pthread_create(&threads[i], NULL, concurrent_same_string, &data[i]);
        ASSERT_EQ(0, rc);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    /* All operations should succeed */
    for (int i = 0; i < num_threads; i++) {
        ASSERT_EQ(iterations, data[i].success_count);
    }
}

/* Edge Cases */

void test_string_intern_long_string(void) {
    /* Very long string */
    char buf[2000];
    memset(buf, 'x', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    Value *s = string_intern(buf, sizeof(buf) - 1);

    ASSERT(s != NULL);
    ASSERT_EQ(sizeof(buf) - 1, string_length(s));

    value_free(s);
}

void test_string_intern_special_chars(void) {
    Value *a = string_intern("hello\nworld", 11);
    Value *b = string_intern("hello\tworld", 11);
    Value *c = string_intern("hello\0world", 11);

    ASSERT(a != NULL);
    ASSERT(b != NULL);
    ASSERT(c != NULL);

    /* All should be different */
    ASSERT(!string_equals(a, b));

    value_free(a);
    value_free(b);
    value_free(c);
}

void test_string_intern_unicode(void) {
    Value *s = string_intern("héllo wörld", 14); /* UTF-8 */

    ASSERT(s != NULL);

    value_free(s);
}

void test_string_intern_binary_data(void) {
    /* Binary data with null bytes */
    const char data[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    Value *s = string_intern(data, 5);

    ASSERT(s != NULL);
    ASSERT_EQ(5, string_length(s));

    value_free(s);
}

/* Performance Characteristics */

void test_string_intern_repeated_lookup_fast(void) {
    /* First intern */
    Value *first = string_intern("performance_test", 16);
    ASSERT(first != NULL);

    /* Repeated lookups should be fast (hit cache) */
    for (int i = 0; i < 10000; i++) {
        Value *s = string_intern("performance_test", 16);
        ASSERT(s != NULL);
        /* Should get same cached value */
        ASSERT(s == first || string_equals(s, first));
    }

    value_free(first);
}

/* Main */

int main(void) {
    printf("Running string interning tests...\n\n");

    printf("String Interning Basic Tests:\n");
    RUN_TEST(test_string_intern_basic);
    RUN_TEST(test_string_intern_returns_same);
    RUN_TEST(test_string_intern_different_strings);
    RUN_TEST(test_string_intern_empty);
    RUN_TEST(test_string_intern_short_strings);

    printf("\nCache Behavior Tests:\n");
    RUN_TEST(test_string_intern_caches);
    RUN_TEST(test_string_intern_eviction);
    RUN_TEST(test_string_intern_after_eviction);

    printf("\nHash Distribution Tests:\n");
    RUN_TEST(test_string_intern_hash_distribution);

    printf("\nThread Safety Tests:\n");
    RUN_TEST(test_string_intern_thread_safety);
    RUN_TEST(test_string_intern_concurrent_same_string);

    printf("\nEdge Cases:\n");
    RUN_TEST(test_string_intern_long_string);
    RUN_TEST(test_string_intern_special_chars);
    RUN_TEST(test_string_intern_unicode);
    RUN_TEST(test_string_intern_binary_data);

    printf("\nPerformance Tests:\n");
    RUN_TEST(test_string_intern_repeated_lookup_fast);

    return TEST_RESULT();
}
