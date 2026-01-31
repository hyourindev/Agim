/*
 * Agim - Telemetry & Introspection Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/telemetry.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "runtime/timer.h"
#include "vm/gc.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t current_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Block Statistics */

void stats_init(BlockStats *stats) {
    if (!stats) return;
    memset(stats, 0, sizeof(BlockStats));
    stats->started_at = timer_current_time_ms();
}

void stats_record_send(BlockStats *stats) {
    if (stats) stats->messages_sent++;
}

void stats_record_receive(BlockStats *stats) {
    if (stats) stats->messages_received++;
}

void stats_record_dropped(BlockStats *stats) {
    if (stats) stats->messages_dropped++;
}

void stats_record_reductions(BlockStats *stats, uint64_t count) {
    if (stats) stats->reductions += count;
}

void stats_record_yield(BlockStats *stats) {
    if (stats) stats->yields++;
}

void stats_record_allocation(BlockStats *stats, size_t bytes) {
    if (stats) {
        stats->heap_allocations++;
        stats->heap_bytes_allocated += bytes;
    }
}

void stats_record_gc(BlockStats *stats, size_t bytes_collected) {
    if (stats) {
        stats->gc_cycles++;
        stats->gc_bytes_collected += bytes_collected;
    }
}

uint64_t stats_uptime_ms(const BlockStats *stats) {
    if (!stats) return 0;
    return timer_current_time_ms() - stats->started_at;
}

/* Trace Buffer */

static TraceBuffer *trace_buffer_new(size_t capacity) {
    TraceBuffer *buf = calloc(1, sizeof(TraceBuffer));
    if (!buf) return NULL;

    buf->events = calloc(capacity, sizeof(TraceEvent));
    if (!buf->events) {
        free(buf);
        return NULL;
    }

    buf->capacity = capacity;
    atomic_store(&buf->write_index, 0);
    atomic_store(&buf->count, 0);

    return buf;
}

static void trace_buffer_free(TraceBuffer *buf) {
    if (!buf) return;
    free(buf->events);
    free(buf);
}

static void trace_buffer_push(TraceBuffer *buf, const TraceEvent *event) {
    if (!buf || !event) return;

    size_t index = atomic_fetch_add(&buf->write_index, 1) % buf->capacity;
    buf->events[index] = *event;

    size_t count = atomic_load(&buf->count);
    if (count < buf->capacity) {
        atomic_fetch_add(&buf->count, 1);
    }
}

/* Tracer */

Tracer *tracer_new(TraceFlags flags, size_t buffer_capacity) {
    Tracer *tracer = calloc(1, sizeof(Tracer));
    if (!tracer) return NULL;

    tracer->flags = flags;
    tracer->enabled = true;
    tracer->callback = NULL;
    tracer->callback_ctx = NULL;
    tracer->tracer_pid = 0;

    if (buffer_capacity > 0) {
        tracer->buffer = trace_buffer_new(buffer_capacity);
        if (!tracer->buffer) {
            free(tracer);
            return NULL;
        }
    }

    return tracer;
}

void tracer_free(Tracer *tracer) {
    if (!tracer) return;

    trace_buffer_free(tracer->buffer);
    free(tracer);
}

void tracer_set_enabled(Tracer *tracer, bool enabled) {
    if (tracer) tracer->enabled = enabled;
}

void tracer_set_flags(Tracer *tracer, TraceFlags flags) {
    if (tracer) tracer->flags = flags;
}

void tracer_set_callback(Tracer *tracer, TraceCallback callback, void *ctx) {
    if (!tracer) return;
    tracer->callback = callback;
    tracer->callback_ctx = ctx;
}

void tracer_set_target(Tracer *tracer, Pid target_pid) {
    if (tracer) tracer->tracer_pid = target_pid;
}

static bool should_trace(Tracer *tracer, TraceFlag flag) {
    return tracer && tracer->enabled && (tracer->flags & flag);
}

