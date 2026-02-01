/*
 * Agim - Array Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "types/array.h"
#include "vm/value.h"
#include "vm/gc.h"
#include "util/alloc.h"
#include "debug/log.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

void array_set_gc_heap(Heap *heap) {
    gc_set_current_heap(heap);
}

Heap *array_get_gc_heap(void) {
    return gc_get_current_heap();
}

/* Array Creation */

Value *value_array_with_capacity(size_t capacity) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) {
        LOG_ERROR("array: failed to allocate Value");
        return NULL;
    }

    v->type = VAL_ARRAY;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    Array *arr = agim_alloc(sizeof(Array));
    if (!arr) {
        LOG_ERROR("array: failed to allocate Array struct");
        agim_free(v);
        return NULL;
    }
    arr->length = 0;
    arr->capacity = capacity > 0 ? capacity : 8;
    arr->items = agim_alloc(sizeof(Value *) * arr->capacity);
    if (!arr->items) {
        LOG_ERROR("array: failed to allocate items buffer for capacity %zu", arr->capacity);
        agim_free(arr);
        agim_free(v);
        return NULL;
    }

    v->as.array = arr;
    return v;
}

Value *value_array(void) {
    return value_array_with_capacity(8);
}

/* Array Properties */

size_t array_length(const Value *v) {
    if (!v || v->type != VAL_ARRAY) return 0;
    return v->as.array->length;
}

size_t array_capacity(const Value *v) {
    if (!v || v->type != VAL_ARRAY) return 0;
    return v->as.array->capacity;
}

Value **array_data(const Value *v) {
    if (!v || v->type != VAL_ARRAY) return NULL;
    return v->as.array->items;
}

/* Internal Helpers */

static bool array_ensure_capacity(Array *arr, size_t needed);
static Value *array_ensure_writable(Value *v);

/* Array Access */

Value *array_get(const Value *v, size_t index) {
    if (!v || v->type != VAL_ARRAY) return NULL;
    Array *arr = v->as.array;
    if (index >= arr->length) return NULL;
    return arr->items[index];
}

Value *array_set(Value *v, size_t index, Value *item) {
    if (!v || v->type != VAL_ARRAY) return v;

    Value *writable = array_ensure_writable(v);
    if (!writable || writable->type != VAL_ARRAY) return writable;

    Array *arr = writable->as.array;
    if (index >= arr->length) return writable;

    Heap *heap = gc_get_current_heap();
    if (heap) {
        gc_write_barrier(heap, writable, item);
    }

    /* Release the old value being replaced */
    if (arr->items[index]) {
        value_free(arr->items[index]);
    }

    arr->items[index] = item;
    return writable;
}

static bool array_ensure_capacity(Array *arr, size_t needed) {
    if (needed <= arr->capacity) return true;

    size_t new_cap = arr->capacity;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2) {
            return false;
        }
        new_cap *= 2;
    }

    if (new_cap > SIZE_MAX / sizeof(Value *)) {
        return false;
    }

    Value **new_items = agim_realloc(arr->items, sizeof(Value *) * new_cap);
    if (!new_items) return false;

    arr->items = new_items;
    arr->capacity = new_cap;
    return true;
}

static Value *array_ensure_writable(Value *v) {
    if (!v || v->type != VAL_ARRAY) return v;

    if (!value_needs_cow(v)) {
        return v;
    }

    Array *old = v->as.array;

    Value *new_v = agim_alloc(sizeof(Value));
    if (!new_v) return NULL;

    Array *new_arr = agim_alloc(sizeof(Array));
    if (!new_arr) {
        agim_free(new_v);
        return NULL;
    }

    Value **items = agim_alloc(sizeof(Value *) * old->capacity);
    if (!items) {
        agim_free(new_arr);
        agim_free(new_v);
        return NULL;
    }

    new_v->type = VAL_ARRAY;
    atomic_store_explicit(&new_v->refcount, 1, memory_order_relaxed);
    new_v->flags = 0;
    new_v->gc_state = 0;
    new_v->next = NULL;

    new_arr->length = old->length;
    new_arr->capacity = old->capacity;
    new_arr->items = items;

    for (size_t i = 0; i < old->length; i++) {
        new_arr->items[i] = value_retain(old->items[i]);
    }

    new_v->as.array = new_arr;

    value_release(v);

    return new_v;
}

/* Array Modification */

Value *array_push(Value *v, Value *item) {
    if (!v || v->type != VAL_ARRAY) return v;

    Value *writable = array_ensure_writable(v);
    if (!writable || writable->type != VAL_ARRAY) return writable;

    Array *arr = writable->as.array;

    Heap *heap = gc_get_current_heap();
    if (heap) {
        gc_write_barrier(heap, writable, item);
    }

    if (!array_ensure_capacity(arr, arr->length + 1)) {
        return writable;
    }
    arr->items[arr->length++] = item;
    return writable;
}

Value *array_pop(Value *v, Value **arr_out) {
    if (!v || v->type != VAL_ARRAY) {
        if (arr_out) *arr_out = v;
        return NULL;
    }

    Value *writable = array_ensure_writable(v);
    if (arr_out) *arr_out = writable;

    if (!writable || writable->type != VAL_ARRAY) return NULL;

    Array *arr = writable->as.array;
    if (arr->length == 0) return NULL;
    return arr->items[--arr->length];
}

