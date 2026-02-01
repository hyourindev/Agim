/*
 * Agim - Property Testing Framework
 *
 * A simple property-based testing framework for C.
 * Generates random inputs and verifies properties hold.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_PROPERTY_TEST_H
#define AGIM_PROPERTY_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Configuration */
#define PROP_DEFAULT_ITERATIONS 100
#define PROP_MAX_STRING_LEN 256
#define PROP_MAX_ARRAY_LEN 100

/* Test state */
static int prop_tests_run = 0;
static int prop_tests_passed = 0;
static int prop_tests_failed = 0;
static unsigned int prop_seed = 0;

/* Initialize property testing with optional seed */
static inline void prop_init(unsigned int seed) {
    prop_seed = seed ? seed : (unsigned int)time(NULL);
    srand(prop_seed);
    prop_tests_run = 0;
    prop_tests_passed = 0;
    prop_tests_failed = 0;
}

/* Random generators */
static inline int prop_rand_int(void) {
    return rand();
}

static inline int prop_rand_int_range(int min, int max) {
    if (min >= max) return min;
    return min + (rand() % (max - min + 1));
}

static inline double prop_rand_double(void) {
    return (double)rand() / RAND_MAX;
}

static inline double prop_rand_double_range(double min, double max) {
    return min + prop_rand_double() * (max - min);
}

static inline bool prop_rand_bool(void) {
    return rand() % 2 == 0;
}

static inline size_t prop_rand_size(size_t max) {
    return (size_t)(rand() % (max + 1));
}

/* Generate random string (caller must free) */
static inline char *prop_rand_string(size_t max_len) {
    size_t len = prop_rand_size(max_len);
    char *str = malloc(len + 1);
    if (!str) return NULL;

    for (size_t i = 0; i < len; i++) {
        str[i] = 'a' + (rand() % 26);
    }
    str[len] = '\0';
    return str;
}

/* Generate random alphanumeric string */
static inline char *prop_rand_alnum_string(size_t max_len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t len = prop_rand_size(max_len);
    char *str = malloc(len + 1);
    if (!str) return NULL;

    for (size_t i = 0; i < len; i++) {
        str[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    str[len] = '\0';
    return str;
}

/* Property test result */
typedef struct {
    bool passed;
    int iteration;
    const char *message;
} PropResult;

/* Property function type */
typedef bool (*PropFunc)(void *context);

/* Run a property test */
static inline PropResult prop_check(const char *name, PropFunc prop, void *context, int iterations) {
    (void)name;  /* Used by PROP_CHECK macro for printing */
    PropResult result = {true, 0, NULL};

    for (int i = 0; i < iterations; i++) {
        if (!prop(context)) {
            result.passed = false;
            result.iteration = i;
            result.message = "Property failed";
            break;
        }
    }

    return result;
}

/* Macros for property testing */
#define PROP_CHECK(name, prop_func, context, iterations) do { \
    prop_tests_run++; \
    printf("  Checking: %s (%d iterations)... ", name, iterations); \
    fflush(stdout); \
    PropResult _result = prop_check(name, prop_func, context, iterations); \
    if (_result.passed) { \
        printf("OK\n"); \
        prop_tests_passed++; \
    } else { \
        printf("FAILED at iteration %d\n", _result.iteration); \
        prop_tests_failed++; \
    } \
} while(0)

#define PROP_ASSERT(cond) do { \
    if (!(cond)) return false; \
} while(0)

#define PROP_RESULT() (prop_tests_failed == 0 ? 0 : 1)

#define PROP_SUMMARY() do { \
    printf("\nProperty Tests: %d/%d passed", prop_tests_passed, prop_tests_run); \
    if (prop_tests_failed > 0) { \
        printf(" (%d failed)", prop_tests_failed); \
    } \
    printf(" [seed: %u]\n", prop_seed); \
} while(0)

/* Shrinking support (simplified) */
typedef struct {
    void *data;
    size_t size;
} ShrinkCandidate;

/* Common property patterns */

/* Idempotence: f(f(x)) == f(x) */
#define PROP_IDEMPOTENT(func, input, cmp) \
    (cmp(func(func(input)), func(input)))

/* Inverse: f(g(x)) == x */
#define PROP_INVERSE(f, g, input, cmp) \
    (cmp(f(g(input)), input))

/* Commutative: f(a, b) == f(b, a) */
#define PROP_COMMUTATIVE(func, a, b, cmp) \
    (cmp(func(a, b), func(b, a)))

/* Associative: f(f(a, b), c) == f(a, f(b, c)) */
#define PROP_ASSOCIATIVE(func, a, b, c, cmp) \
    (cmp(func(func(a, b), c), func(a, func(b, c))))

#endif /* AGIM_PROPERTY_TEST_H */
