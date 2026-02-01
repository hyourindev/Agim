/*
 * Agim - Test Common Header
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_TEST_COMMON_H
#define AGIM_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond)                                                    \
    do {                                                                \
        tests_run++;                                                    \
        if (!(cond)) {                                                  \
            printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            abort();                                                    \
        } else {                                                        \
            tests_passed++;                                             \
        }                                                               \
    } while (0)

#define ASSERT_EQ(expected, actual)                                     \
    do {                                                                \
        long long _expected = (long long)(expected);                    \
        long long _actual = (long long)(actual);                        \
        tests_run++;                                                    \
        if (_expected != _actual) {                                     \
            printf("  FAIL: %s:%d: expected %lld, got %lld\n",          \
                   __FILE__, __LINE__, _expected, _actual);             \
            abort();                                                    \
        } else {                                                        \
            tests_passed++;                                             \
        }                                                               \
    } while (0)

#define ASSERT_STR_EQ(expected, actual)                                 \
    do {                                                                \
        tests_run++;                                                    \
        if (strcmp((expected), (actual)) != 0) {                        \
            printf("  FAIL: %s:%d: expected \"%s\", got \"%s\"\n",      \
                   __FILE__, __LINE__, (expected), (actual));           \
            abort();                                                    \
        } else {                                                        \
            tests_passed++;                                             \
        }                                                               \
    } while (0)

#define RUN_TEST(fn)                                                    \
    do {                                                                \
        printf("Running %s...\n", #fn);                                 \
        fn();                                                           \
    } while (0)

#define TEST_RESULT()                                                   \
    (printf("\n%d/%d tests passed\n", tests_passed, tests_run),         \
     tests_passed == tests_run ? 0 : 1)

#endif /* AGIM_TEST_COMMON_H */
