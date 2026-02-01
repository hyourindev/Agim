/*
 * Agim - NaN Boxing Tests
 *
 * P1.1.8.4: Tests for NaN-boxed value representation.
 * - Encoding and decoding of all value types
 * - Type checking predicates
 * - Edge cases (NaN, infinity, negative zero)
 * - Integer sign extension
 * - Equality and comparison
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/nanbox.h"

#include <math.h>
#include <float.h>

/*
 * Test: nanbox_double encodes regular doubles
 */
void test_nanbox_double_regular(void) {
    double values[] = {0.0, 1.0, -1.0, 3.14159, -2.71828, 1e100, -1e-100};
    size_t count = sizeof(values) / sizeof(values[0]);

    for (size_t i = 0; i < count; i++) {
        NanValue v = nanbox_double(values[i]);
        ASSERT(nanbox_is_double(v));
        ASSERT(!nanbox_is_int(v));
        ASSERT(!nanbox_is_obj(v));
        ASSERT(!nanbox_is_special(v));
        ASSERT(!nanbox_is_pid(v));

        double decoded = nanbox_as_double(v);
        ASSERT(decoded == values[i]);
    }
}

/*
 * Test: nanbox_double encodes infinity
 */
void test_nanbox_double_infinity(void) {
    NanValue pos_inf = nanbox_double(INFINITY);
    NanValue neg_inf = nanbox_double(-INFINITY);

    ASSERT(nanbox_is_double(pos_inf));
    ASSERT(nanbox_is_double(neg_inf));

    ASSERT(nanbox_as_double(pos_inf) == INFINITY);
    ASSERT(nanbox_as_double(neg_inf) == -INFINITY);
}

/*
 * Test: nanbox_double encodes negative zero
 */
void test_nanbox_double_negative_zero(void) {
    NanValue v = nanbox_double(-0.0);

    ASSERT(nanbox_is_double(v));
    double decoded = nanbox_as_double(v);

    /* Check it's negative zero */
    ASSERT(decoded == 0.0);
    ASSERT(1.0 / decoded < 0);  /* -Infinity for -0.0 */
}

/*
 * Test: nanbox_int encodes positive integers
 */
void test_nanbox_int_positive(void) {
    int64_t values[] = {0, 1, 42, 1000, 123456789, 0x7FFFFFFFFFFF};
    size_t count = sizeof(values) / sizeof(values[0]);

    for (size_t i = 0; i < count; i++) {
        NanValue v = nanbox_int(values[i]);
        ASSERT(nanbox_is_int(v));
        ASSERT(!nanbox_is_double(v));
        ASSERT(!nanbox_is_obj(v));

        int64_t decoded = nanbox_as_int(v);
        ASSERT_EQ(values[i], decoded);
    }
}

/*
 * Test: nanbox_int encodes negative integers
 */
void test_nanbox_int_negative(void) {
    int64_t values[] = {-1, -42, -1000, -123456789, -0x7FFFFFFFFFFF};
    size_t count = sizeof(values) / sizeof(values[0]);

    for (size_t i = 0; i < count; i++) {
        NanValue v = nanbox_int(values[i]);
        ASSERT(nanbox_is_int(v));

        int64_t decoded = nanbox_as_int(v);
        ASSERT_EQ(values[i], decoded);
    }
}

/*
 * Test: nanbox_int sign extension
 */
void test_nanbox_int_sign_extension(void) {
    /* Test that bit 47 (sign bit in 48-bit payload) extends correctly */
    int64_t negative = -1;
    NanValue v = nanbox_int(negative);
    int64_t decoded = nanbox_as_int(v);
    ASSERT_EQ(-1, decoded);

    /* Test boundary values */
    int64_t max_positive = 0x7FFFFFFFFFFF;  /* 48-bit max positive */
    v = nanbox_int(max_positive);
    decoded = nanbox_as_int(v);
    ASSERT_EQ(max_positive, decoded);

    int64_t min_negative = -0x800000000000LL;  /* 48-bit min negative */
    v = nanbox_int(min_negative);
    decoded = nanbox_as_int(v);
    ASSERT_EQ(min_negative, decoded);
}

/*
 * Test: nanbox_obj encodes pointers
 */