Value *array_insert(Value *v, size_t index, Value *item) {
    if (!v || v->type != VAL_ARRAY) return v;

    Value *writable = array_ensure_writable(v);
    if (!writable || writable->type != VAL_ARRAY) return writable;

    Array *arr = writable->as.array;

    Heap *heap = gc_get_current_heap();
    if (heap) {
        gc_write_barrier(heap, writable, item);
    }

    if (index > arr->length) {
        index = arr->length;
    }

    if (!array_ensure_capacity(arr, arr->length + 1)) {
        return writable;
    }

    if (index < arr->length) {
        memmove(&arr->items[index + 1], &arr->items[index],
                sizeof(Value *) * (arr->length - index));
    }

    arr->items[index] = item;
    arr->length++;
    return writable;
}

Value *array_remove(Value *v, size_t index, Value **arr_out) {
    if (!v || v->type != VAL_ARRAY) {
        if (arr_out) *arr_out = v;
        return NULL;
    }

    Value *writable = array_ensure_writable(v);
    if (arr_out) *arr_out = writable;

    if (!writable || writable->type != VAL_ARRAY) return NULL;

    Array *arr = writable->as.array;
    if (index >= arr->length) return NULL;

    Value *removed = arr->items[index];

    if (index < arr->length - 1) {
        memmove(&arr->items[index], &arr->items[index + 1],
                sizeof(Value *) * (arr->length - index - 1));
    }

    arr->length--;
    return removed;
}

Value *array_clear(Value *v) {
    if (!v || v->type != VAL_ARRAY) return v;

    Value *writable = array_ensure_writable(v);
    if (writable && writable->type == VAL_ARRAY) {
        Array *arr = writable->as.array;
        /* Free all elements before clearing */
        for (size_t i = 0; i < arr->length; i++) {
            if (arr->items[i]) {
                value_free(arr->items[i]);
            }
        }
        arr->length = 0;
    }
    return writable;
}

/* Array Operations */

Value *array_slice(const Value *v, size_t start, size_t end) {
    if (!v || v->type != VAL_ARRAY) {
        return value_nil();
    }

    Array *arr = v->as.array;
    if (start > arr->length) start = arr->length;
    if (end > arr->length) end = arr->length;
    if (start > end) start = end;

    Value *result = value_array_with_capacity(end - start);
    for (size_t i = start; i < end; i++) {
        array_push(result, value_retain(arr->items[i]));
    }
    return result;
}

Value *array_concat(const Value *a, const Value *b) {
    if (!a || a->type != VAL_ARRAY || !b || b->type != VAL_ARRAY) {
        return value_array();
    }

    size_t len_a = a->as.array->length;
    size_t len_b = b->as.array->length;

    Value *result = value_array_with_capacity(len_a + len_b);

    for (size_t i = 0; i < len_a; i++) {
        array_push(result, value_retain(a->as.array->items[i]));
    }
    for (size_t i = 0; i < len_b; i++) {
        array_push(result, value_retain(b->as.array->items[i]));
    }

    return result;
}

int64_t array_find(const Value *v, const Value *item) {
    if (!v || v->type != VAL_ARRAY) return -1;
    Array *arr = v->as.array;

    for (size_t i = 0; i < arr->length; i++) {
        if (value_equals(arr->items[i], item)) {
            return (int64_t)i;
        }
    }

    return -1;
}

bool array_contains(const Value *v, const Value *item) {
    return array_find(v, item) >= 0;
}

Value *array_reverse(Value *v) {
    if (!v || v->type != VAL_ARRAY) return v;

    Value *writable = array_ensure_writable(v);
    if (!writable || writable->type != VAL_ARRAY) return writable;

    Array *arr = writable->as.array;

    size_t left = 0;
    size_t right = arr->length > 0 ? arr->length - 1 : 0;

    while (left < right) {
        Value *temp = arr->items[left];
        arr->items[left] = arr->items[right];
        arr->items[right] = temp;
        left++;
        right--;
    }
    return writable;
}

/* Sorting
 *
 * Uses thread-local storage for the comparator to enable safe concurrent sorts.
 * This pattern is consistent with tls_current_heap in gc.c and tls_current_alloc
 * in worker_alloc.c.
 */

static _Thread_local int (*tls_custom_compare)(const Value *, const Value *) = NULL;

static int qsort_compare(const void *a, const void *b) {
    Value *va = *(Value **)a;
    Value *vb = *(Value **)b;

    if (tls_custom_compare) {
        return tls_custom_compare(va, vb);
    }

    return value_compare(va, vb);
}

Value *array_sort(Value *v) {
    return array_sort_by(v, NULL);
}

Value *array_sort_by(Value *v, int (*compare)(const Value *, const Value *)) {
    if (!v || v->type != VAL_ARRAY) return v;

    Value *writable = array_ensure_writable(v);
    if (!writable || writable->type != VAL_ARRAY) return writable;

    Array *arr = writable->as.array;
    if (arr->length < 2) return writable;

    tls_custom_compare = compare;  /* Thread-local, no race */
    qsort(arr->items, arr->length, sizeof(Value *), qsort_compare);
    tls_custom_compare = NULL;     /* Clean up */
    return writable;
}
