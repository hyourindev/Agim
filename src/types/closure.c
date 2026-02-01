/*
 * Agim - Closure Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "types/closure.h"
#include "vm/value.h"
#include "vm/gc.h"
#include "vm/nanbox_convert.h"
#include "util/alloc.h"

#include <stdatomic.h>
#include <string.h>

void closure_set_gc_heap(Heap *heap) {
    gc_set_current_heap(heap);
}

Heap *closure_get_gc_heap(void) {
    return gc_get_current_heap();
}

/* Value Constructors */

Value *value_closure(Function *function, size_t upvalue_count) {
    if (!function) return value_nil();

    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;

    Closure *closure = agim_alloc(sizeof(Closure));
    if (!closure) {
        agim_free(v);
        return NULL;
    }

    Upvalue **upvalues = NULL;
    if (upvalue_count > 0) {
        upvalues = agim_alloc(sizeof(Upvalue *) * upvalue_count);
        if (!upvalues) {
            agim_free(closure);
            agim_free(v);
            return NULL;
        }
        memset(upvalues, 0, sizeof(Upvalue *) * upvalue_count);
    }

    v->type = VAL_CLOSURE;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    closure->function = function;
    closure->upvalue_count = upvalue_count;
    closure->upvalues = upvalues;

    v->as.closure = closure;
    return v;
}

bool value_is_closure(const Value *v) {
    return v && v->type == VAL_CLOSURE;
}

/* Upvalue Operations */

Upvalue *upvalue_new(NanValue *slot) {
    Upvalue *upvalue = agim_alloc(sizeof(Upvalue));
    if (!upvalue) return NULL;
    upvalue->location = slot;
    upvalue->closed = NANBOX_NIL;
    upvalue->next = NULL;
    return upvalue;
}

void upvalue_free(Upvalue *upvalue) {
    if (!upvalue) return;
    agim_free(upvalue);
}

void upvalue_close(Upvalue *upvalue) {
    if (!upvalue || !upvalue->location) return;

    upvalue->closed = *upvalue->location;
    upvalue->location = NULL;
}

bool upvalue_is_open(const Upvalue *upvalue) {
    return upvalue && upvalue->location != NULL;
}

NanValue upvalue_get_nan(const Upvalue *upvalue) {
    if (!upvalue) return NANBOX_NIL;

    if (upvalue->location) {
        return *upvalue->location;
    } else {
        return upvalue->closed;
    }
}

void upvalue_set_nan(Upvalue *upvalue, NanValue value) {
    if (!upvalue) return;

    if (upvalue->location) {
        *upvalue->location = value;
    } else {
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

/* Closure Operations */

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