void test_nanbox_obj(void) {
    int dummy1, dummy2, dummy3;
    void *ptrs[] = {&dummy1, &dummy2, &dummy3, NULL};
    size_t count = sizeof(ptrs) / sizeof(ptrs[0]);

    for (size_t i = 0; i < count; i++) {
        NanValue v = nanbox_obj(ptrs[i]);
        if (ptrs[i] != NULL) {
            ASSERT(nanbox_is_obj(v));
            ASSERT(!nanbox_is_double(v));
            ASSERT(!nanbox_is_int(v));
        }

        void *decoded = nanbox_as_obj(v);
        ASSERT_EQ(ptrs[i], decoded);
    }
}

/*
 * Test: nanbox_pid encodes process IDs
 */
void test_nanbox_pid(void) {
    uint64_t pids[] = {0, 1, 42, 1000, 0xFFFFFFFFFFFF};
    size_t count = sizeof(pids) / sizeof(pids[0]);

    for (size_t i = 0; i < count; i++) {
        NanValue v = nanbox_pid(pids[i]);
        ASSERT(nanbox_is_pid(v));
        ASSERT(!nanbox_is_double(v));
        ASSERT(!nanbox_is_int(v));
        ASSERT(!nanbox_is_obj(v));

        uint64_t decoded = nanbox_as_pid(v);
        ASSERT_EQ(pids[i], decoded);
    }
}

/*
 * Test: nanbox_bool encodes booleans
 */
void test_nanbox_bool(void) {
    NanValue t = nanbox_bool(true);
    NanValue f = nanbox_bool(false);

    ASSERT(nanbox_is_bool(t));
    ASSERT(nanbox_is_bool(f));
    ASSERT(nanbox_is_special(t));
    ASSERT(nanbox_is_special(f));

    ASSERT(nanbox_is_true(t));
    ASSERT(!nanbox_is_true(f));
    ASSERT(nanbox_is_false(f));
    ASSERT(!nanbox_is_false(t));

    ASSERT(nanbox_as_bool(t) == true);
    ASSERT(nanbox_as_bool(f) == false);
}

/*
 * Test: NANBOX_NIL special value
 */
void test_nanbox_nil(void) {
    NanValue v = NANBOX_NIL;

    ASSERT(nanbox_is_nil(v));
    ASSERT(nanbox_is_special(v));
    ASSERT(!nanbox_is_double(v));
    ASSERT(!nanbox_is_int(v));
    ASSERT(!nanbox_is_bool(v));
    ASSERT(!nanbox_is_obj(v));
    ASSERT(!nanbox_is_pid(v));
}

/*
 * Test: NANBOX_TRUE and NANBOX_FALSE constants
 */
void test_nanbox_true_false_constants(void) {
    ASSERT_EQ(NANBOX_TRUE, NANBOX_VAL_TRUE);
    ASSERT_EQ(NANBOX_FALSE, NANBOX_VAL_FALSE);
    ASSERT_EQ(NANBOX_NIL, NANBOX_VAL_NIL);
}

/*
 * Test: nanbox_is_number covers ints and doubles
 */
void test_nanbox_is_number(void) {
    ASSERT(nanbox_is_number(nanbox_double(3.14)));
    ASSERT(nanbox_is_number(nanbox_int(42)));
    ASSERT(!nanbox_is_number(NANBOX_NIL));
    ASSERT(!nanbox_is_number(NANBOX_TRUE));
    ASSERT(!nanbox_is_number(nanbox_pid(1)));
}

/*
 * Test: nanbox_to_float conversion
 */
void test_nanbox_to_float(void) {
    /* Double -> float */
    ASSERT(nanbox_to_float(nanbox_double(3.14)) == 3.14);

    /* Int -> float */
    ASSERT(nanbox_to_float(nanbox_int(42)) == 42.0);

    /* Non-number -> 0.0 */
    ASSERT(nanbox_to_float(NANBOX_NIL) == 0.0);
    ASSERT(nanbox_to_float(NANBOX_TRUE) == 0.0);
}

/*
 * Test: nanbox_to_int conversion
 */
void test_nanbox_to_int(void) {
    /* Int -> int */
    ASSERT_EQ(42, nanbox_to_int(nanbox_int(42)));

    /* Double -> int (truncates) */
    ASSERT_EQ(3, nanbox_to_int(nanbox_double(3.7)));
    ASSERT_EQ(-3, nanbox_to_int(nanbox_double(-3.7)));

    /* Non-number -> 0 */
    ASSERT_EQ(0, nanbox_to_int(NANBOX_NIL));
    ASSERT_EQ(0, nanbox_to_int(NANBOX_TRUE));
}

/*
 * Test: nanbox_is_truthy
 */
