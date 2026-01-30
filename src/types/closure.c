/*
 * Agim - Closure Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "types/closure.h"
#include "vm/value.h"
#include "vm/gc.h"
#include "util/alloc.h"

#include <stdatomic.h>
#include <string.h>

/*
 * Thread-local heap reference for write barriers.
 * Set by the VM during execution.
 */
static _Thread_local Heap *tls_current_heap = NULL;

void closure_set_gc_heap(Heap *heap) {
    tls_current_heap = heap;
}

Heap *closure_get_gc_heap(void) {
    return tls_current_heap;
}

/*============================================================================
 * NaN Boxing Helpers
 *============================================================================*/

/**
 * Convert a NanValue to a Value* (allocates for primitives).
 */
static Value *nanbox_to_value(NanValue v) {
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

/**
 * Convert a Value* to a NanValue.
 */
static NanValue value_to_nanbox(Value *val) {
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
        /* Heap objects: encode pointer to Value */
        return nanbox_obj(val);
    }
}

/*============================================================================
 * Value Constructors
 *============================================================================*/

Value *value_closure(Function *function, size_t upvalue_count) {
    if (!function) return value_nil();

    Value *v = agim_alloc(sizeof(Value));
    v->type = VAL_CLOSURE;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;  /* Closures are mutable (capture state) */
    v->gc_state = 0;
    v->next = NULL;

    Closure *closure = agim_alloc(sizeof(Closure));
    closure->function = function;
    closure->upvalue_count = upvalue_count;

    if (upvalue_count > 0) {
        closure->upvalues = agim_alloc(sizeof(Upvalue *) * upvalue_count);
        memset(closure->upvalues, 0, sizeof(Upvalue *) * upvalue_count);
    } else {
        closure->upvalues = NULL;
    }

    v->as.closure = closure;
    return v;
}

bool value_is_closure(const Value *v) {
    return v && v->type == VAL_CLOSURE;
}

/*============================================================================
 * Upvalue Operations
 *============================================================================*/

Upvalue *upvalue_new(NanValue *slot) {
    Upvalue *upvalue = agim_alloc(sizeof(Upvalue));
    upvalue->location = slot;
    upvalue->closed = NANBOX_NIL;
    upvalue->next = NULL;
    return upvalue;
}

void upvalue_free(Upvalue *upvalue) {
    if (!upvalue) return;
    /* Note: Values referenced by closed are managed by GC */
    agim_free(upvalue);
}

void upvalue_close(Upvalue *upvalue) {
    if (!upvalue || !upvalue->location) return;

    /* Copy the NanValue from the stack into the upvalue */
    upvalue->closed = *upvalue->location;
    upvalue->location = NULL;
}

bool upvalue_is_open(const Upvalue *upvalue) {
    return upvalue && upvalue->location != NULL;
}

NanValue upvalue_get_nan(const Upvalue *upvalue) {
    if (!upvalue) return NANBOX_NIL;

    if (upvalue->location) {
        /* Open: read from stack */
        return *upvalue->location;
    } else {
        /* Closed: read from upvalue */
        return upvalue->closed;
    }
}

void upvalue_set_nan(Upvalue *upvalue, NanValue value) {
    if (!upvalue) return;

    if (upvalue->location) {
        /* Open: write to stack */
        *upvalue->location = value;
    } else {
        /* Closed: write to upvalue */
        upvalue->closed = value;
    }
}

Value *upvalue_get(const Upvalue *upvalue) {
    NanValue v = upvalue_get_nan(upvalue);
    return nanbox_to_value(v);
}

void upvalue_set(Upvalue *upvalue, Value *value) {
    NanValue v = value_to_nanbox(value);
    upvalue_set_nan(upvalue, v);
}

/*============================================================================
 * Closure Operations
 *============================================================================*/

Function *closure_function(const Value *v) {
    if (!v || v->type != VAL_CLOSURE) return NULL;
    Closure *closure = (Closure *)v->as.closure;
    return closure->function;
}

Upvalue *closure_get_upvalue(const Value *v, size_t index) {
    if (!v || v->type != VAL_CLOSURE) return NULL;
    Closure *closure = (Closure *)v->as.closure;
    if (index >= closure->upvalue_count) return NULL;
    return closure->upvalues[index];
}

void closure_set_upvalue(Value *v, size_t index, Upvalue *upvalue) {
    if (!v || v->type != VAL_CLOSURE) return;
    Closure *closure = (Closure *)v->as.closure;
    if (index >= closure->upvalue_count) return;
    closure->upvalues[index] = upvalue;
}

size_t closure_upvalue_count(const Value *v) {
    if (!v || v->type != VAL_CLOSURE) return 0;
    Closure *closure = (Closure *)v->as.closure;
    return closure->upvalue_count;
}
