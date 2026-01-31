/*
 * Agim - Garbage Collector
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "vm/gc.h"
#include "vm/nanbox.h"
#include "types/closure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Thread-local heap for write barriers */

static _Thread_local Heap *tls_current_heap = NULL;

void gc_set_current_heap(Heap *heap) {
    tls_current_heap = heap;
}

Heap *gc_get_current_heap(void) {
    return tls_current_heap;
}

/* Default Configuration */

GCConfig gc_config_default(void) {
    return (GCConfig){
        .initial_heap_size = 16 * 1024,
        .max_heap_size = 1 * 1024 * 1024,
        .growth_factor = 1.5f,
        .gc_threshold = 0.75f,
        .incremental_step = 100,
        .max_remember_size = 1024,
    };
}

/* Heap Lifecycle */

Heap *heap_new(const GCConfig *config) {
    Heap *heap = malloc(sizeof(Heap));
    if (!heap) return NULL;

    heap->objects = NULL;
    heap->bytes_allocated = 0;
    heap->next_gc = config ? config->initial_heap_size : gc_config_default().initial_heap_size;
    heap->max_size = config ? config->max_heap_size : gc_config_default().max_heap_size;

    heap->gc_phase = GC_IDLE;
    heap->mark_cursor = NULL;
    heap->sweep_cursor = NULL;
    heap->sweep_prev = NULL;
    heap->step_budget = config ? config->incremental_step : 100;

    /* Gray list for tri-color marking */
    heap->gray_list = NULL;
    heap->gray_count = 0;
    heap->gray_capacity = 0;

    heap->generational_enabled = true;
    heap->young_count = 0;
    heap->old_count = 0;
    heap->young_bytes = 0;
    heap->old_bytes = 0;
    heap->remember_set = NULL;
    heap->remember_count = 0;
    heap->remember_capacity = 0;
    heap->max_remember_size = config ? config->max_remember_size : 1024;
    heap->needs_full_gc = false;
    heap->promotion_threshold = 2;
    heap->young_gc_threshold = config ? config->initial_heap_size / 4 : 4096;
    heap->minor_gc_count = 0;
    heap->major_gc_count = 0;

    heap->total_allocated = 0;
    heap->total_freed = 0;
    heap->gc_count = 0;

    return heap;
}

void heap_free(Heap *heap) {
    if (!heap) return;

    Value *object = heap->objects;
    while (object) {
        Value *next = object->next;
        value_free(object);
        object = next;
    }

    free(heap->remember_set);
    free(heap->gray_list);
    free(heap);
}

/* Allocation */

static size_t value_size(ValueType type) {
    /* Use base sizes without conservative padding to avoid excessive GC triggers.
     * Actual allocation sizes are tracked at allocation time where applicable. */
    switch (type) {
    case VAL_NIL:
    case VAL_BOOL:
    case VAL_INT:
    case VAL_FLOAT:
    case VAL_PID:
        return sizeof(Value);
    case VAL_STRING:
        return sizeof(Value) + sizeof(String);  /* Base only, actual tracked elsewhere */
    case VAL_ARRAY:
        return sizeof(Value) + sizeof(Array);   /* Base only */
    case VAL_MAP:
        return sizeof(Value) + sizeof(Map);     /* Base only */
    case VAL_FUNCTION:
        return sizeof(Value) + sizeof(Function);
    case VAL_BYTES:
        return sizeof(Value) + sizeof(Bytes);   /* Base only */
    default:
        return sizeof(Value);
    }
}

