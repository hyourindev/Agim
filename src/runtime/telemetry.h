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

/*============================================================================
 * Block Statistics
 *============================================================================*/

/**
 * Runtime statistics for a block.
 */
typedef struct BlockStats {
    /* Message passing */
    uint64_t messages_sent;         /* Total messages sent */
    uint64_t messages_received;     /* Total messages received */
    uint64_t messages_dropped;      /* Messages dropped due to overflow */

    /* Execution */
    uint64_t reductions;            /* Total reductions executed */
    uint64_t yields;                /* Number of times yielded */
    uint64_t context_switches;      /* Context switch count */

    /* Memory */
    uint64_t heap_allocations;      /* Total heap allocations */
    uint64_t heap_bytes_allocated;  /* Total bytes allocated */
    uint64_t gc_cycles;             /* GC collection count */
    uint64_t gc_bytes_collected;    /* Total bytes freed by GC */

    /* Time */
    uint64_t started_at;            /* Timestamp when started (ms) */
    uint64_t cpu_time_ns;           /* CPU time consumed (ns) */
    uint64_t wall_time_ns;          /* Wall clock time (ns) */

    /* State changes */
    uint64_t state_changes;         /* Number of state transitions */
    uint64_t wait_count;            /* Number of times entered WAITING */
    uint64_t wait_time_ns;          /* Total time spent waiting (ns) */
} BlockStats;

/*============================================================================
 * Trace Flags
 *============================================================================*/

/**
 * Flags for tracing block activity.
 */
typedef enum TraceFlag {
    TRACE_NONE         = 0,
    TRACE_SEND         = 1 << 0,    /* Trace message sends */
    TRACE_RECEIVE      = 1 << 1,    /* Trace message receives */
    TRACE_SPAWN        = 1 << 2,    /* Trace spawn operations */
    TRACE_EXIT         = 1 << 3,    /* Trace block exits */
    TRACE_LINK         = 1 << 4,    /* Trace link/unlink operations */
    TRACE_SCHEDULE     = 1 << 5,    /* Trace scheduling events */
    TRACE_GC           = 1 << 6,    /* Trace GC events */
    TRACE_CALL         = 1 << 7,    /* Trace function calls */
    TRACE_ALL          = 0xFF,      /* Trace everything */
} TraceFlag;

typedef uint32_t TraceFlags;

/**
 * Trace event type.
 */
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

/**
 * A single trace event.
 */
typedef struct TraceEvent {
    TraceEventType type;            /* Event type */
    uint64_t timestamp;             /* When event occurred (ns) */
    Pid source_pid;                 /* Source block PID */
    Pid target_pid;                 /* Target PID (for send/spawn/link) */
    union {
        struct {
            const char *message_type;   /* Message type (if available) */
            size_t message_size;        /* Message size in bytes */
        } msg;
        struct {
            int exit_code;              /* Exit code */
            const char *reason;         /* Exit reason */
        } exit;
        struct {
            const char *func_name;      /* Function name */
            int depth;                  /* Call depth */
        } call;
        struct {
            size_t bytes_collected;     /* Bytes freed */
            size_t heap_size;           /* Heap size after GC */
        } gc;
    } data;
    struct TraceEvent *next;        /* Next event in buffer */
} TraceEvent;

/*============================================================================
 * Trace Buffer
 *============================================================================*/

/**
 * Circular buffer for trace events.
 */
typedef struct TraceBuffer {
    TraceEvent *events;             /* Event array */
    size_t capacity;                /* Buffer capacity */
    _Atomic(size_t) write_index;    /* Next write position */
    _Atomic(size_t) count;          /* Number of events (capped at capacity) */
} TraceBuffer;

/*============================================================================
 * Tracer
 *============================================================================*/

/**
 * Callback for trace events.
 */
typedef void (*TraceCallback)(const TraceEvent *event, void *ctx);

/**
 * Block tracer configuration.
 */
typedef struct Tracer {
    TraceFlags flags;               /* What to trace */
    TraceBuffer *buffer;            /* Event buffer (NULL = no buffering) */
    TraceCallback callback;         /* Callback for events (NULL = none) */
    void *callback_ctx;             /* Callback context */
    Pid tracer_pid;                 /* PID to send trace messages to (0 = none) */
    bool enabled;                   /* Master enable/disable */
} Tracer;