void test_nanbox_is_truthy(void) {
    /* Nil and false are falsy */
    ASSERT(!nanbox_is_truthy(NANBOX_NIL));
    ASSERT(!nanbox_is_truthy(NANBOX_FALSE));

    /* True is truthy */
    ASSERT(nanbox_is_truthy(NANBOX_TRUE));

    /* Non-zero numbers are truthy */
    ASSERT(nanbox_is_truthy(nanbox_int(1)));
    ASSERT(nanbox_is_truthy(nanbox_int(-1)));
    ASSERT(nanbox_is_truthy(nanbox_double(0.1)));
    ASSERT(nanbox_is_truthy(nanbox_double(-0.1)));

    /* Zero is falsy */
    ASSERT(!nanbox_is_truthy(nanbox_int(0)));
    ASSERT(!nanbox_is_truthy(nanbox_double(0.0)));

    /* Objects/PIDs are truthy */
    int dummy;
    ASSERT(nanbox_is_truthy(nanbox_obj(&dummy)));
    ASSERT(nanbox_is_truthy(nanbox_pid(1)));
}

/*
 * Test: nanbox_equal for identical values
 */
void test_nanbox_equal_identical(void) {
    ASSERT(nanbox_equal(NANBOX_NIL, NANBOX_NIL));
    ASSERT(nanbox_equal(NANBOX_TRUE, NANBOX_TRUE));
    ASSERT(nanbox_equal(NANBOX_FALSE, NANBOX_FALSE));

    ASSERT(nanbox_equal(nanbox_int(42), nanbox_int(42)));
    ASSERT(nanbox_equal(nanbox_double(3.14), nanbox_double(3.14)));
    ASSERT(nanbox_equal(nanbox_pid(123), nanbox_pid(123)));
}

/*
 * Test: nanbox_equal for different values
 */
void test_nanbox_equal_different(void) {
    ASSERT(!nanbox_equal(NANBOX_NIL, NANBOX_TRUE));
    ASSERT(!nanbox_equal(NANBOX_TRUE, NANBOX_FALSE));
    ASSERT(!nanbox_equal(nanbox_int(42), nanbox_int(43)));
    ASSERT(!nanbox_equal(nanbox_double(3.14), nanbox_double(2.71)));
    ASSERT(!nanbox_equal(nanbox_pid(1), nanbox_pid(2)));
}

/*
 * Test: nanbox_equal for mixed int/double
 */
void test_nanbox_equal_mixed_numeric(void) {
    /* Same numeric value, different representation */
    ASSERT(nanbox_equal(nanbox_int(42), nanbox_double(42.0)));
    ASSERT(nanbox_equal(nanbox_double(100.0), nanbox_int(100)));

    /* Different numeric values */
    ASSERT(!nanbox_equal(nanbox_int(42), nanbox_double(42.5)));
}

/*
 * Test: nanbox_equal with NaN
 */
void test_nanbox_equal_nan(void) {
    NanValue nan1 = nanbox_double(NAN);
    NanValue nan2 = nanbox_double(NAN);

    /* NaN != NaN per IEEE 754 */
    ASSERT(!nanbox_equal(nan1, nan2));
    ASSERT(!nanbox_equal(nan1, nan1));
}

/*
 * Test: Tag bits are correct
 */
void test_nanbox_tag_bits(void) {
    NanValue int_val = nanbox_int(0);
    NanValue obj_val = nanbox_obj(NULL);
    NanValue pid_val = nanbox_pid(0);

    ASSERT_EQ(NANBOX_TAG_INT, int_val & NANBOX_TAG_MASK);
    ASSERT_EQ(NANBOX_TAG_OBJ, obj_val & NANBOX_TAG_MASK);
    ASSERT_EQ(NANBOX_TAG_PID, pid_val & NANBOX_TAG_MASK);
    ASSERT_EQ(NANBOX_TAG_SPECIAL, NANBOX_NIL & NANBOX_TAG_MASK);
}

/*
 * Test: Double encoding doesn't corrupt values
 */
void test_nanbox_double_roundtrip(void) {
    double values[] = {
        0.0, 1.0, -1.0,
        DBL_MIN, DBL_MAX,
        DBL_EPSILON,
        1.7976931348623157e+308,  /* Near DBL_MAX */
        2.2250738585072014e-308,  /* Near DBL_MIN */
    };
    size_t count = sizeof(values) / sizeof(values[0]);

    for (size_t i = 0; i < count; i++) {
        NanValue v = nanbox_double(values[i]);
        double decoded = nanbox_as_double(v);
        ASSERT(decoded == values[i]);
    }
}

/*
 * Test: Integer roundtrip at boundaries
 */
