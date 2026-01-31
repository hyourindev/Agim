/*
 * Agim - Closure Type Operations
 *
 * Closures capture variables from enclosing scopes.
 * Upvalues provide the mechanism for variable capture.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_TYPES_CLOSURE_H
#define AGIM_TYPES_CLOSURE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Value Value;
typedef struct Function Function;

#include "vm/nanbox.h"

/* Upvalue Structure */

/*
 * When open: points to a stack slot (location != NULL)
 * When closed: holds the value directly (location == NULL)
 */
typedef struct Upvalue {
    NanValue *location;
    NanValue closed;
    struct Upvalue *next;
} Upvalue;

/* Closure Structure */

typedef struct Closure {
    Function *function;
    Upvalue **upvalues;
    size_t upvalue_count;
} Closure;

/* Value Constructors */

Value *value_closure(Function *function, size_t upvalue_count);
bool value_is_closure(const Value *v);

/* Upvalue Operations */

Upvalue *upvalue_new(NanValue *slot);
void upvalue_free(Upvalue *upvalue);
void upvalue_close(Upvalue *upvalue);
bool upvalue_is_open(const Upvalue *upvalue);
NanValue upvalue_get_nan(const Upvalue *upvalue);
void upvalue_set_nan(Upvalue *upvalue, NanValue value);
Value *upvalue_get(const Upvalue *upvalue);
void upvalue_set(Upvalue *upvalue, Value *value);

/* Closure Operations */

Function *closure_function(const Value *v);
Upvalue *closure_get_upvalue(const Value *v, size_t index);
void closure_set_upvalue(Value *v, size_t index, Upvalue *upvalue);
size_t closure_upvalue_count(const Value *v);

#endif /* AGIM_TYPES_CLOSURE_H */