/*============================================================================
 * Telemetry API - Statistics
 *============================================================================*/

/**
 * Initialize block stats to zero.
 */
void stats_init(BlockStats *stats);

/**
 * Record a message send.
 */
void stats_record_send(BlockStats *stats);

/**
 * Record a message receive.
 */
void stats_record_receive(BlockStats *stats);

/**
 * Record dropped message.
 */
void stats_record_dropped(BlockStats *stats);

/**
 * Record reductions.
 */
void stats_record_reductions(BlockStats *stats, uint64_t count);

/**
 * Record a yield.
 */
void stats_record_yield(BlockStats *stats);

/**
 * Record heap allocation.
 */
void stats_record_allocation(BlockStats *stats, size_t bytes);

/**
 * Record GC cycle.
 */
void stats_record_gc(BlockStats *stats, size_t bytes_collected);

/**
 * Get uptime in milliseconds.
 */
uint64_t stats_uptime_ms(const BlockStats *stats);

/*============================================================================
 * Telemetry API - Tracing
 *============================================================================*/

/**
 * Create a new tracer.
 */
Tracer *tracer_new(TraceFlags flags, size_t buffer_capacity);

/**
 * Free a tracer.
 */
void tracer_free(Tracer *tracer);

/**
 * Enable/disable tracing.
 */
void tracer_set_enabled(Tracer *tracer, bool enabled);

/**
 * Set trace flags.
 */
void tracer_set_flags(Tracer *tracer, TraceFlags flags);

/**
 * Set callback for trace events.
 */
void tracer_set_callback(Tracer *tracer, TraceCallback callback, void *ctx);

/**
 * Set PID to receive trace messages.
 */
void tracer_set_target(Tracer *tracer, Pid target_pid);

/**
 * Record a trace event.
 */
void tracer_record(Tracer *tracer, TraceEventType type, Pid source,
                   Pid target, const void *data);

/**
 * Record a send event.
 */
void tracer_record_send(Tracer *tracer, Pid from, Pid to,
                        const char *msg_type, size_t msg_size);

/**
 * Record a receive event.
 */
void tracer_record_receive(Tracer *tracer, Pid receiver, Pid sender,
                           const char *msg_type, size_t msg_size);

/**
 * Record a spawn event.
 */
void tracer_record_spawn(Tracer *tracer, Pid parent, Pid child);

/**
 * Record an exit event.
 */
void tracer_record_exit(Tracer *tracer, Pid pid, int exit_code, const char *reason);

/**
 * Record a link event.
 */
void tracer_record_link(Tracer *tracer, Pid pid1, Pid pid2);

/**
 * Record an unlink event.
 */
void tracer_record_unlink(Tracer *tracer, Pid pid1, Pid pid2);

/**
 * Record a GC event.
 */
void tracer_record_gc(Tracer *tracer, Pid pid, size_t bytes_collected, size_t heap_size);

/**
 * Record a function call.
 */
void tracer_record_call(Tracer *tracer, Pid pid, const char *func_name, int depth);

/**
 * Record a function return.
 */
void tracer_record_return(Tracer *tracer, Pid pid, const char *func_name, int depth);

/*============================================================================
 * Telemetry API - Buffer Access
 *============================================================================*/

/**
 * Get events from trace buffer.
 * Returns array of events (caller must free).
 */
TraceEvent *tracer_get_events(Tracer *tracer, size_t *count);

/**
 * Clear trace buffer.
 */
void tracer_clear(Tracer *tracer);

/*============================================================================
 * System-Wide Telemetry
 *============================================================================*/

/**
 * System-wide telemetry stats.
 */
typedef struct SystemStats {
    /* Blocks */
    uint64_t total_blocks_created;
    uint64_t total_blocks_exited;
    uint64_t active_blocks;

    /* Messages */
    uint64_t total_messages_sent;
    uint64_t total_messages_dropped;

    /* Memory */
    uint64_t total_heap_bytes;
    uint64_t total_gc_cycles;

    /* Scheduling */
    uint64_t total_context_switches;
    uint64_t total_yields;

    /* Time */
    uint64_t uptime_ms;
} SystemStats;

/**
 * Get system-wide stats from scheduler.
 */
struct Scheduler;
void system_stats_get(struct Scheduler *sched, SystemStats *stats);

#endif /* AGIM_RUNTIME_TELEMETRY_H */
