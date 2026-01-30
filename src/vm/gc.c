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

/*============================================================================
 * Default Configuration
 *============================================================================*/

GCConfig gc_config_default(void) {
    return (GCConfig){
        .initial_heap_size = 16 * 1024,        /* 16 KB (was 1 MB) - enables 1M agents */
        .max_heap_size = 1 * 1024 * 1024,      /* 1 MB (was 256 MB) */
        .growth_factor = 1.5f,                 /* Was 2.0 - slower growth for stability */
        .gc_threshold = 0.75f,
        .incremental_step = 100,               /* Process 100 objects per step */
        .max_remember_size = 1024,             /* Max 1024 remembered refs before full GC */
    };
}

/*============================================================================
 * Heap Lifecycle
 *============================================================================*/

Heap *heap_new(const GCConfig *config) {
    Heap *heap = malloc(sizeof(Heap));
    if (!heap) return NULL;

    heap->objects = NULL;
    heap->bytes_allocated = 0;
    heap->next_gc = config ? config->initial_heap_size : gc_config_default().initial_heap_size;
    heap->max_size = config ? config->max_heap_size : gc_config_default().max_heap_size;

    /* Incremental GC state */
    heap->gc_phase = GC_IDLE;
    heap->mark_cursor = NULL;
    heap->sweep_cursor = NULL;
    heap->sweep_prev = NULL;
    heap->step_budget = config ? config->incremental_step : 100;

    /* Generational GC state */
    heap->generational_enabled = true;  /* Enable by default */
    heap->young_count = 0;
    heap->old_count = 0;
    heap->young_bytes = 0;
    heap->old_bytes = 0;
    heap->remember_set = NULL;
    heap->remember_count = 0;
    heap->remember_capacity = 0;
    heap->max_remember_size = config ? config->max_remember_size : 1024;
    heap->needs_full_gc = false;
    heap->promotion_threshold = 2;  /* Promote after 2 survivals */
    heap->young_gc_threshold = config ? config->initial_heap_size / 4 : 4096;  /* 25% of initial heap */
    heap->minor_gc_count = 0;
    heap->major_gc_count = 0;

    heap->total_allocated = 0;
    heap->total_freed = 0;
    heap->gc_count = 0;

    return heap;
}

void heap_free(Heap *heap) {
    if (!heap) return;

    /* Free all objects */
    Value *object = heap->objects;
    while (object) {
        Value *next = object->next;
        value_free(object);
        object = next;
    }

    /* Free remember set */
    free(heap->remember_set);

    free(heap);
}

/*============================================================================
 * Allocation
 *============================================================================*/

static size_t value_size(ValueType type) {
    /* Estimate size for tracking purposes */
    switch (type) {
    case VAL_NIL:
    case VAL_BOOL:
    case VAL_INT:
    case VAL_FLOAT:
    case VAL_PID:
        return sizeof(Value);
    case VAL_STRING:
        return sizeof(Value) + sizeof(String) + 64; /* Average string */
    case VAL_ARRAY:
        return sizeof(Value) + sizeof(Array) + 8 * sizeof(Value *);
    case VAL_MAP:
        return sizeof(Value) + sizeof(Map) + 16 * sizeof(MapEntry *);
    case VAL_FUNCTION:
        return sizeof(Value) + sizeof(Function);
    case VAL_BYTES:
        return sizeof(Value) + sizeof(Bytes) + 64;
    default:
        return sizeof(Value);
    }
}

Value *heap_alloc_with_gc(Heap *heap, ValueType type, VM *vm) {
    size_t size = value_size(type);

    /* Check if remember set overflow triggered full GC request */
    if (heap->needs_full_gc && vm) {
        gc_collect_full(heap, vm);
        heap->needs_full_gc = false;
    }

    /* Generational GC: check if minor collection needed */
    if (heap->generational_enabled && vm &&
        heap->young_bytes + size > heap->young_gc_threshold) {
        gc_collect_young(heap, vm);
    }

    /* Check if we need a full collection */
    if (heap->bytes_allocated + size > heap->next_gc) {
        if (vm) {
            /* We have VM context - run full GC */
            if (heap->generational_enabled) {
                gc_collect_full(heap, vm);
            } else {
                gc_collect(heap, vm);
            }
        } else {
            /* No VM context - grow the heap instead */
            if (heap->next_gc < heap->max_size) {
                heap->next_gc = (size_t)(heap->next_gc * 1.5f);
                if (heap->next_gc > heap->max_size) {
                    heap->next_gc = heap->max_size;
                }
            }
        }
    }

    /* Check hard limit */
    if (heap->bytes_allocated + size > heap->max_size) {
        /* Try one more full GC if we can */
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

    /* Allocate based on type */
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
        /* Closures are created via closure_new(), not heap_alloc */
        return NULL;
    case VAL_RESULT:
        /* Results are created via value_result_ok/err(), not heap_alloc */
        return NULL;
    }

    if (!value) return NULL;

    /* Initialize gc_state for new object */
    value->gc_state = 0;  /* Young generation, not marked, survival count 0 */

    /* Add to object list */
    value->next = heap->objects;
    heap->objects = value;

    /* Update stats */
    heap->bytes_allocated += size;
    heap->total_allocated += size;

    /* Track young generation */
    if (heap->generational_enabled) {
        heap->young_count++;
        heap->young_bytes += size;
    }

    return value;
}

