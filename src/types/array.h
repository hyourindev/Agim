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

typedef struct Value Value;

/* Array Structure */

typedef struct Array {
    size_t length;
    size_t capacity;
    Value **items;
} Array;

/* Array Creation */

Value *value_array(void);
Value *value_array_with_capacity(size_t capacity);

/* Array Properties */

size_t array_length(const Value *v);
size_t array_capacity(const Value *v);
Value **array_data(const Value *v);

/* Array Access */

Value *array_get(const Value *v, size_t index);
Value *array_set(Value *v, size_t index, Value *item);

/* Array Modification */

Value *array_push(Value *v, Value *item);
Value *array_pop(Value *v, Value **arr_out);
Value *array_insert(Value *v, size_t index, Value *item);
Value *array_remove(Value *v, size_t index, Value **arr_out);
Value *array_clear(Value *v);

/* Array Operations */

Value *array_slice(const Value *v, size_t start, size_t end);
Value *array_concat(const Value *a, const Value *b);
int64_t array_find(const Value *v, const Value *item);
bool array_contains(const Value *v, const Value *item);
Value *array_reverse(Value *v);
Value *array_sort(Value *v);
Value *array_sort_by(Value *v, int (*compare)(const Value *, const Value *));

/* GC Integration */

typedef struct Heap Heap;

void array_set_gc_heap(Heap *heap);
Heap *array_get_gc_heap(void);

#endif /* AGIM_TYPES_ARRAY_H */