Value *heap_alloc_with_gc(Heap *heap, ValueType type, VM *vm) {
    size_t size = value_size(type);

    if (heap->needs_full_gc && vm) {
        gc_collect_full(heap, vm);
        heap->needs_full_gc = false;
    }

    if (heap->generational_enabled && vm &&
        heap->young_bytes + size > heap->young_gc_threshold) {
        gc_collect_young(heap, vm);
    }

    if (heap->bytes_allocated + size > heap->next_gc) {
        if (vm) {
            if (heap->generational_enabled) {
                gc_collect_full(heap, vm);
            } else {
                gc_collect(heap, vm);
            }
        } else {
            if (heap->next_gc < heap->max_size) {
                heap->next_gc = (size_t)(heap->next_gc * 1.5f);
                if (heap->next_gc > heap->max_size) {
                    heap->next_gc = heap->max_size;
                }
            }
        }
    }

    if (heap->bytes_allocated + size > heap->max_size) {
        if (vm) {
            if (heap->generational_enabled) {
                gc_collect_full(heap, vm);
            } else {
                gc_collect(heap, vm);
            }
        }
        if (heap->bytes_allocated + size > heap->max_size) {
            fprintf(stderr, "agim: heap limit exceeded\n");
            return NULL;
        }
    }

    Value *value = NULL;
    switch (type) {
    case VAL_NIL:
        value = value_nil();
        break;
    case VAL_BOOL:
        value = value_bool(false);
        break;
    case VAL_INT:
        value = value_int(0);
        break;
    case VAL_FLOAT:
        value = value_float(0.0);
        break;
    case VAL_STRING:
        value = value_string("");
        break;
    case VAL_ARRAY:
        value = value_array();
        break;
    case VAL_MAP:
        value = value_map();
        break;
    case VAL_PID:
        value = value_pid(0);
        break;
    case VAL_FUNCTION:
        value = value_function(NULL, 0);
        break;
    case VAL_BYTES:
        value = value_bytes(64);
        break;
    case VAL_VECTOR:
        value = value_vector(1);
        break;
    case VAL_CLOSURE:
    case VAL_RESULT:
    case VAL_OPTION:
    case VAL_STRUCT:
    case VAL_ENUM:
        return NULL;
    }

    if (!value) return NULL;

    value->gc_state = 0;
    value->next = heap->objects;
    heap->objects = value;

    heap->bytes_allocated += size;
    heap->total_allocated += size;

    if (heap->generational_enabled) {
        heap->young_count++;
        heap->young_bytes += size;
    }

    return value;
}

Value *heap_alloc(Heap *heap, ValueType type) {
    size_t size = value_size(type);

    if (heap->bytes_allocated + size > heap->next_gc) {
        if (heap->next_gc < heap->max_size) {
            heap->next_gc = (size_t)(heap->next_gc * 1.5f);
            if (heap->next_gc > heap->max_size) {
                heap->next_gc = heap->max_size;
            }
        }
    }

    if (heap->bytes_allocated + size > heap->max_size) {
        fprintf(stderr, "agim: heap limit exceeded\n");
        return NULL;
    }

    Value *value = NULL;
    switch (type) {
    case VAL_NIL:
        value = value_nil();
        break;
    case VAL_BOOL:
        value = value_bool(false);
        break;
    case VAL_INT:
        value = value_int(0);
        break;
    case VAL_FLOAT:
        value = value_float(0.0);
        break;
    case VAL_STRING:
        value = value_string("");
        break;
    case VAL_ARRAY:
        value = value_array();
        break;
    case VAL_MAP:
        value = value_map();
        break;
    case VAL_PID:
        value = value_pid(0);
        break;
    case VAL_FUNCTION:
        value = value_function(NULL, 0);
        break;
    case VAL_BYTES:
        value = value_bytes(64);
        break;
    case VAL_VECTOR:
        value = value_vector(1);
        break;
    case VAL_CLOSURE:
    case VAL_RESULT:
    case VAL_OPTION:
    case VAL_STRUCT:
    case VAL_ENUM:
        return NULL;
    }

    if (!value) return NULL;

    value->gc_state = 0;
    value->next = heap->objects;
    heap->objects = value;

    heap->bytes_allocated += size;
    heap->total_allocated += size;

    if (heap->generational_enabled) {
        heap->young_count++;
        heap->young_bytes += size;
    }

    return value;
}

/* Marking */

