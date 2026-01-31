/*
 * Agim - Timer Wheel for Timeout Management
 *
 * Efficient timer management for receive timeouts and other time-based
 * operations. Uses a hierarchical timer wheel for O(1) insertion and
 * deletion.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_TIMER_H
#define AGIM_RUNTIME_TIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "runtime/mailbox.h"

/*============================================================================
 * Timer Types
 *============================================================================*/

/**
 * Timer callback function type.
 */
typedef void (*TimerCallback)(void *ctx, Pid block_pid);

/**
 * Timer entry represents a pending timeout.
 */
typedef struct TimerEntry {
    Pid block_pid;              /* Block waiting for timeout */
    uint64_t deadline_ms;       /* Absolute deadline in milliseconds */
    TimerCallback callback;     /* Callback when timer fires (optional) */
    void *callback_ctx;         /* Context for callback */
    struct TimerEntry *next;    /* Next entry in bucket/free list */
    struct TimerEntry *prev;    /* Previous entry in bucket */
    bool cancelled;             /* True if timer was cancelled */
} TimerEntry;

/**
 * Timer wheel bucket (slot).
 */
typedef struct TimerBucket {
    TimerEntry *head;           /* First entry in bucket */
    TimerEntry *tail;           /* Last entry in bucket */
} TimerBucket;

/**
 * Timer wheel configuration.
 */
typedef struct TimerConfig {
    size_t wheel_size;          /* Number of slots (power of 2, default 256) */
    uint64_t tick_ms;           /* Milliseconds per tick (default 10) */
} TimerConfig;

/**
 * Timer wheel for managing timeouts.
 */
typedef struct TimerWheel {
    /* Configuration */
    size_t wheel_size;          /* Number of slots */
    uint64_t tick_ms;           /* Ms per tick */

    /* Wheel state */
    TimerBucket *buckets;       /* Array of buckets */
    size_t current_slot;        /* Current position in wheel */
    uint64_t current_time_ms;   /* Current time in ms */

    /* Memory management */
    TimerEntry *free_list;      /* Reusable timer entries */
    size_t allocated;           /* Total entries allocated */

    /* Synchronization */
    pthread_mutex_t lock;
} TimerWheel;

/*============================================================================
 * Timer Wheel API
 *============================================================================*/

/**
 * Get default timer configuration.
 */
TimerConfig timer_config_default(void);

/**
 * Create a new timer wheel.
 */
TimerWheel *timer_wheel_new(const TimerConfig *config);

/**
 * Free a timer wheel and all entries.
 */
void timer_wheel_free(TimerWheel *wheel);

/**
 * Add a timer that fires after timeout_ms milliseconds.
 * Returns a timer entry that can be used to cancel.
 */
TimerEntry *timer_add(TimerWheel *wheel, Pid block_pid, uint64_t timeout_ms,
                      TimerCallback callback, void *ctx);

/**
 * Cancel a timer.
 * Returns true if the timer was cancelled, false if already fired.
 */
bool timer_cancel(TimerWheel *wheel, TimerEntry *entry);

/**
 * Advance the timer wheel and return fired timers.
 * Call this periodically (e.g., every tick_ms).
 * Returns array of fired timer entries (caller must process).
 */
TimerEntry *timer_tick(TimerWheel *wheel, uint64_t current_time_ms, size_t *fired_count);

/**
 * Get the next deadline time (for efficient sleeping).
 * Returns 0 if no timers are pending.
 */
uint64_t timer_next_deadline(const TimerWheel *wheel);

/**
 * Check if any timers are pending.
 */
bool timer_has_pending(const TimerWheel *wheel);

/**
 * Get current time in milliseconds (helper).
 */
uint64_t timer_current_time_ms(void);

#endif /* AGIM_RUNTIME_TIMER_H */
