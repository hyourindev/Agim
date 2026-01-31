/*
 * Agim - Timer Wheel Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/timer.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* Configuration */

TimerConfig timer_config_default(void) {
    return (TimerConfig){
        .wheel_size = 256,      /* 256 slots */
        .tick_ms = 10,          /* 10ms per tick = 2.56s per rotation */
    };
}

/* Time Helpers */

uint64_t timer_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/* Timer Entry Management */

static TimerEntry *timer_entry_alloc(TimerWheel *wheel) {
    if (wheel->free_list) {
        TimerEntry *entry = wheel->free_list;
        wheel->free_list = entry->next;
        memset(entry, 0, sizeof(TimerEntry));
        return entry;
    }

    TimerEntry *entry = malloc(sizeof(TimerEntry));
    if (entry) {
        memset(entry, 0, sizeof(TimerEntry));
        wheel->allocated++;
    }
    return entry;
}

static void timer_entry_free(TimerWheel *wheel, TimerEntry *entry) {
    if (!entry) return;

    entry->next = wheel->free_list;
    entry->prev = NULL;
    entry->cancelled = false;
    wheel->free_list = entry;
}

/* Bucket Operations */

static void bucket_add(TimerBucket *bucket, TimerEntry *entry) {
    entry->prev = bucket->tail;
    entry->next = NULL;

    if (bucket->tail) {
        bucket->tail->next = entry;
    } else {
        bucket->head = entry;
    }
    bucket->tail = entry;
}

static void bucket_remove(TimerBucket *bucket, TimerEntry *entry) {
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        bucket->head = entry->next;
    }

    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        bucket->tail = entry->prev;
    }

    entry->next = NULL;
    entry->prev = NULL;
}

/* Timer Wheel Lifecycle */

TimerWheel *timer_wheel_new(const TimerConfig *config) {
    TimerWheel *wheel = malloc(sizeof(TimerWheel));
    if (!wheel) return NULL;

    TimerConfig cfg = config ? *config : timer_config_default();

    wheel->wheel_size = cfg.wheel_size;
    wheel->tick_ms = cfg.tick_ms;
    wheel->current_slot = 0;
    wheel->current_time_ms = timer_current_time_ms();
    wheel->free_list = NULL;
    wheel->allocated = 0;
    atomic_init(&wheel->min_deadline, 0);

    wheel->buckets = calloc(wheel->wheel_size, sizeof(TimerBucket));
    if (!wheel->buckets) {
        free(wheel);
        return NULL;
    }

    pthread_mutex_init(&wheel->lock, NULL);

    return wheel;
}

void timer_wheel_free(TimerWheel *wheel) {
    if (!wheel) return;

    pthread_mutex_lock(&wheel->lock);

    for (size_t i = 0; i < wheel->wheel_size; i++) {
        TimerEntry *entry = wheel->buckets[i].head;
        while (entry) {
            TimerEntry *next = entry->next;
            free(entry);
            entry = next;
        }
    }

    TimerEntry *entry = wheel->free_list;
    while (entry) {
        TimerEntry *next = entry->next;
        free(entry);
        entry = next;
    }

    free(wheel->buckets);
    pthread_mutex_unlock(&wheel->lock);
    pthread_mutex_destroy(&wheel->lock);
    free(wheel);
}

/* Timer Operations */

TimerEntry *timer_add(TimerWheel *wheel, Pid block_pid, uint64_t timeout_ms,
                      TimerCallback callback, void *ctx) {
    if (!wheel) return NULL;

    pthread_mutex_lock(&wheel->lock);

    TimerEntry *entry = timer_entry_alloc(wheel);
    if (!entry) {
        pthread_mutex_unlock(&wheel->lock);
        return NULL;
    }

    uint64_t now = timer_current_time_ms();
    entry->block_pid = block_pid;
    entry->callback = callback;
    entry->callback_ctx = ctx;
    entry->cancelled = false;

    /* Check for deadline overflow - cap at max uint64 */
    if (timeout_ms > UINT64_MAX - now) {
        entry->deadline_ms = UINT64_MAX;
    } else {
        entry->deadline_ms = now + timeout_ms;
    }

    uint64_t ticks = timeout_ms / wheel->tick_ms;
    if (ticks == 0) ticks = 1;

    size_t slot = (wheel->current_slot + ticks) % wheel->wheel_size;
    entry->slot = slot;  /* Store slot for O(1) removal */

    bucket_add(&wheel->buckets[slot], entry);

    /* Update min_deadline atomically using CAS loop.
     * This prevents concurrent timer_add calls from losing updates. */
    uint64_t current_min = atomic_load_explicit(&wheel->min_deadline, memory_order_relaxed);
    while (current_min == 0 || entry->deadline_ms < current_min) {
        if (atomic_compare_exchange_weak_explicit(
                &wheel->min_deadline, &current_min, entry->deadline_ms,
                memory_order_relaxed, memory_order_relaxed)) {
            break;
        }
        /* CAS failed, current_min was updated - check again */
    }

    pthread_mutex_unlock(&wheel->lock);
    return entry;
}

