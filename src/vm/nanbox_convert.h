/*
 * Agim - NaN Box Conversion Functions
 *
 * Converts between NanValue (packed 64-bit) and Value* (heap objects).
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_NANBOX_CONVERT_H
#define AGIM_VM_NANBOX_CONVERT_H

#include "vm/nanbox.h"
#include "vm/value.h"

static inline Value *nanbox_to_value(NanValue v) {
    if (nanbox_is_nil(v)) {
        return value_nil();
    } else if (nanbox_is_bool(v)) {
        return value_bool(nanbox_as_bool(v));
    } else if (nanbox_is_int(v)) {
        return value_int(nanbox_as_int(v));
    } else if (nanbox_is_double(v)) {
        return value_float(nanbox_as_double(v));
    } else if (nanbox_is_pid(v)) {
        return value_pid(nanbox_as_pid(v));
    } else if (nanbox_is_obj(v)) {
        return (Value *)nanbox_as_obj(v);
    }
    return value_nil();
}

static inline NanValue value_to_nanbox(Value *val) {
    if (!val) return NANBOX_NIL;

    switch (val->type) {
    case VAL_NIL:
        return NANBOX_NIL;
    case VAL_BOOL:
        return nanbox_bool(val->as.boolean);
    case VAL_INT:
        return nanbox_int(val->as.integer);
    case VAL_FLOAT:
        return nanbox_double(val->as.floating);
    case VAL_PID:
        return nanbox_pid(val->as.pid);
    default:
        return nanbox_obj(val);
    }
}

#endif /* AGIM_VM_NANBOX_CONVERT_H */