void gc_mark_value(Value *value) {
    if (!value || value_is_marked(value)) return;

    value_set_marked(value, true);

    switch (value->type) {
    case VAL_ARRAY: {
        Array *arr = value->as.array;
        for (size_t i = 0; i < arr->length; i++) {
            if (arr->items[i]) {  /* NULL check for safety */
                gc_mark_value(arr->items[i]);
            }
        }
        break;
    }
    case VAL_MAP: {
        Map *map = value->as.map;
        for (size_t i = 0; i < map->capacity; i++) {
            MapEntry *entry = map->buckets[i];
            while (entry) {
                gc_mark_value(entry->value);
                entry = entry->next;
            }
        }
        break;
    }
    case VAL_CLOSURE: {
        Closure *closure = (Closure *)value->as.closure;
        for (size_t i = 0; i < closure->upvalue_count; i++) {
            Upvalue *upvalue = closure->upvalues[i];
            if (upvalue) {
                if (!upvalue_is_open(upvalue)) {
                    NanValue closed = upvalue->closed;
                    if (nanbox_is_obj(closed)) {
                        gc_mark_value((Value *)nanbox_as_obj(closed));
                    }
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

static void gc_mark_nanvalue(NanValue v) {
    if (nanbox_is_obj(v)) {
        gc_mark_value((Value *)nanbox_as_obj(v));
    }
}

void gc_mark_roots(VM *vm) {
    for (NanValue *slot = vm->stack; slot < vm->stack_top; slot++) {
        gc_mark_nanvalue(*slot);
    }

    gc_mark_value(vm->globals);

    Upvalue *upvalue = vm->open_upvalues;
    while (upvalue) {
        if (!upvalue_is_open(upvalue)) {
            gc_mark_nanvalue(upvalue->closed);
        }
        upvalue = upvalue->next;
    }

    if (vm->code) {
        Chunk *chunk = vm->code->main;
        for (size_t i = 0; i < chunk->constants_size; i++) {
            gc_mark_value(chunk->constants[i]);
        }

        for (size_t f = 0; f < vm->code->functions_count; f++) {
            chunk = vm->code->functions[f];
            for (size_t i = 0; i < chunk->constants_size; i++) {
                gc_mark_value(chunk->constants[i]);
            }
        }
    }
}

/* Sweeping */

static void sweep(Heap *heap) {
    Value **object = &heap->objects;

    while (*object) {
        Value *obj = *object;
        if (value_is_marked(obj)) {
            value_set_marked(obj, false);

            if (heap->generational_enabled && !value_is_old_gen(obj)) {
                value_inc_survival(obj);
                if (value_survival_count(obj) >= heap->promotion_threshold) {
                    size_t size = value_size(obj->type);
                    heap->young_count--;
                    heap->young_bytes -= size;
                    heap->old_count++;
                    heap->old_bytes += size;
                    value_set_old_gen(obj);
                }
            }

            object = &obj->next;
        } else {
            uint32_t expected = 0;
            if (!atomic_compare_exchange_strong_explicit(
                    &obj->refcount, &expected, REFCOUNT_FREEING,
                    memory_order_acq_rel, memory_order_acquire)) {
                value_set_marked(obj, false);
                object = &obj->next;
                continue;
            }

            *object = obj->next;

            size_t size = value_size(obj->type);
            heap->bytes_allocated -= size;
            heap->total_freed += size;

            if (heap->generational_enabled) {
                if (value_is_old_gen(obj)) {
                    heap->old_count--;
                    heap->old_bytes -= size;
                } else {
                    heap->young_count--;
                    heap->young_bytes -= size;
                }
            }

            value_free(obj);
        }
    }
}

/* Collection */

void gc_collect(Heap *heap, VM *vm) {
#ifdef AGIM_DEBUG
    printf("-- gc begin (used: %zu bytes)\n", heap->bytes_allocated);
    size_t before = heap->bytes_allocated;
#endif

    gc_mark_roots(vm);
    sweep(heap);

    heap->next_gc = (size_t)(heap->bytes_allocated * 2.0f);
    if (heap->next_gc > heap->max_size) {
        heap->next_gc = heap->max_size;
    }

    heap->gc_count++;

#ifdef AGIM_DEBUG
    printf("-- gc end (freed: %zu bytes, now: %zu bytes)\n",
           before - heap->bytes_allocated, heap->bytes_allocated);
#endif
}

/* Statistics */

size_t heap_used(const Heap *heap) {
    return heap->bytes_allocated;
}

HeapStats heap_stats(const Heap *heap) {
    size_t object_count = 0;
    Value *obj = heap->objects;
    while (obj) {
        object_count++;
        obj = obj->next;
    }

    return (HeapStats){
        .bytes_allocated = heap->bytes_allocated,
        .bytes_freed = heap->total_freed,
        .objects_allocated = object_count,
        .objects_freed = 0,
        .gc_runs = heap->gc_count,
    };
}

void heap_print_stats(const Heap *heap) {
    HeapStats stats = heap_stats(heap);

    printf("Heap Statistics:\n");
    printf("  Allocated:    %zu bytes\n", stats.bytes_allocated);
    printf("  Total freed:  %zu bytes\n", stats.bytes_freed);
    printf("  Objects:      %zu\n", stats.objects_allocated);
    printf("  GC runs:      %zu\n", stats.gc_runs);
    printf("  Max size:     %zu bytes\n", heap->max_size);
}

/* Incremental GC */

bool gc_start_incremental(Heap *heap, VM *vm) {
    if (!heap || !vm) return false;

    if (heap->gc_phase != GC_IDLE) {
        return false;
    }

    heap->gc_phase = GC_MARKING;
    gc_mark_roots(vm);
    heap->mark_cursor = heap->objects;

    return true;
}

static bool gc_step_marking(Heap *heap) {
    size_t processed = 0;

    while (heap->mark_cursor && processed < heap->step_budget) {
        Value *obj = heap->mark_cursor;
        heap->mark_cursor = obj->next;

        if (value_is_marked(obj)) {
            switch (obj->type) {
            case VAL_ARRAY: {
                Array *arr = obj->as.array;
                for (size_t i = 0; i < arr->length; i++) {
                    gc_mark_value(arr->items[i]);
                }
                break;
            }
            case VAL_MAP: {
                Map *map = obj->as.map;
                for (size_t i = 0; i < map->capacity; i++) {
                    MapEntry *entry = map->buckets[i];
                    while (entry) {
                        gc_mark_value(entry->value);
                        entry = entry->next;
                    }
                }
                break;
            }
            default:
                break;
            }
        }

        processed++;
    }

    return heap->mark_cursor != NULL;
}

static bool gc_step_sweeping(Heap *heap) {
    size_t processed = 0;

    while (*heap->sweep_prev && processed < heap->step_budget) {
        Value *obj = *heap->sweep_prev;

        if (value_is_marked(obj)) {
            value_set_marked(obj, false);
            heap->sweep_prev = &obj->next;
        } else {
            uint32_t expected = 0;
            if (!atomic_compare_exchange_strong_explicit(
                    &obj->refcount, &expected, REFCOUNT_FREEING,
                    memory_order_acq_rel, memory_order_acquire)) {
                value_set_marked(obj, false);
                heap->sweep_prev = &obj->next;
                processed++;
                continue;
            }

            *heap->sweep_prev = obj->next;

            size_t size = value_size(obj->type);
            heap->bytes_allocated -= size;
            heap->total_freed += size;

            value_free(obj);
        }

        processed++;
    }

    return *heap->sweep_prev != NULL;
}

bool gc_step(Heap *heap, VM *vm) {
    if (!heap || heap->gc_phase == GC_IDLE) {
        return false;
    }

    (void)vm;

    switch (heap->gc_phase) {
    case GC_MARKING:
        if (!gc_step_marking(heap)) {
            heap->gc_phase = GC_SWEEPING;
            heap->sweep_prev = &heap->objects;
        }
        return true;

    case GC_SWEEPING:
        if (!gc_step_sweeping(heap)) {
            heap->gc_phase = GC_IDLE;
            heap->gc_count++;

            heap->next_gc = (size_t)(heap->bytes_allocated * 1.5f);
            if (heap->next_gc > heap->max_size) {
                heap->next_gc = heap->max_size;
            }

            return false;
        }
        return true;

    case GC_IDLE:
    default:
        return false;
    }
}

bool gc_in_progress(const Heap *heap) {
    return heap && heap->gc_phase != GC_IDLE;
}

void gc_complete(Heap *heap, VM *vm) {
    if (!heap || !vm) return;

    while (gc_step(heap, vm)) {
    }
}

/* Gray List for Tri-Color Marking */

static bool gc_gray_push(Heap *heap, Value *value) {
    if (!heap || !value) return false;

    if (heap->gray_count >= heap->gray_capacity) {
        size_t new_cap = heap->gray_capacity == 0 ? 64 : heap->gray_capacity * 2;
        Value **new_list = realloc(heap->gray_list, sizeof(Value *) * new_cap);
        if (!new_list) return false;
        heap->gray_list = new_list;
        heap->gray_capacity = new_cap;
    }

    heap->gray_list[heap->gray_count++] = value;
    return true;
}

static Value *gc_gray_pop(Heap *heap) {
    if (!heap || heap->gray_count == 0) return NULL;
    return heap->gray_list[--heap->gray_count];
}

/* Incremental marking with work packets */
bool gc_mark_increment(Heap *heap, size_t max_objects) {
    if (!heap) return true;

    size_t marked = 0;
    while (heap->gray_count > 0 && marked < max_objects) {
        Value *obj = gc_gray_pop(heap);
        if (!obj || !value_is_marked(obj)) continue;

        /* Scan object and push children to gray list */
        switch (obj->type) {
        case VAL_ARRAY: {
            Array *arr = obj->as.array;
            for (size_t i = 0; i < arr->length; i++) {
                Value *child = arr->items[i];
                if (child && !value_is_marked(child)) {
                    value_set_marked(child, true);
                    gc_gray_push(heap, child);
                }
            }
            break;
        }
        case VAL_MAP: {
            Map *map = obj->as.map;
            for (size_t i = 0; i < map->capacity; i++) {
                MapEntry *entry = map->buckets[i];
                while (entry) {
                    if (entry->value && !value_is_marked(entry->value)) {
                        value_set_marked(entry->value, true);
                        gc_gray_push(heap, entry->value);
                    }
                    entry = entry->next;
                }
            }
            break;
        }
        case VAL_CLOSURE: {
            Closure *closure = (Closure *)obj->as.closure;
            for (size_t i = 0; i < closure->upvalue_count; i++) {
                Upvalue *upvalue = closure->upvalues[i];
                if (upvalue && !upvalue_is_open(upvalue)) {
                    NanValue closed = upvalue->closed;
                    if (nanbox_is_obj(closed)) {
                        Value *child = (Value *)nanbox_as_obj(closed);
                        if (child && !value_is_marked(child)) {
                            value_set_marked(child, true);
                            gc_gray_push(heap, child);
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
        }
        marked++;
    }

    /* Return true if marking is complete (gray list empty) */
    return heap->gray_count == 0;
}

/* Generational GC */

static void remember_set_add(Heap *heap, Value *value) {
    if (!heap || !value) return;

    if (value_is_remembered(value)) return;

    if (heap->remember_count >= heap->max_remember_size) {
        heap->needs_full_gc = true;
        return;
    }

    if (heap->remember_count >= heap->remember_capacity) {
        size_t new_cap = heap->remember_capacity == 0 ? 64 : heap->remember_capacity * 2;
        if (new_cap > heap->max_remember_size) {
            new_cap = heap->max_remember_size;
        }
        Value **new_set = realloc(heap->remember_set, sizeof(Value *) * new_cap);
        if (!new_set) return;
        heap->remember_set = new_set;
        heap->remember_capacity = new_cap;
    }

    heap->remember_set[heap->remember_count++] = value;
    value_set_remembered(value, true);
}

static void remember_set_clear(Heap *heap) {
    if (!heap) return;

    for (size_t i = 0; i < heap->remember_count; i++) {
        if (heap->remember_set[i]) {
            value_set_remembered(heap->remember_set[i], false);
        }
    }
    heap->remember_count = 0;
}

void gc_write_barrier(Heap *heap, Value *container, Value *value) {
    if (!heap || !heap->generational_enabled) return;
    if (!container || !value) return;

    if (value_is_old_gen(container) && !value_is_old_gen(value)) {
        remember_set_add(heap, container);
    }
}

static void gc_mark_young(Heap *heap, VM *vm) {
    gc_mark_roots(vm);

    for (size_t i = 0; i < heap->remember_count; i++) {
        Value *old_obj = heap->remember_set[i];
        if (!old_obj) continue;

        switch (old_obj->type) {
        case VAL_ARRAY: {
            Array *arr = old_obj->as.array;
            for (size_t j = 0; j < arr->length; j++) {
                gc_mark_value(arr->items[j]);
            }
            break;
        }
        case VAL_MAP: {
            Map *map = old_obj->as.map;
            for (size_t j = 0; j < map->capacity; j++) {
                MapEntry *entry = map->buckets[j];
                while (entry) {
                    gc_mark_value(entry->value);
                    entry = entry->next;
                }
            }
            break;
        }
        case VAL_CLOSURE: {
            Closure *closure = (Closure *)old_obj->as.closure;
            for (size_t j = 0; j < closure->upvalue_count; j++) {
                Upvalue *upvalue = closure->upvalues[j];
                if (upvalue && !upvalue_is_open(upvalue)) {
                    NanValue closed = upvalue->closed;
                    if (nanbox_is_obj(closed)) {
                        gc_mark_value((Value *)nanbox_as_obj(closed));
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

static void sweep_young(Heap *heap) {
    Value **object = &heap->objects;

    while (*object) {
        Value *obj = *object;

        if (value_is_old_gen(obj)) {
            object = &obj->next;
            continue;
        }

        if (value_is_marked(obj)) {
            value_set_marked(obj, false);
            value_inc_survival(obj);

            if (value_survival_count(obj) >= heap->promotion_threshold) {
                size_t size = value_size(obj->type);
                heap->young_count--;
                heap->young_bytes -= size;
                heap->old_count++;
                heap->old_bytes += size;
                value_set_old_gen(obj);
            }

            object = &obj->next;
        } else {
            uint32_t expected = 0;
            if (!atomic_compare_exchange_strong_explicit(
                    &obj->refcount, &expected, REFCOUNT_FREEING,
                    memory_order_acq_rel, memory_order_acquire)) {
                value_set_marked(obj, false);
                object = &obj->next;
                continue;
            }

            *object = obj->next;

            size_t size = value_size(obj->type);
            heap->bytes_allocated -= size;
            heap->young_count--;
            heap->young_bytes -= size;
            heap->total_freed += size;

            value_free(obj);
        }
    }
}

void gc_collect_young(Heap *heap, VM *vm) {
    if (!heap || !vm) return;

#ifdef AGIM_DEBUG
    printf("-- minor gc begin (young: %zu bytes)\n", heap->young_bytes);
    size_t before = heap->young_bytes;
#endif

    gc_mark_young(heap, vm);
    sweep_young(heap);
    remember_set_clear(heap);

    heap->young_gc_threshold = heap->young_bytes * 2;
    if (heap->young_gc_threshold < 4096) {
        heap->young_gc_threshold = 4096;
    }

    heap->minor_gc_count++;
    heap->gc_count++;

#ifdef AGIM_DEBUG
    printf("-- minor gc end (freed: %zu bytes, young now: %zu bytes)\n",
           before - heap->young_bytes, heap->young_bytes);
#endif
}

void gc_collect_full(Heap *heap, VM *vm) {
    if (!heap || !vm) return;

#ifdef AGIM_DEBUG
    printf("-- major gc begin (total: %zu bytes)\n", heap->bytes_allocated);
    size_t before = heap->bytes_allocated;
#endif

    gc_mark_roots(vm);
    sweep(heap);
    remember_set_clear(heap);

    heap->next_gc = (size_t)(heap->bytes_allocated * 2.0f);
    if (heap->next_gc > heap->max_size) {
        heap->next_gc = heap->max_size;
    }

    heap->major_gc_count++;
    heap->gc_count++;

#ifdef AGIM_DEBUG
    printf("-- major gc end (freed: %zu bytes, now: %zu bytes)\n",
           before - heap->bytes_allocated, heap->bytes_allocated);
#endif
}

void gc_set_generational(Heap *heap, bool enabled) {
    if (heap) {
        heap->generational_enabled = enabled;
    }
}