Value *heap_alloc(Heap *heap, ValueType type) {
    /* Legacy version without GC - just grows heap */
    size_t size = value_size(type);

    /* Check if we need to grow */
    if (heap->bytes_allocated + size > heap->next_gc) {
        if (heap->next_gc < heap->max_size) {
            heap->next_gc = (size_t)(heap->next_gc * 1.5f);
            if (heap->next_gc > heap->max_size) {
                heap->next_gc = heap->max_size;
            }
        }
    }

    /* Check hard limit */
    if (heap->bytes_allocated + size > heap->max_size) {
        fprintf(stderr, "agim: heap limit exceeded\n");
        return NULL;
    }

    /* Allocate based on type */
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
        /* Closures are created via closure_new(), not heap_alloc */
        return NULL;
    case VAL_RESULT:
        /* Results are created via value_result_ok/err(), not heap_alloc */
        return NULL;
    }

    if (!value) return NULL;

    /* Initialize gc_state for new object */
    value->gc_state = 0;  /* Young generation, not marked, survival count 0 */

    /* Add to object list */
    value->next = heap->objects;
    heap->objects = value;

    /* Update stats */
    heap->bytes_allocated += size;
    heap->total_allocated += size;

    /* Track young generation */
    if (heap->generational_enabled) {
        heap->young_count++;
        heap->young_bytes += size;
    }

    return value;
}

/*============================================================================
 * Marking
 *============================================================================*/

