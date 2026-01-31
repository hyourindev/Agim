/*
 * Agim - Timer Wheel for Timeout Management
 *
 * Efficient timer management using a timer wheel for O(1) operations.
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

/* Types */

typedef void (*TimerCallback)(void *ctx, Pid block_pid);

typedef struct TimerEntry {
    Pid block_pid;
    uint64_t deadline_ms;
    TimerCallback callback;
    void *callback_ctx;
    struct TimerEntry *next;
    struct TimerEntry *prev;
    bool cancelled;
} TimerEntry;

typedef struct TimerBucket {
    TimerEntry *head;
    TimerEntry *tail;
} TimerBucket;

typedef struct TimerConfig {
    size_t wheel_size;
    uint64_t tick_ms;
} TimerConfig;

typedef struct TimerWheel {
    size_t wheel_size;
    uint64_t tick_ms;
    TimerBucket *buckets;
    size_t current_slot;
    uint64_t current_time_ms;
    TimerEntry *free_list;
    size_t allocated;
    pthread_mutex_t lock;
} TimerWheel;

/* Timer Wheel API */

TimerConfig timer_config_default(void);
TimerWheel *timer_wheel_new(const TimerConfig *config);
void timer_wheel_free(TimerWheel *wheel);

TimerEntry *timer_add(TimerWheel *wheel, Pid block_pid, uint64_t timeout_ms,
                      TimerCallback callback, void *ctx);
bool timer_cancel(TimerWheel *wheel, TimerEntry *entry);
TimerEntry *timer_tick(TimerWheel *wheel, uint64_t current_time_ms, size_t *fired_count);
uint64_t timer_next_deadline(const TimerWheel *wheel);
bool timer_has_pending(const TimerWheel *wheel);

uint64_t timer_current_time_ms(void);

#endif /* AGIM_RUNTIME_TIMER_H */
