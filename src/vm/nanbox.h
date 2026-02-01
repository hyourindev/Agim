/*
 * Agim - NaN Boxing Value Representation
 *
 * Packs all values into 8 bytes using IEEE 754 quiet NaN space.
 * Eliminates heap allocation for primitives.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_NANBOX_H
#define AGIM_VM_NANBOX_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * NaN Boxing Layout:
 *
 * IEEE 754 double: [sign:1][exponent:11][mantissa:52]
 * A quiet NaN has exponent all 1s and bit 51 set.
 *
 * Tag encoding (bits 48-50 after quiet NaN prefix):
 *   QNAN + 0x0004 = Integer (48-bit signed)
 *   QNAN + 0x0005 = Object pointer (Value*)
 *   QNAN + 0x0006 = Special (nil, true, false)
 *   QNAN + 0x0007 = PID (48-bit process ID)
 */

typedef uint64_t NanValue;

/* Bit Patterns */

#define NANBOX_QNAN     0x7FFC000000000000ULL
#define NANBOX_PAYLOAD  0x0000FFFFFFFFFFFFULL

#define NANBOX_TAG_INT     0x7FFC000000000000ULL
#define NANBOX_TAG_OBJ     0x7FFD000000000000ULL
#define NANBOX_TAG_SPECIAL 0x7FFE000000000000ULL
#define NANBOX_TAG_PID     0x7FFF000000000000ULL

#define NANBOX_TAG_MASK    0xFFFF000000000000ULL

#define NANBOX_NIL   (NANBOX_TAG_SPECIAL | 1ULL)
#define NANBOX_TRUE  (NANBOX_TAG_SPECIAL | 2ULL)
#define NANBOX_FALSE (NANBOX_TAG_SPECIAL | 3ULL)

/* Type Checking */

static inline bool nanbox_is_double(NanValue v) {
    uint64_t tag = v & 0xFFFC000000000000ULL;
    return tag != 0x7FFC000000000000ULL;
}

static inline bool nanbox_is_int(NanValue v) {
    return (v & NANBOX_TAG_MASK) == NANBOX_TAG_INT;
}

static inline bool nanbox_is_obj(NanValue v) {
    return (v & NANBOX_TAG_MASK) == NANBOX_TAG_OBJ;
}

static inline bool nanbox_is_special(NanValue v) {
    return (v & NANBOX_TAG_MASK) == NANBOX_TAG_SPECIAL;
}

static inline bool nanbox_is_pid(NanValue v) {
    return (v & NANBOX_TAG_MASK) == NANBOX_TAG_PID;
}

static inline bool nanbox_is_nil(NanValue v) {
    return v == NANBOX_NIL;
}

static inline bool nanbox_is_true(NanValue v) {
    return v == NANBOX_TRUE;
}

static inline bool nanbox_is_false(NanValue v) {
    return v == NANBOX_FALSE;
}

static inline bool nanbox_is_bool(NanValue v) {
    return v == NANBOX_TRUE || v == NANBOX_FALSE;
}

static inline bool nanbox_is_number(NanValue v) {
    return nanbox_is_double(v) || nanbox_is_int(v);
}

/* Value Encoding */

static inline NanValue nanbox_double(double d) {
    NanValue v;
    memcpy(&v, &d, sizeof(v));
    return v;
}

static inline NanValue nanbox_int(int64_t i) {
    return NANBOX_TAG_INT | ((uint64_t)i & NANBOX_PAYLOAD);
}

static inline NanValue nanbox_obj(void *ptr) {
    return NANBOX_TAG_OBJ | ((uint64_t)(uintptr_t)ptr & NANBOX_PAYLOAD);
}

static inline NanValue nanbox_pid(uint64_t pid) {
    return NANBOX_TAG_PID | (pid & NANBOX_PAYLOAD);
}

static inline NanValue nanbox_bool(bool b) {
    return b ? NANBOX_TRUE : NANBOX_FALSE;
}

#define NANBOX_VAL_NIL   NANBOX_NIL
#define NANBOX_VAL_TRUE  NANBOX_TRUE
#define NANBOX_VAL_FALSE NANBOX_FALSE

/* Value Decoding */

static inline double nanbox_as_double(NanValue v) {
    double d;
    memcpy(&d, &v, sizeof(d));
    return d;
}

static inline int64_t nanbox_as_int(NanValue v) {
    int64_t payload = (int64_t)(v & NANBOX_PAYLOAD);
    if (payload & 0x800000000000ULL) {
        payload |= 0xFFFF000000000000ULL;
    }
    return payload;
}

static inline void *nanbox_as_obj(NanValue v) {
    return (void *)(uintptr_t)(v & NANBOX_PAYLOAD);
}

static inline uint64_t nanbox_as_pid(NanValue v) {
    return v & NANBOX_PAYLOAD;
}

static inline bool nanbox_as_bool(NanValue v) {
    return v == NANBOX_TRUE;
}

/* Numeric Coercion */

static inline double nanbox_to_float(NanValue v) {
    if (nanbox_is_double(v)) return nanbox_as_double(v);
    if (nanbox_is_int(v)) return (double)nanbox_as_int(v);
    return 0.0;
}

static inline int64_t nanbox_to_int(NanValue v) {
    if (nanbox_is_int(v)) return nanbox_as_int(v);
    if (nanbox_is_double(v)) return (int64_t)nanbox_as_double(v);
    return 0;
}

/* Truthiness */

static inline bool nanbox_is_truthy(NanValue v) {
    if (v == NANBOX_NIL || v == NANBOX_FALSE) return false;
    if (nanbox_is_int(v)) return nanbox_as_int(v) != 0;
    if (nanbox_is_double(v)) return nanbox_as_double(v) != 0.0;
    return true;
}

/* Equality */

static inline bool nanbox_equal(NanValue a, NanValue b) {
    /* Handle NaN: NaN != NaN per IEEE 754 */
    if (nanbox_is_double(a) && nanbox_is_double(b)) {
        double da = nanbox_as_double(a);
        double db = nanbox_as_double(b);
        /* NaN comparison always returns false */
        return da == db;
    }
    /* For non-doubles, bit equality is fine */
    if (a == b) return true;
    /* Mixed int/float comparison */
    if (nanbox_is_number(a) && nanbox_is_number(b)) {
        return nanbox_to_float(a) == nanbox_to_float(b);
    }
    return false;
}

#endif /* AGIM_VM_NANBOX_H */