void gc_mark_value(Value *value) {
    if (!value || value_is_marked(value)) return;

    value_set_marked(value, true);

    /* Recursively mark referenced objects */
    switch (value->type) {
    case VAL_ARRAY: {
        Array *arr = value->as.array;
        for (size_t i = 0; i < arr->length; i++) {
            gc_mark_value(arr->items[i]);
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
        /* Mark upvalue contents */
        for (size_t i = 0; i < closure->upvalue_count; i++) {
            Upvalue *upvalue = closure->upvalues[i];
            if (upvalue) {
                /* Mark the closed-over value if the upvalue is closed */
                if (!upvalue_is_open(upvalue)) {
                    /* upvalue->closed is now NanValue, extract object if present */
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

/**
 * Mark a NaN-boxed value for GC.
 * Only marks heap objects (primitives don't need marking).
 */
static void gc_mark_nanvalue(NanValue v) {
    if (nanbox_is_obj(v)) {
        gc_mark_value((Value *)nanbox_as_obj(v));
    }
}

void gc_mark_roots(VM *vm) {
    /* Mark stack (now uses NanValue) */
    for (NanValue *slot = vm->stack; slot < vm->stack_top; slot++) {
        gc_mark_nanvalue(*slot);
    }

    /* Mark globals */
    gc_mark_value(vm->globals);

    /* Mark open upvalues */
    Upvalue *upvalue = vm->open_upvalues;
    while (upvalue) {
        /* Open upvalues point to stack, which is already marked */
        /* But mark the closed value if upvalue is closed */
        if (!upvalue_is_open(upvalue)) {
            gc_mark_nanvalue(upvalue->closed);
        }
        upvalue = upvalue->next;
    }

    /* Mark constants in bytecode */
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

/*============================================================================
 * Sweeping
 *============================================================================*/

static void sweep(Heap *heap) {
    Value **object = &heap->objects;

    while (*object) {
        Value *obj = *object;
        if (value_is_marked(obj)) {
            /* Survived - unmark for next GC cycle */
            value_set_marked(obj, false);

            /* Generational: increment survival count, possibly promote to old gen */
            if (heap->generational_enabled && !value_is_old_gen(obj)) {
                value_inc_survival(obj);
                if (value_survival_count(obj) >= heap->promotion_threshold) {
                    /* Promote to old generation */
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
            /*
             * Object is unreachable. Use CAS to atomically claim it for freeing.
             * This prevents a race where another thread increments refcount
             * between our check and free.
             *
             * We try to change refcount from 0 to REFCOUNT_FREEING.
             * If this fails, someone has obtained a reference - keep the object.
             */
            uint32_t expected = 0;
            if (!atomic_compare_exchange_strong_explicit(
                    &obj->refcount, &expected, REFCOUNT_FREEING,
                    memory_order_acq_rel, memory_order_acquire)) {
                /*
                 * CAS failed - refcount is no longer 0.
                 * Someone has a reference, keep the object alive.
                 * Unmark in case it becomes reachable again.
                 */
                value_set_marked(obj, false);
                object = &obj->next;
                continue;
            }

            /* Successfully claimed for freeing - safe to free */
            *object = obj->next;

            size_t size = value_size(obj->type);
            heap->bytes_allocated -= size;
            heap->total_freed += size;

            /* Update generational counters */
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

/*============================================================================
 * Collection
 *============================================================================*/

void gc_collect(Heap *heap, VM *vm) {
#ifdef AGIM_DEBUG
    printf("-- gc begin (used: %zu bytes)\n", heap->bytes_allocated);
    size_t before = heap->bytes_allocated;
#endif

    /* Mark phase */
    gc_mark_roots(vm);

    /* Sweep phase */
    sweep(heap);

    /* Update next GC threshold */
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

/*============================================================================
 * Statistics
 *============================================================================*/

size_t heap_used(const Heap *heap) {
    return heap->bytes_allocated;
}

HeapStats heap_stats(const Heap *heap) {
    /* Count objects */
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
        .objects_freed = 0, /* Not tracked per-object */
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

/*============================================================================
 * Incremental GC
 *============================================================================*/

bool gc_start_incremental(Heap *heap, VM *vm) {
    if (!heap || !vm) return false;

    /* Don't start if already in progress */
    if (heap->gc_phase != GC_IDLE) {
        return false;
    }

    /* Start marking phase */
    heap->gc_phase = GC_MARKING;

    /* Mark roots first (this is quick) */
    gc_mark_roots(vm);

    /* Set up for incremental marking of transitive references */
    heap->mark_cursor = heap->objects;

    return true;
}

/**
 * Perform incremental marking step.
 * Returns true if more marking work remains.
 */
static bool gc_step_marking(Heap *heap) {
    size_t processed = 0;

    while (heap->mark_cursor && processed < heap->step_budget) {
        Value *obj = heap->mark_cursor;
        heap->mark_cursor = obj->next;

        /* If marked, ensure children are marked */
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

/**
 * Perform incremental sweeping step.
 * Returns true if more sweep work remains.
 */
static bool gc_step_sweeping(Heap *heap) {
    size_t processed = 0;

    while (*heap->sweep_prev && processed < heap->step_budget) {
        Value *obj = *heap->sweep_prev;

        if (value_is_marked(obj)) {
            /* Keep - unmark for next cycle */
            value_set_marked(obj, false);
            heap->sweep_prev = &obj->next;
        } else {
            /*
             * Object is unreachable. Use CAS to atomically claim it for freeing.
             * This prevents a race where another thread increments refcount
             * between our check and free.
             */
            uint32_t expected = 0;
            if (!atomic_compare_exchange_strong_explicit(
                    &obj->refcount, &expected, REFCOUNT_FREEING,
                    memory_order_acq_rel, memory_order_acquire)) {
                /* CAS failed - someone has a reference, keep object */
                value_set_marked(obj, false);
                heap->sweep_prev = &obj->next;
                processed++;
                continue;
            }

            /* Successfully claimed - free unreachable object */
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

    (void)vm; /* VM used for re-marking if write barrier triggered */

    switch (heap->gc_phase) {
    case GC_MARKING:
        if (!gc_step_marking(heap)) {
            /* Marking complete, transition to sweeping */
            heap->gc_phase = GC_SWEEPING;
            heap->sweep_prev = &heap->objects;
        }
        return true;

    case GC_SWEEPING:
        if (!gc_step_sweeping(heap)) {
            /* Sweeping complete, finish GC */
            heap->gc_phase = GC_IDLE;
            heap->gc_count++;

            /* Update next GC threshold */
            heap->next_gc = (size_t)(heap->bytes_allocated * 1.5f);
            if (heap->next_gc > heap->max_size) {
                heap->next_gc = heap->max_size;
            }

            return false; /* GC complete */
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

    /* Run steps until complete */
    while (gc_step(heap, vm)) {
        /* Keep stepping */
    }
}

/*============================================================================
 * Generational GC Implementation
 *============================================================================*/

/**
 * Add a value to the remember set.
 * Called when an old-gen object references a young-gen object.
 */
static void remember_set_add(Heap *heap, Value *value) {
    if (!heap || !value) return;

    /* Already in remember set? */
    if (value_is_remembered(value)) return;

    /* Check if we've hit the maximum remember set size */
    if (heap->remember_count >= heap->max_remember_size) {
        /* Signal that a full GC is needed at next allocation */
        heap->needs_full_gc = true;
        return;  /* Don't add more - full GC will clear this anyway */
    }

    /* Grow remember set if needed */
    if (heap->remember_count >= heap->remember_capacity) {
        size_t new_cap = heap->remember_capacity == 0 ? 64 : heap->remember_capacity * 2;
        /* Cap growth at max_remember_size */
        if (new_cap > heap->max_remember_size) {
            new_cap = heap->max_remember_size;
        }
        Value **new_set = realloc(heap->remember_set, sizeof(Value *) * new_cap);
        if (!new_set) return;  /* OOM - skip, will be caught in full GC */
        heap->remember_set = new_set;
        heap->remember_capacity = new_cap;
    }

    heap->remember_set[heap->remember_count++] = value;
    value_set_remembered(value, true);
}

/**
 * Clear the remember set after collection.
 */
static void remember_set_clear(Heap *heap) {
    if (!heap) return;

    /* Clear remembered flags */
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

    /* If container is old and value is young, add to remember set */
    if (value_is_old_gen(container) && !value_is_old_gen(value)) {
        remember_set_add(heap, container);
    }
}

/**
 * Mark young objects only (+ remember set roots).
 */
static void gc_mark_young(Heap *heap, VM *vm) {
    /* Mark roots - only follow into young generation during minor GC */
    gc_mark_roots(vm);

    /* Mark from remember set (old objects pointing to young) */
    for (size_t i = 0; i < heap->remember_count; i++) {
        Value *old_obj = heap->remember_set[i];
        if (!old_obj) continue;

        /* Mark children of this old object (they might be young) */
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

/**
 * Sweep only young generation objects.
 */
static void sweep_young(Heap *heap) {
    Value **object = &heap->objects;

    while (*object) {
        Value *obj = *object;

        /* Skip old generation objects in minor GC */
        if (value_is_old_gen(obj)) {
            object = &obj->next;
            continue;
        }

        if (value_is_marked(obj)) {
            /* Survived - unmark and possibly promote */
            value_set_marked(obj, false);
            value_inc_survival(obj);

            if (value_survival_count(obj) >= heap->promotion_threshold) {
                /* Promote to old generation */
                size_t size = value_size(obj->type);
                heap->young_count--;
                heap->young_bytes -= size;
                heap->old_count++;
                heap->old_bytes += size;
                value_set_old_gen(obj);
            }

            object = &obj->next;
        } else {
            /*
             * Object is unreachable. Use CAS to atomically claim it for freeing.
             * This prevents a race where another thread increments refcount
             * between our check and free.
             */
            uint32_t expected = 0;
            if (!atomic_compare_exchange_strong_explicit(
                    &obj->refcount, &expected, REFCOUNT_FREEING,
                    memory_order_acq_rel, memory_order_acquire)) {
                /* CAS failed - someone has a reference, keep object */
                value_set_marked(obj, false);
                object = &obj->next;
                continue;
            }

            /* Successfully claimed - free unreachable young object */
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

    /* Mark phase (young + remember set roots) */
    gc_mark_young(heap, vm);

    /* Sweep phase (young only) */
    sweep_young(heap);

    /* Clear remember set */
    remember_set_clear(heap);

    /* Update threshold for next minor GC */
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

    /* Full mark phase */
    gc_mark_roots(vm);

    /* Full sweep phase */
    sweep(heap);

    /* Clear remember set */
    remember_set_clear(heap);

    /* Update next GC threshold */
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