void tracer_record(Tracer *tracer, TraceEventType type, Pid source,
                   Pid target, const void *data) {
    if (!tracer || !tracer->enabled) return;

    TraceEvent event = {
        .type = type,
        .timestamp = current_time_ns(),
        .source_pid = source,
        .target_pid = target,
        .next = NULL,
    };

    if (data) {
        switch (type) {
        case TRACE_EVENT_SEND:
        case TRACE_EVENT_RECEIVE:
        case TRACE_EVENT_EXIT:
        default:
            break;
        }
    }

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }

    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_send(Tracer *tracer, Pid from, Pid to,
                        const char *msg_type, size_t msg_size) {
    if (!should_trace(tracer, TRACE_SEND)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_SEND,
        .timestamp = current_time_ns(),
        .source_pid = from,
        .target_pid = to,
        .data.msg = { .message_type = msg_type, .message_size = msg_size },
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_receive(Tracer *tracer, Pid receiver, Pid sender,
                           const char *msg_type, size_t msg_size) {
    if (!should_trace(tracer, TRACE_RECEIVE)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_RECEIVE,
        .timestamp = current_time_ns(),
        .source_pid = receiver,
        .target_pid = sender,
        .data.msg = { .message_type = msg_type, .message_size = msg_size },
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_spawn(Tracer *tracer, Pid parent, Pid child) {
    if (!should_trace(tracer, TRACE_SPAWN)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_SPAWN,
        .timestamp = current_time_ns(),
        .source_pid = parent,
        .target_pid = child,
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_exit(Tracer *tracer, Pid pid, int exit_code, const char *reason) {
    if (!should_trace(tracer, TRACE_EXIT)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_EXIT,
        .timestamp = current_time_ns(),
        .source_pid = pid,
        .target_pid = 0,
        .data.exit = { .exit_code = exit_code, .reason = reason },
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_link(Tracer *tracer, Pid pid1, Pid pid2) {
    if (!should_trace(tracer, TRACE_LINK)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_LINK,
        .timestamp = current_time_ns(),
        .source_pid = pid1,
        .target_pid = pid2,
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_unlink(Tracer *tracer, Pid pid1, Pid pid2) {
    if (!should_trace(tracer, TRACE_LINK)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_UNLINK,
        .timestamp = current_time_ns(),
        .source_pid = pid1,
        .target_pid = pid2,
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_gc(Tracer *tracer, Pid pid, size_t bytes_collected, size_t heap_size) {
    if (!should_trace(tracer, TRACE_GC)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_GC,
        .timestamp = current_time_ns(),
        .source_pid = pid,
        .target_pid = 0,
        .data.gc = { .bytes_collected = bytes_collected, .heap_size = heap_size },
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_call(Tracer *tracer, Pid pid, const char *func_name, int depth) {
    if (!should_trace(tracer, TRACE_CALL)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_CALL,
        .timestamp = current_time_ns(),
        .source_pid = pid,
        .target_pid = 0,
        .data.call = { .func_name = func_name, .depth = depth },
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

void tracer_record_return(Tracer *tracer, Pid pid, const char *func_name, int depth) {
    if (!should_trace(tracer, TRACE_CALL)) return;

    TraceEvent event = {
        .type = TRACE_EVENT_RETURN,
        .timestamp = current_time_ns(),
        .source_pid = pid,
        .target_pid = 0,
        .data.call = { .func_name = func_name, .depth = depth },
        .next = NULL,
    };

    if (tracer->buffer) {
        trace_buffer_push(tracer->buffer, &event);
    }
    if (tracer->callback) {
        tracer->callback(&event, tracer->callback_ctx);
    }
}

/* Buffer Access */

TraceEvent *tracer_get_events(Tracer *tracer, size_t *count) {
    if (!tracer || !tracer->buffer || !count) {
        if (count) *count = 0;
        return NULL;
    }

    TraceBuffer *buf = tracer->buffer;
    size_t n = atomic_load(&buf->count);
    if (n == 0) {
        *count = 0;
        return NULL;
    }

    TraceEvent *events = malloc(sizeof(TraceEvent) * n);
    if (!events) {
        *count = 0;
        return NULL;
    }

    size_t write_idx = atomic_load(&buf->write_index);
    size_t start = (n == buf->capacity) ? (write_idx % buf->capacity) : 0;

    for (size_t i = 0; i < n; i++) {
        events[i] = buf->events[(start + i) % buf->capacity];
    }

    *count = n;
    return events;
}

void tracer_clear(Tracer *tracer) {
    if (!tracer || !tracer->buffer) return;

    atomic_store(&tracer->buffer->write_index, 0);
    atomic_store(&tracer->buffer->count, 0);
}

/* System-Wide Statistics */

typedef struct {
    SystemStats *stats;
} StatsAggContext;

static void aggregate_block_stats(Block *block, void *ctx) {
    StatsAggContext *agg = (StatsAggContext *)ctx;
    if (!block || !agg->stats) return;

    agg->stats->total_messages_sent += block->counters.messages_sent;
    agg->stats->total_gc_cycles += block->counters.gc_collections;
    agg->stats->total_yields += 1;

    if (block->heap) {
        agg->stats->total_heap_bytes += heap_used(block->heap);
    }
}

void system_stats_get(Scheduler *sched, SystemStats *stats) {
    if (!sched || !stats) return;

    memset(stats, 0, sizeof(SystemStats));

    stats->active_blocks = scheduler_block_count(sched);
    stats->uptime_ms = timer_current_time_ms() - sched->start_time_ms;

    StatsAggContext ctx = { .stats = stats };

    BlockRegistry *reg = &sched->registry;
    for (size_t i = 0; i < REGISTRY_SHARDS; i++) {
        RegistryShard *shard = &reg->shards[i];
        pthread_mutex_lock(&shard->lock);

        for (size_t j = 0; j < shard->capacity; j++) {
            BlockEntry *entry = shard->buckets[j];
            while (entry) {
                if (entry->block) {
                    aggregate_block_stats(entry->block, &ctx);
                }
                entry = entry->next;
            }
        }

        pthread_mutex_unlock(&shard->lock);
    }

    stats->total_context_switches = atomic_load(&sched->context_switches);
    stats->total_blocks_created = atomic_load(&sched->total_spawned);
    stats->total_blocks_exited = atomic_load(&sched->total_terminated);
}
