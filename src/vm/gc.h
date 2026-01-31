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

/* GC Configuration */

typedef struct GCConfig {
    size_t initial_heap_size;
    size_t max_heap_size;
    float growth_factor;
    float gc_threshold;
    size_t incremental_step;
    size_t max_remember_size;
} GCConfig;

/* Incremental GC Phases */

typedef enum GCPhase {
    GC_IDLE,
    GC_MARKING,
    GC_SWEEPING,
} GCPhase;

/* Incremental marking constants */
#define GC_MARK_WORK_PACKET_SIZE 256

/* Card table for generational GC write barriers */
#define GC_CARD_SIZE 512
#define GC_CARD_TABLE_SIZE 4096

/* Heap (Per-Block) */

typedef struct Heap {
    Value *objects;

    size_t bytes_allocated;
    size_t next_gc;
    size_t max_size;

    /* Incremental GC state */
    GCPhase gc_phase;
    Value *mark_cursor;
    Value *sweep_cursor;
    Value **sweep_prev;
    size_t step_budget;

    /* Gray list for tri-color marking */
    Value **gray_list;
    size_t gray_count;
    size_t gray_capacity;

    /* Card table for efficient write barrier */
    uint8_t card_table[GC_CARD_TABLE_SIZE];

    /* Generational GC state */
    bool generational_enabled;
    size_t young_count;
    size_t old_count;
    size_t young_bytes;
    size_t old_bytes;
    Value **remember_set;
    size_t remember_count;
    size_t remember_capacity;
    size_t max_remember_size;
    bool needs_full_gc;
    uint8_t promotion_threshold;
    size_t young_gc_threshold;
    size_t minor_gc_count;
    size_t major_gc_count;

    /* Statistics */
    size_t total_allocated;
    size_t total_freed;
    size_t gc_count;
} Heap;

/* Thread-local heap for write barriers */

void gc_set_current_heap(Heap *heap);
Heap *gc_get_current_heap(void);

/* GC API */

GCConfig gc_config_default(void);
Heap *heap_new(const GCConfig *config);
void heap_free(Heap *heap);
Value *heap_alloc(Heap *heap, ValueType type);
Value *heap_alloc_with_gc(Heap *heap, ValueType type, struct VM *vm);
void gc_collect(Heap *heap, VM *vm);
bool gc_start_incremental(Heap *heap, VM *vm);
bool gc_step(Heap *heap, VM *vm);
bool gc_in_progress(const Heap *heap);
void gc_complete(Heap *heap, VM *vm);
void gc_mark_value(Value *value);
void gc_mark_roots(VM *vm);

/* Generational GC */

void gc_collect_young(Heap *heap, VM *vm);
void gc_collect_full(Heap *heap, VM *vm);
void gc_write_barrier(Heap *heap, Value *container, Value *value);
void gc_set_generational(Heap *heap, bool enabled);

/* Incremental marking with gray list */
bool gc_mark_increment(Heap *heap, size_t max_objects);

/* Statistics */

size_t heap_used(const Heap *heap);

typedef struct HeapStats {
    size_t bytes_allocated;
    size_t bytes_freed;
    size_t objects_allocated;
    size_t objects_freed;
    size_t gc_runs;
} HeapStats;

HeapStats heap_stats(const Heap *heap);
void heap_print_stats(const Heap *heap);

#endif /* AGIM_VM_GC_H */
