/*
 * Agim - Timer Wheel Tests
 *
 * P1.1.7.1: Tests for timer wheel operations.
 * - timer_wheel_new creates wheel
 * - timer_add schedules timers
 * - timer_cancel cancels timers
 * - timer_tick fires expired timers
 * - timer_next_deadline returns correct deadline
 * - timer_has_pending checks for pending timers
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/timer.h"

#include <stdlib.h>

/* Helper: Process and free fired entries from timer_tick */
static size_t process_fired_entries(TimerEntry *fired) {
    size_t count = 0;
    TimerEntry *entry = fired;
    while (entry) {
        TimerEntry *next = entry->next;
        /* Call the callback if present */
        if (entry->callback) {
            entry->callback(entry->callback_ctx, entry->block_pid);
        }
        free(entry);  /* Free the fired entry */
        entry = next;
        count++;
    }
    return count;
}

/* Test callback tracking */
static int g_callback_count = 0;
static Pid g_last_callback_pid = 0;
static void *g_last_callback_ctx = NULL;

static void test_callback(void *ctx, Pid block_pid) {
    g_callback_count++;
    g_last_callback_pid = block_pid;
    g_last_callback_ctx = ctx;
}

static void reset_callback_tracking(void) {
    g_callback_count = 0;
    g_last_callback_pid = 0;
    g_last_callback_ctx = NULL;
}

/*
 * Test: timer_config_default returns valid config
 */
void test_config_default(void) {
    TimerConfig config = timer_config_default();

    ASSERT(config.wheel_size > 0);
    ASSERT(config.tick_ms > 0);
}

/*
 * Test: timer_wheel_new creates wheel
 */
