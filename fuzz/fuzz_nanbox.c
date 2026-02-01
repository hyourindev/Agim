/*
 * Agim - NaN-Boxing Fuzzer
 *
 * Fuzz target for NaN-boxed value operations using libFuzzer.
 * Tests value type detection and extraction for all 64-bit patterns.
 *
 * Build: cmake -DAGIM_ENABLE_FUZZING=ON -DCMAKE_C_COMPILER=clang ..
 * Run:   ./fuzz_nanbox corpus/nanbox/ -max_len=64
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "vm/nanbox.h"

/*
 * Test type detection for a given 64-bit NaN-boxed value.
 * The NaN-boxing scheme must correctly identify all patterns.
 */
static void test_type_detection(NanValue v) {
    /* Type detection functions should not crash */
    bool is_double = nanbox_is_double(v);
    bool is_int = nanbox_is_int(v);
    bool is_obj = nanbox_is_obj(v);
    bool is_special = nanbox_is_special(v);
    bool is_pid = nanbox_is_pid(v);
    bool is_nil = nanbox_is_nil(v);
    bool is_bool = nanbox_is_bool(v);
    bool is_number = nanbox_is_number(v);

    /* Suppress unused warnings */
    (void)(is_double + is_int + is_obj + is_special + is_pid);
    (void)(is_nil + is_bool + is_number);

    /* Test extraction for safe types */
    if (is_double) {
        double d = nanbox_as_double(v);
        (void)d;
    }
    if (is_int) {
        int64_t i = nanbox_as_int(v);
        (void)i;
    }
    if (is_pid) {
        uint64_t pid = nanbox_as_pid(v);
        (void)pid;
    }
    /* Don't try to extract objects as they may be invalid pointers */
}

/*
 * Test round-trip encoding/decoding.
 */
static void test_round_trip(const uint8_t *data, size_t size) {
    if (size < 8) return;

    /* Test integer round-trip */
    int64_t int_val;
    memcpy(&int_val, data, 8);

    /* Clamp to 48-bit signed range */
    int64_t clamped = int_val;
    if (clamped > 0x7FFFFFFFFFFFLL) clamped = 0x7FFFFFFFFFFFLL;
    if (clamped < -0x800000000000LL) clamped = -0x800000000000LL;

    NanValue encoded = nanbox_int(clamped);
    if (nanbox_is_int(encoded)) {
        int64_t decoded = nanbox_as_int(encoded);
        (void)decoded;
    }

    /* Test double round-trip */
    if (size >= 16) {
        double dbl_val;
        memcpy(&dbl_val, data + 8, 8);

        /* Skip NaN values since they have special behavior */
        if (dbl_val == dbl_val) {  /* Not NaN */
            NanValue dbl_encoded = nanbox_double(dbl_val);
            if (nanbox_is_double(dbl_encoded)) {
                double dbl_decoded = nanbox_as_double(dbl_encoded);
                (void)dbl_decoded;
            }
        }
    }

    /* Test PID round-trip */
    if (size >= 8) {
        uint64_t pid_val;
        memcpy(&pid_val, data, 8);
        pid_val &= NANBOX_PAYLOAD;  /* Ensure 48-bit */

        NanValue pid_encoded = nanbox_pid(pid_val);
        if (nanbox_is_pid(pid_encoded)) {
            uint64_t pid_decoded = nanbox_as_pid(pid_encoded);
            (void)pid_decoded;
        }
    }
}

/*
 * LLVMFuzzerTestOneInput - Entry point for libFuzzer
 *
 * This function is called by libFuzzer with random input data.
 * We interpret the input as one or more 64-bit patterns and test
 * the NaN-boxing type detection for each.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Need at least 8 bytes for one value */
    if (size < 8) {
        return 0;
    }

    /* Test each 8-byte chunk as a NanValue */
    size_t num_values = size / 8;
    for (size_t i = 0; i < num_values; i++) {
        NanValue v;
        memcpy(&v, data + (i * 8), 8);
        test_type_detection(v);
    }

    /* Test round-trip encoding */
    test_round_trip(data, size);

    /* Test special values */
    test_type_detection(NANBOX_NIL);
    test_type_detection(NANBOX_TRUE);
    test_type_detection(NANBOX_FALSE);

    return 0;
}
