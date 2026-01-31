/*
 * Agim - Telemetry & Introspection
 *
 * Runtime statistics and tracing for blocks.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_TELEMETRY_H
#define AGIM_RUNTIME_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

#include "runtime/mailbox.h"

/* Block Statistics */

typedef struct BlockStats {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t messages_dropped;

    uint64_t reductions;
    uint64_t yields;
    uint64_t context_switches;

    uint64_t heap_allocations;
    uint64_t heap_bytes_allocated;
    uint64_t gc_cycles;
    uint64_t gc_bytes_collected;

    uint64_t started_at;
    uint64_t cpu_time_ns;
    uint64_t wall_time_ns;

    uint64_t state_changes;
    uint64_t wait_count;
    uint64_t wait_time_ns;
} BlockStats;

/* Trace Flags */

typedef enum TraceFlag {
    TRACE_NONE         = 0,
    TRACE_SEND         = 1 << 0,
    TRACE_RECEIVE      = 1 << 1,
    TRACE_SPAWN        = 1 << 2,
    TRACE_EXIT         = 1 << 3,
    TRACE_LINK         = 1 << 4,
    TRACE_SCHEDULE     = 1 << 5,
    TRACE_GC           = 1 << 6,
    TRACE_CALL         = 1 << 7,
    TRACE_ALL          = 0xFF,
} TraceFlag;

typedef uint32_t TraceFlags;

typedef enum TraceEventType {
    TRACE_EVENT_SEND,
    TRACE_EVENT_RECEIVE,
    TRACE_EVENT_SPAWN,
    TRACE_EVENT_EXIT,
    TRACE_EVENT_LINK,
    TRACE_EVENT_UNLINK,
    TRACE_EVENT_SCHEDULE,
    TRACE_EVENT_YIELD,
    TRACE_EVENT_GC,
    TRACE_EVENT_CALL,
    TRACE_EVENT_RETURN,
} TraceEventType;

typedef struct TraceEvent {
    TraceEventType type;
    uint64_t timestamp;
    Pid source_pid;
    Pid target_pid;
    union {
        struct {
            const char *message_type;
            size_t message_size;
        } msg;
        struct {
            int exit_code;
            const char *reason;
        } exit;
        struct {
            const char *func_name;
            int depth;
        } call;
        struct {
            size_t bytes_collected;
            size_t heap_size;
        } gc;
    } data;
    struct TraceEvent *next;
} TraceEvent;

/* Trace Buffer */

typedef struct TraceBuffer {
    TraceEvent *events;
    size_t capacity;
    _Atomic(size_t) write_index;
    _Atomic(size_t) count;
} TraceBuffer;

/* Tracer */

typedef void (*TraceCallback)(const TraceEvent *event, void *ctx);

typedef struct Tracer {
    TraceFlags flags;
    TraceBuffer *buffer;
    TraceCallback callback;
    void *callback_ctx;
    Pid tracer_pid;
    bool enabled;
} Tracer;

/* Statistics API */

void stats_init(BlockStats *stats);
void stats_record_send(BlockStats *stats);
void stats_record_receive(BlockStats *stats);
void stats_record_dropped(BlockStats *stats);
void stats_record_reductions(BlockStats *stats, uint64_t count);
void stats_record_yield(BlockStats *stats);
void stats_record_allocation(BlockStats *stats, size_t bytes);
void stats_record_gc(BlockStats *stats, size_t bytes_collected);
uint64_t stats_uptime_ms(const BlockStats *stats);

/* Tracing API */

Tracer *tracer_new(TraceFlags flags, size_t buffer_capacity);
void tracer_free(Tracer *tracer);
void tracer_set_enabled(Tracer *tracer, bool enabled);
void tracer_set_flags(Tracer *tracer, TraceFlags flags);
void tracer_set_callback(Tracer *tracer, TraceCallback callback, void *ctx);
void tracer_set_target(Tracer *tracer, Pid target_pid);

void tracer_record(Tracer *tracer, TraceEventType type, Pid source,
                   Pid target, const void *data);
void tracer_record_send(Tracer *tracer, Pid from, Pid to,
                        const char *msg_type, size_t msg_size);
void tracer_record_receive(Tracer *tracer, Pid receiver, Pid sender,
                           const char *msg_type, size_t msg_size);
void tracer_record_spawn(Tracer *tracer, Pid parent, Pid child);
void tracer_record_exit(Tracer *tracer, Pid pid, int exit_code, const char *reason);
void tracer_record_link(Tracer *tracer, Pid pid1, Pid pid2);
void tracer_record_unlink(Tracer *tracer, Pid pid1, Pid pid2);
void tracer_record_gc(Tracer *tracer, Pid pid, size_t bytes_collected, size_t heap_size);
void tracer_record_call(Tracer *tracer, Pid pid, const char *func_name, int depth);
void tracer_record_return(Tracer *tracer, Pid pid, const char *func_name, int depth);

/* Buffer Access */

TraceEvent *tracer_get_events(Tracer *tracer, size_t *count);
void tracer_clear(Tracer *tracer);

/* System-Wide Statistics */

typedef struct SystemStats {
    uint64_t total_blocks_created;
    uint64_t total_blocks_exited;
    uint64_t active_blocks;

    uint64_t total_messages_sent;
    uint64_t total_messages_dropped;

    uint64_t total_heap_bytes;
    uint64_t total_gc_cycles;

    uint64_t total_context_switches;
    uint64_t total_yields;

    uint64_t uptime_ms;
} SystemStats;

struct Scheduler;
void system_stats_get(struct Scheduler *sched, SystemStats *stats);

#endif /* AGIM_RUNTIME_TELEMETRY_H */