void test_wheel_new(void) {
    TimerConfig config = timer_config_default();
    TimerWheel *wheel = timer_wheel_new(&config);

    ASSERT(wheel != NULL);
    ASSERT(wheel->buckets != NULL);
    ASSERT_EQ(config.wheel_size, wheel->wheel_size);
    ASSERT_EQ(config.tick_ms, wheel->tick_ms);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_wheel_new with NULL config uses defaults
 */
void test_wheel_new_null_config(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);

    ASSERT(wheel != NULL);
    ASSERT(wheel->wheel_size > 0);
    ASSERT(wheel->tick_ms > 0);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_wheel_new with custom config
 */
void test_wheel_new_custom_config(void) {
    TimerConfig config = {
        .wheel_size = 128,
        .tick_ms = 5,
    };
    TimerWheel *wheel = timer_wheel_new(&config);

    ASSERT(wheel != NULL);
    ASSERT_EQ(128, wheel->wheel_size);
    ASSERT_EQ(5, wheel->tick_ms);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_wheel_free handles NULL
 */
void test_wheel_free_null(void) {
    timer_wheel_free(NULL);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: New wheel has no pending timers
 */
void test_wheel_initially_empty(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    ASSERT(!timer_has_pending(wheel));

    timer_wheel_free(wheel);
}

/*
 * Test: timer_add returns valid entry
 */
void test_timer_add(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    TimerEntry *entry = timer_add(wheel, 1, 100, test_callback, NULL);
    ASSERT(entry != NULL);
    ASSERT_EQ(1, entry->block_pid);
    ASSERT(!entry->cancelled);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_add with callback and context
 */
void test_timer_add_with_context(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    int ctx_value = 42;
    TimerEntry *entry = timer_add(wheel, 1, 100, test_callback, &ctx_value);
    ASSERT(entry != NULL);
    ASSERT_EQ(&ctx_value, entry->callback_ctx);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_add multiple timers
 */
void test_timer_add_multiple(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    TimerEntry *e1 = timer_add(wheel, 1, 100, test_callback, NULL);
    TimerEntry *e2 = timer_add(wheel, 2, 200, test_callback, NULL);
    TimerEntry *e3 = timer_add(wheel, 3, 300, test_callback, NULL);

    ASSERT(e1 != NULL);
    ASSERT(e2 != NULL);
    ASSERT(e3 != NULL);
    ASSERT(e1 != e2);
    ASSERT(e2 != e3);

    ASSERT(timer_has_pending(wheel));

    timer_wheel_free(wheel);
}

/*
 * Test: timer_add sets deadline correctly
 */
void test_timer_add_deadline(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    uint64_t before = timer_current_time_ms();
    TimerEntry *entry = timer_add(wheel, 1, 100, test_callback, NULL);
    uint64_t after = timer_current_time_ms();

    ASSERT(entry != NULL);
    /* Deadline should be approximately current_time + timeout */
    ASSERT(entry->deadline_ms >= before + 100);
    ASSERT(entry->deadline_ms <= after + 100 + 10);  /* Allow small variance */

    timer_wheel_free(wheel);
}

/*
 * Test: timer_cancel cancels timer
 */
void test_timer_cancel(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    TimerEntry *entry = timer_add(wheel, 1, 1000, test_callback, NULL);
    ASSERT(entry != NULL);
    ASSERT(!entry->cancelled);

    bool cancelled = timer_cancel(wheel, entry);
    ASSERT(cancelled);

    /* Entry is now on free list - don't access it */

    timer_wheel_free(wheel);
}

/*
 * Test: timer_cancel with NULL entry
 */
void test_timer_cancel_null_entry(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    bool cancelled = timer_cancel(wheel, NULL);
    ASSERT(!cancelled);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_cancel with NULL wheel
 */
void test_timer_cancel_null_wheel(void) {
    bool cancelled = timer_cancel(NULL, NULL);
    ASSERT(!cancelled);
}

/*
 * Test: timer_tick fires expired timer
 */
void test_timer_tick_fires(void) {
    reset_callback_tracking();

    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    /* Add timer with 0ms timeout (fires immediately) */
    timer_add(wheel, 42, 0, test_callback, NULL);

    uint64_t now = timer_current_time_ms();
    size_t fired_count = 0;
    TimerEntry *fired = timer_tick(wheel, now + 100, &fired_count);

    /* Process the fired entries (calls callbacks and frees them) */
    size_t processed = process_fired_entries(fired);

    ASSERT(fired_count >= 1);
    ASSERT(processed >= 1);
    ASSERT(g_callback_count >= 1);
    ASSERT_EQ(42, g_last_callback_pid);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_tick doesn't fire future timers
 */
void test_timer_tick_no_early_fire(void) {
    reset_callback_tracking();

    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    /* Add timer with large timeout */
    uint64_t now = timer_current_time_ms();
    timer_add(wheel, 1, 10000, test_callback, NULL);  /* 10 seconds */

    size_t fired_count = 0;
    TimerEntry *fired = timer_tick(wheel, now, &fired_count);
    process_fired_entries(fired);

    ASSERT_EQ(0, fired_count);
    ASSERT_EQ(0, g_callback_count);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_tick fires multiple expired timers
 */
void test_timer_tick_fires_multiple(void) {
    reset_callback_tracking();

    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    /* Add multiple timers with 0ms timeout */
    timer_add(wheel, 1, 0, test_callback, NULL);
    timer_add(wheel, 2, 0, test_callback, NULL);
    timer_add(wheel, 3, 0, test_callback, NULL);

    uint64_t now = timer_current_time_ms();
    size_t fired_count = 0;
    TimerEntry *fired = timer_tick(wheel, now + 100, &fired_count);
    process_fired_entries(fired);

    ASSERT(fired_count >= 3);
    ASSERT(g_callback_count >= 3);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_tick passes context to callback
 */
void test_timer_tick_callback_context(void) {
    reset_callback_tracking();

    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    int ctx_value = 99;
    timer_add(wheel, 1, 0, test_callback, &ctx_value);

    uint64_t now = timer_current_time_ms();
    size_t fired_count = 0;
    TimerEntry *fired = timer_tick(wheel, now + 100, &fired_count);
    process_fired_entries(fired);

    ASSERT_EQ(&ctx_value, g_last_callback_ctx);

    timer_wheel_free(wheel);
}

/*
 * Test: timer_tick with NULL wheel
 */
void test_timer_tick_null_wheel(void) {
    size_t fired = 0;
    TimerEntry *result = timer_tick(NULL, 0, &fired);
    ASSERT(result == NULL);
    ASSERT_EQ(0, fired);
}

/*
 * Test: timer_next_deadline returns correct value
 */
void test_timer_next_deadline(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    /* No timers - should return 0 */
    uint64_t no_timer_deadline = timer_next_deadline(wheel);
    ASSERT_EQ(0, no_timer_deadline);

    /* Add a timer */
    uint64_t now = timer_current_time_ms();
    timer_add(wheel, 1, 100, test_callback, NULL);

    uint64_t deadline = timer_next_deadline(wheel);
    ASSERT(deadline > 0);
    ASSERT(deadline >= now);
    ASSERT(deadline <= now + 150);  /* Allow some variance */

    timer_wheel_free(wheel);
}

/*
 * Test: timer_has_pending detects pending timers
 */
void test_timer_has_pending(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    ASSERT(!timer_has_pending(wheel));

    timer_add(wheel, 1, 1000, test_callback, NULL);
    ASSERT(timer_has_pending(wheel));

    timer_wheel_free(wheel);
}

/*
 * Test: timer_current_time_ms returns reasonable value
 */
void test_timer_current_time(void) {
    uint64_t t1 = timer_current_time_ms();
    uint64_t t2 = timer_current_time_ms();

    /* Time should be positive and non-decreasing */
    ASSERT(t1 > 0);
    ASSERT(t2 >= t1);
}

/*
 * Test: timer_has_pending with NULL wheel
 */
void test_has_pending_null_wheel(void) {
    bool pending = timer_has_pending(NULL);
    ASSERT(!pending);
}

/*
 * Test: timer_next_deadline with NULL wheel
 */
void test_next_deadline_null_wheel(void) {
    uint64_t deadline = timer_next_deadline(NULL);
    ASSERT_EQ(0, deadline);
}

/*
 * Test: Add timer to NULL wheel
 */
void test_timer_add_null_wheel(void) {
    TimerEntry *entry = timer_add(NULL, 1, 100, test_callback, NULL);
    ASSERT(entry == NULL);
}

/*
 * Test: Large number of timers
 */
void test_timer_many_timers(void) {
    TimerWheel *wheel = timer_wheel_new(NULL);
    ASSERT(wheel != NULL);

    /* Add 100 timers with large timeouts so they don't fire */
    for (int i = 0; i < 100; i++) {
        TimerEntry *entry = timer_add(wheel, (Pid)(i + 1), 10000 + (uint64_t)(i * 10), test_callback, NULL);
        ASSERT(entry != NULL);
    }

    ASSERT(timer_has_pending(wheel));

    timer_wheel_free(wheel);
}

/*
 * Test: Timers in same slot (small wheel)
 */
void test_timers_same_slot(void) {
    reset_callback_tracking();

    TimerConfig config = {
        .wheel_size = 4,  /* Small wheel to force collisions */
        .tick_ms = 100,
    };
    TimerWheel *wheel = timer_wheel_new(&config);
    ASSERT(wheel != NULL);

    /* Add multiple timers with 0ms timeout */
    timer_add(wheel, 1, 0, test_callback, NULL);
    timer_add(wheel, 2, 0, test_callback, NULL);
    timer_add(wheel, 3, 0, test_callback, NULL);

    uint64_t now = timer_current_time_ms();
    size_t fired = 0;
    TimerEntry *fired_entries = timer_tick(wheel, now + 1000, &fired);
    process_fired_entries(fired_entries);

    ASSERT(fired >= 3);
    ASSERT(g_callback_count >= 3);

    timer_wheel_free(wheel);
}

int main(void) {
    printf("Running timer wheel tests...\n");

    printf("\nConfiguration tests:\n");
    RUN_TEST(test_config_default);

    printf("\nWheel lifecycle tests:\n");
    RUN_TEST(test_wheel_new);
    RUN_TEST(test_wheel_new_null_config);
    RUN_TEST(test_wheel_new_custom_config);
    RUN_TEST(test_wheel_free_null);
    RUN_TEST(test_wheel_initially_empty);

    printf("\ntimer_add tests:\n");
    RUN_TEST(test_timer_add);
    RUN_TEST(test_timer_add_with_context);
    RUN_TEST(test_timer_add_multiple);
    RUN_TEST(test_timer_add_deadline);
    RUN_TEST(test_timer_add_null_wheel);

    printf("\ntimer_cancel tests:\n");
    RUN_TEST(test_timer_cancel);
    RUN_TEST(test_timer_cancel_null_entry);
    RUN_TEST(test_timer_cancel_null_wheel);

    printf("\ntimer_tick tests:\n");
    RUN_TEST(test_timer_tick_fires);
    RUN_TEST(test_timer_tick_no_early_fire);
    RUN_TEST(test_timer_tick_fires_multiple);
    RUN_TEST(test_timer_tick_callback_context);
    RUN_TEST(test_timer_tick_null_wheel);

    printf("\ntimer_next_deadline tests:\n");
    RUN_TEST(test_timer_next_deadline);
    RUN_TEST(test_next_deadline_null_wheel);

    printf("\ntimer_has_pending tests:\n");
    RUN_TEST(test_timer_has_pending);
    RUN_TEST(test_has_pending_null_wheel);

    printf("\nTime utility tests:\n");
    RUN_TEST(test_timer_current_time);

    printf("\nAdvanced tests:\n");
    RUN_TEST(test_timer_many_timers);
    RUN_TEST(test_timers_same_slot);

    return TEST_RESULT();
}