void test_nanbox_int_roundtrip(void) {
    int64_t values[] = {
        0,
        1, -1,
        127, -128,  /* int8 bounds */
        32767, -32768,  /* int16 bounds */
        2147483647, -2147483648,  /* int32 bounds */
        0x7FFFFFFFFFFF, -0x800000000000LL,  /* 48-bit bounds */
    };
    size_t count = sizeof(values) / sizeof(values[0]);

    for (size_t i = 0; i < count; i++) {
        NanValue v = nanbox_int(values[i]);
        int64_t decoded = nanbox_as_int(v);
        ASSERT_EQ(values[i], decoded);
    }
}

/*
 * Test: Type predicates are mutually exclusive
 */
void test_nanbox_type_mutual_exclusion(void) {
    NanValue values[] = {
        nanbox_double(3.14),
        nanbox_int(42),
        nanbox_obj(NULL),
        NANBOX_NIL,
        NANBOX_TRUE,
        nanbox_pid(1),
    };
    size_t count = sizeof(values) / sizeof(values[0]);

    for (size_t i = 0; i < count; i++) {
        NanValue v = values[i];
        int type_count = 0;

        /* Only for non-special non-double values */
        if (!nanbox_is_double(v)) {
            if (nanbox_is_int(v)) type_count++;
            if (nanbox_is_obj(v)) type_count++;
            if (nanbox_is_special(v)) type_count++;
            if (nanbox_is_pid(v)) type_count++;

            /* Each non-double value should match exactly one type */
            ASSERT_EQ(1, type_count);
        }
    }
}

/*
 * Test: Zero handling
 */
void test_nanbox_zero(void) {
    NanValue int_zero = nanbox_int(0);
    NanValue double_zero = nanbox_double(0.0);

    ASSERT(nanbox_is_int(int_zero));
    ASSERT(nanbox_is_double(double_zero));

    ASSERT_EQ(0, nanbox_as_int(int_zero));
    ASSERT(nanbox_as_double(double_zero) == 0.0);

    /* They should be equal as numbers */
    ASSERT(nanbox_equal(int_zero, double_zero));
}

/*
 * Test: Large PID values
 */
void test_nanbox_pid_large(void) {
    /* 48-bit max */
    uint64_t large_pid = 0xFFFFFFFFFFFF;
    NanValue v = nanbox_pid(large_pid);

    ASSERT(nanbox_is_pid(v));
    ASSERT_EQ(large_pid, nanbox_as_pid(v));
}

int main(void) {
    printf("Running NaN-boxing tests...\n");

    printf("\nDouble encoding tests:\n");
    RUN_TEST(test_nanbox_double_regular);
    RUN_TEST(test_nanbox_double_infinity);
    RUN_TEST(test_nanbox_double_negative_zero);
    RUN_TEST(test_nanbox_double_roundtrip);

    printf("\nInteger encoding tests:\n");
    RUN_TEST(test_nanbox_int_positive);
    RUN_TEST(test_nanbox_int_negative);
    RUN_TEST(test_nanbox_int_sign_extension);
    RUN_TEST(test_nanbox_int_roundtrip);

    printf("\nObject encoding tests:\n");
    RUN_TEST(test_nanbox_obj);

    printf("\nPID encoding tests:\n");
    RUN_TEST(test_nanbox_pid);
    RUN_TEST(test_nanbox_pid_large);

    printf("\nBoolean encoding tests:\n");
    RUN_TEST(test_nanbox_bool);

    printf("\nSpecial value tests:\n");
    RUN_TEST(test_nanbox_nil);
    RUN_TEST(test_nanbox_true_false_constants);

    printf("\nType checking tests:\n");
    RUN_TEST(test_nanbox_is_number);
    RUN_TEST(test_nanbox_type_mutual_exclusion);
    RUN_TEST(test_nanbox_tag_bits);

    printf("\nConversion tests:\n");
    RUN_TEST(test_nanbox_to_float);
    RUN_TEST(test_nanbox_to_int);

    printf("\nTruthiness tests:\n");
    RUN_TEST(test_nanbox_is_truthy);

    printf("\nEquality tests:\n");
    RUN_TEST(test_nanbox_equal_identical);
    RUN_TEST(test_nanbox_equal_different);
    RUN_TEST(test_nanbox_equal_mixed_numeric);
    RUN_TEST(test_nanbox_equal_nan);

    printf("\nEdge case tests:\n");
    RUN_TEST(test_nanbox_zero);

    return TEST_RESULT();
}
