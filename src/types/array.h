/*
 * Agim - Array Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_TYPES_ARRAY_H
#define AGIM_TYPES_ARRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration */
typedef struct Value Value;

/*============================================================================
 * Array Structure
 *============================================================================*/

/**
 * Dynamic array of values.
 */
typedef struct Array {
    size_t length;      /* Number of elements */
    size_t capacity;    /* Allocated capacity */
    Value **items;      /* Array of value pointers */
} Array;

/*============================================================================
 * Array Creation
 *============================================================================*/

/**
 * Create an empty array.
 */
Value *value_array(void);

/**
 * Create an array with preallocated capacity.
 */
Value *value_array_with_capacity(size_t capacity);

/*============================================================================
 * Array Properties
 *============================================================================*/

/**
 * Get the length of an array.
 * Returns 0 if not an array.
 */
size_t array_length(const Value *v);

/**
 * Get the capacity of an array.
 * Returns 0 if not an array.
 */
size_t array_capacity(const Value *v);

/**
 * Get the raw items pointer.
 * Returns NULL if not an array.
 */
Value **array_data(const Value *v);

/*============================================================================
 * Array Access
 *============================================================================*/

/**
 * Get element at index.
 * Returns NULL if out of bounds or not an array.
 */
Value *array_get(const Value *v, size_t index);

/**
 * Set element at index.
 * Returns the (possibly new) Value to use after COW.
 * Does nothing if out of bounds or not an array.
 */
Value *array_set(Value *v, size_t index, Value *item);

/*============================================================================
 * Array Modification
 *============================================================================*/

/**
 * Append element to end of array.
 * Returns the (possibly new) Value to use after COW.
 */
Value *array_push(Value *v, Value *item);

/**
 * Remove and return last element.
 * Updates *arr_out to the (possibly new) Value after COW.
 * Returns the popped element, or NULL if empty or not an array.
 */
Value *array_pop(Value *v, Value **arr_out);

/**
 * Insert element at index, shifting elements right.
 * Returns the (possibly new) Value to use after COW.
 */
Value *array_insert(Value *v, size_t index, Value *item);

/**
 * Remove element at index, shifting elements left.
 * Updates *arr_out to the (possibly new) Value after COW.
 * Returns the removed element, or NULL if out of bounds.
 */
Value *array_remove(Value *v, size_t index, Value **arr_out);

/**
 * Remove all elements (but keep capacity).
 * Returns the (possibly new) Value to use after COW.
 */
Value *array_clear(Value *v);

/*============================================================================
 * Array Operations
 *============================================================================*/

/**
 * Extract a subarray (slice).
 * Returns nil if not an array.
 */
Value *array_slice(const Value *v, size_t start, size_t end);

/**
 * Concatenate two arrays.
 * Returns a new array.
 */
Value *array_concat(const Value *a, const Value *b);

/**
 * Find first occurrence of item.
 * Returns -1 if not found.
 */
int64_t array_find(const Value *v, const Value *item);

/**
 * Check if array contains item.
 */
bool array_contains(const Value *v, const Value *item);

/**
 * Reverse array in place.
 * Returns the (possibly new) Value to use after COW.
 */
Value *array_reverse(Value *v);

/**
 * Sort array in place using default comparison.
 * Returns the (possibly new) Value to use after COW.
 */
Value *array_sort(Value *v);

/**
 * Sort array in place using custom comparator.
 * Returns the (possibly new) Value to use after COW.
 */
Value *array_sort_by(Value *v, int (*compare)(const Value *, const Value *));

/*============================================================================
 * GC Integration
 *============================================================================*/

/* Forward declaration for Heap */
typedef struct Heap Heap;

/**
 * Set the thread-local heap for write barriers.
 * Called by VM at start of execution.
 */
void array_set_gc_heap(Heap *heap);

/**
 * Get the current thread-local heap.
 */
Heap *array_get_gc_heap(void);

#endif /* AGIM_TYPES_ARRAY_H */
