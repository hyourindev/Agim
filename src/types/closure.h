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

/* Forward declarations */
typedef struct Value Value;
typedef struct Function Function;

/* NaN boxing type */
#include "vm/nanbox.h"

/*============================================================================
 * Upvalue Structure
 *============================================================================*/

/**
 * An upvalue is a reference to a captured variable.
 *
 * When open: points to a stack slot (location != NULL)
 * When closed: holds the value directly (location == NULL, use 'closed')
 *
 * With NaN boxing, both the stack slot and closed value are NanValue.
 */
typedef struct Upvalue {
    NanValue *location;         /* Pointer to stack slot (when open) */
    NanValue closed;            /* Closed-over value (when closed) */
    struct Upvalue *next;       /* Next open upvalue (linked list) */
} Upvalue;

/*============================================================================
 * Closure Structure
 *============================================================================*/

/**
 * A closure is a function with captured upvalues.
 */
typedef struct Closure {
    Function *function;         /* The wrapped function */
    Upvalue **upvalues;         /* Array of upvalue pointers */
    size_t upvalue_count;       /* Number of upvalues */
} Closure;

/*============================================================================
 * Value Constructors
 *============================================================================*/

/**
 * Create a closure value wrapping a function.
 * upvalue_count specifies how many upvalues to allocate.
 */
Value *value_closure(Function *function, size_t upvalue_count);

/**
 * Check if value is a closure.
 */
bool value_is_closure(const Value *v);

/*============================================================================
 * Upvalue Operations
 *============================================================================*/

/**
 * Create a new open upvalue pointing to a stack slot.
 */
Upvalue *upvalue_new(NanValue *slot);

/**
 * Free an upvalue.
 */
void upvalue_free(Upvalue *upvalue);

/**
 * Close an upvalue by copying the value into the upvalue itself.
 * Called when the variable goes out of scope.
 */
void upvalue_close(Upvalue *upvalue);

/**
 * Check if an upvalue is open (still pointing to stack).
 */
bool upvalue_is_open(const Upvalue *upvalue);

/**
 * Get the NaN-boxed value of an upvalue (works for both open and closed).
 */
NanValue upvalue_get_nan(const Upvalue *upvalue);

/**
 * Set the NaN-boxed value of an upvalue (works for both open and closed).
 */
void upvalue_set_nan(Upvalue *upvalue, NanValue value);

/**
 * Get the value of an upvalue as Value* (compatibility, allocates for primitives).
 */
Value *upvalue_get(const Upvalue *upvalue);

/**
 * Set the value of an upvalue from Value* (compatibility).
 */
void upvalue_set(Upvalue *upvalue, Value *value);

/*============================================================================
 * Closure Operations
 *============================================================================*/

/**
 * Get the function from a closure value.
 * Returns NULL if not a closure.
 */
Function *closure_function(const Value *v);

/**
 * Get an upvalue from a closure by index.
 * Returns NULL if out of bounds or not a closure.
 */
Upvalue *closure_get_upvalue(const Value *v, size_t index);

/**
 * Set an upvalue in a closure by index.
 * No-op if out of bounds or not a closure.
 */
void closure_set_upvalue(Value *v, size_t index, Upvalue *upvalue);

/**
 * Get the number of upvalues in a closure.
 * Returns 0 if not a closure.
 */
size_t closure_upvalue_count(const Value *v);

#endif /* AGIM_TYPES_CLOSURE_H */
