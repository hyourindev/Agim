/*
 * Agim - Garbage Collector
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_GC_H
#define AGIM_VM_GC_H

#include <stdbool.h>
#include <stddef.h>

#include "vm/value.h"
#include "vm/vm.h"

/*============================================================================
 * GC Configuration
 *============================================================================*/

typedef struct GCConfig {
    size_t initial_heap_size;   /* Initial heap size (bytes) */
    size_t max_heap_size;       /* Maximum heap size (bytes) */
    float growth_factor;        /* Heap growth factor */
    float gc_threshold;         /* Trigger GC when used > threshold * heap_size */
    size_t incremental_step;    /* Max objects to process per incremental step */
    size_t max_remember_size;   /* Maximum remember set size before forcing full GC */
} GCConfig;

/*============================================================================
 * Incremental GC Phases
 *============================================================================*/

typedef enum GCPhase {
    GC_IDLE,        /* No collection in progress */
    GC_MARKING,     /* Incrementally marking reachable objects */
    GC_SWEEPING,    /* Incrementally sweeping unreachable objects */
} GCPhase;

/*============================================================================
 * Heap (Per-Block)
 *============================================================================*/

typedef struct Heap {
    /* All allocated objects */
    Value *objects;

    /* Memory tracking */
    size_t bytes_allocated;
    size_t next_gc;
    size_t max_size;

    /* Incremental GC state */
    GCPhase gc_phase;
    Value *mark_cursor;     /* Current position in marking traversal */
    Value *sweep_cursor;    /* Current position in sweep traversal */
    Value **sweep_prev;     /* Previous pointer for sweep list manipulation */
    size_t step_budget;     /* Objects to process this step */

    /* Generational GC state */
    bool generational_enabled;    /* Enable generational collection */
    size_t young_count;           /* Objects in young generation */
    size_t old_count;             /* Objects in old generation */
    size_t young_bytes;           /* Bytes in young generation */
    size_t old_bytes;             /* Bytes in old generation */
    Value **remember_set;         /* Pointers from old->young (write barrier) */
    size_t remember_count;        /* Current remember set size */
    size_t remember_capacity;     /* Remember set capacity */
    size_t max_remember_size;     /* Maximum remember set size (triggers full GC) */
    bool needs_full_gc;           /* Flag: remember set full, need full GC */
    uint8_t promotion_threshold;  /* Survivals needed before promotion (default: 2) */
    size_t young_gc_threshold;    /* Bytes before triggering minor GC */
    size_t minor_gc_count;        /* Number of minor collections */
    size_t major_gc_count;        /* Number of major collections */

    /* Statistics */
    size_t total_allocated;
    size_t total_freed;
    size_t gc_count;
} Heap;

/*============================================================================
 * GC API
 *============================================================================*/

/**
 * Get default GC configuration.
 */
GCConfig gc_config_default(void);

/**
 * Create a new heap with the given configuration.
 */
Heap *heap_new(const GCConfig *config);

/**
 * Free a heap and all its objects.
 */
void heap_free(Heap *heap);

/**
 * Allocate a value on the heap.
 * If VM is provided and GC threshold is reached, triggers collection.
 * Returns NULL if allocation fails (out of memory).
 */
Value *heap_alloc(Heap *heap, ValueType type);

/**
 * Allocate a value on the heap with optional GC.
 * Pass VM to enable automatic GC when threshold is reached.
 */
Value *heap_alloc_with_gc(Heap *heap, ValueType type, struct VM *vm);

/**
 * Run garbage collection on a heap (full stop-the-world collection).
 * Requires the VM to mark roots.
 */
void gc_collect(Heap *heap, VM *vm);

/**
 * Start incremental garbage collection.
 * Returns true if GC was started, false if already in progress.
 */
bool gc_start_incremental(Heap *heap, VM *vm);

/**
 * Perform one step of incremental GC.
 * Returns true if more work remains, false if GC is complete.
 */
bool gc_step(Heap *heap, VM *vm);

/**
 * Check if incremental GC is in progress.
 */
bool gc_in_progress(const Heap *heap);

/**
 * Force completion of any in-progress incremental GC.
 */
void gc_complete(Heap *heap, VM *vm);

/**
 * Mark a value as reachable.
 */
void gc_mark_value(Value *value);

/**
 * Mark all values reachable from the VM.
 */
void gc_mark_roots(VM *vm);

/*============================================================================
 * Generational GC
 *============================================================================*/

/**
 * Run minor (young generation) collection.
 * Only collects young objects + remember set roots.
 * Much faster than full collection.
 */
void gc_collect_young(Heap *heap, VM *vm);

/**
 * Run major (full) collection.
 * Collects both young and old generations.
 */
void gc_collect_full(Heap *heap, VM *vm);

/**
 * Write barrier for generational GC.
 * Call when storing a reference from container to value.
 * If container is old and value is young, adds container to remember set.
 */
void gc_write_barrier(Heap *heap, Value *container, Value *value);

/**
 * Enable/disable generational GC for a heap.
 */
void gc_set_generational(Heap *heap, bool enabled);

/*============================================================================
 * Statistics
 *============================================================================*/

/**
 * Get current heap usage in bytes.
 */
size_t heap_used(const Heap *heap);

/**
 * Get heap statistics.
 */
typedef struct HeapStats {
    size_t bytes_allocated;
    size_t bytes_freed;
    size_t objects_allocated;
    size_t objects_freed;
    size_t gc_runs;
} HeapStats;

HeapStats heap_stats(const Heap *heap);

/**
 * Print heap statistics.
 */
void heap_print_stats(const Heap *heap);

#endif /* AGIM_VM_GC_H */