bool timer_cancel(TimerWheel *wheel, TimerEntry *entry) {
    if (!wheel || !entry) return false;

    pthread_mutex_lock(&wheel->lock);

    if (entry->cancelled) {
        pthread_mutex_unlock(&wheel->lock);
        return false;
    }

    entry->cancelled = true;

    /* O(1) removal using stored slot index */
    size_t slot = entry->slot;
    if (slot < wheel->wheel_size) {
        bucket_remove(&wheel->buckets[slot], entry);
        timer_entry_free(wheel, entry);
    }

    pthread_mutex_unlock(&wheel->lock);
    return true;
}

TimerEntry *timer_tick(TimerWheel *wheel, uint64_t current_time_ms, size_t *fired_count) {
    if (!wheel || !fired_count) return NULL;

    pthread_mutex_lock(&wheel->lock);

    *fired_count = 0;
    TimerEntry *fired_head = NULL;
    TimerEntry *fired_tail = NULL;
    uint64_t new_min_deadline = 0;
    bool found_pending = false;

    uint64_t elapsed = current_time_ms - wheel->current_time_ms;
    size_t ticks = (size_t)(elapsed / wheel->tick_ms);
    if (ticks == 0 && elapsed > 0) ticks = 1;

    if (ticks > wheel->wheel_size) {
        ticks = wheel->wheel_size;
    }

    for (size_t t = 0; t < ticks; t++) {
        wheel->current_slot = (wheel->current_slot + 1) % wheel->wheel_size;
        TimerBucket *bucket = &wheel->buckets[wheel->current_slot];

        TimerEntry *entry = bucket->head;
        while (entry) {
            TimerEntry *next = entry->next;

            if (!entry->cancelled && entry->deadline_ms <= current_time_ms) {
                bucket_remove(bucket, entry);

                entry->next = NULL;
                entry->prev = fired_tail;
                if (fired_tail) {
                    fired_tail->next = entry;
                } else {
                    fired_head = entry;
                }
                fired_tail = entry;
                (*fired_count)++;
            } else if (!entry->cancelled && entry->deadline_ms > current_time_ms) {
                uint64_t remaining = entry->deadline_ms - current_time_ms;
                size_t ticks_remaining = (size_t)(remaining / wheel->tick_ms);
                if (ticks_remaining == 0) ticks_remaining = 1;

                size_t new_slot = (wheel->current_slot + ticks_remaining) % wheel->wheel_size;
                if (new_slot != wheel->current_slot) {
                    bucket_remove(bucket, entry);
                    entry->slot = new_slot;  /* Update slot after move */
                    bucket_add(&wheel->buckets[new_slot], entry);
                }

                /* Track minimum deadline for remaining timers */
                if (!found_pending || entry->deadline_ms < new_min_deadline) {
                    new_min_deadline = entry->deadline_ms;
                    found_pending = true;
                }
            } else if (entry->cancelled) {
                bucket_remove(bucket, entry);
                timer_entry_free(wheel, entry);
            }

            entry = next;
        }
    }

    /* Recalculate min_deadline if timers fired (only when needed) */
    if (*fired_count > 0) {
        /* Scan all buckets to find new minimum (only after firing) */
        for (size_t i = 0; i < wheel->wheel_size; i++) {
            TimerEntry *e = wheel->buckets[i].head;
            while (e) {
                if (!e->cancelled) {
                    if (!found_pending || e->deadline_ms < new_min_deadline) {
                        new_min_deadline = e->deadline_ms;
                        found_pending = true;
                    }
                }
                e = e->next;
            }
        }
        atomic_store_explicit(&wheel->min_deadline, found_pending ? new_min_deadline : 0, memory_order_relaxed);
    }

    wheel->current_time_ms = current_time_ms;

    pthread_mutex_unlock(&wheel->lock);
    return fired_head;
}

uint64_t timer_next_deadline(const TimerWheel *wheel) {
    if (!wheel) return 0;
    /* O(1) access using cached min_deadline */
    return atomic_load_explicit(&((TimerWheel *)wheel)->min_deadline, memory_order_relaxed);
}

bool timer_has_pending(const TimerWheel *wheel) {
    if (!wheel) return false;

    pthread_mutex_lock((pthread_mutex_t *)&wheel->lock);

    for (size_t i = 0; i < wheel->wheel_size; i++) {
        if (wheel->buckets[i].head) {
            pthread_mutex_unlock((pthread_mutex_t *)&wheel->lock);
            return true;
        }
    }

    pthread_mutex_unlock((pthread_mutex_t *)&wheel->lock);
    return false;
}
