/*
 * Agim - End-to-End Timer Tests
 *
 * Tests the timer wheel infrastructure including scheduling, cancellation,
 * tick processing, and timeout handling. Validates Erlang-style timer
 * semantics for receive timeouts.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "../test_common.h"
#include "runtime/timer.h"
#include "runtime/block.h"

#include <unistd.h>

/* Callback tracking */
static int callback_count = 0;
static Pid last_callback_pid = 0;

static void test_callback(void *ctx, Pid block_pid)
{
	(void)ctx;
	callback_count++;
	last_callback_pid = block_pid;
}

static void reset_callback_tracking(void)
{
	callback_count = 0;
	last_callback_pid = 0;
}

/* Test 1: Timer wheel creation */
void test_timer_wheel_creation(void)
{
	TimerConfig config = {
		.wheel_size = 256,
		.tick_ms = 10,
	};

	TimerWheel *wheel = timer_wheel_new(&config);
	ASSERT(wheel != NULL);
	ASSERT_EQ(256, wheel->wheel_size);
	ASSERT_EQ(10, wheel->tick_ms);
	ASSERT(!timer_has_pending(wheel));

	timer_wheel_free(wheel);
}

/* Test 2: Default timer configuration */
void test_default_timer_config(void)
{
	TimerWheel *wheel = timer_wheel_new(NULL);
	ASSERT(wheel != NULL);
	ASSERT(wheel->wheel_size > 0);
	ASSERT(wheel->tick_ms > 0);

	timer_wheel_free(wheel);
}

/* Test 3: Add timer */
void test_timer_add(void)
{
	TimerWheel *wheel = timer_wheel_new(NULL);
	reset_callback_tracking();

	TimerEntry *entry = timer_add(wheel, 100, 50, test_callback, NULL);
	ASSERT(entry != NULL);
	ASSERT_EQ(100, entry->block_pid);
	ASSERT(!entry->cancelled);
	ASSERT(timer_has_pending(wheel));

	timer_wheel_free(wheel);
}

/* Test 4: Timer fires on tick */
void test_timer_fires(void)
{
	TimerConfig config = {
		.wheel_size = 256,
		.tick_ms = 1,
	};
	TimerWheel *wheel = timer_wheel_new(&config);
	reset_callback_tracking();

	uint64_t now = timer_current_time_ms();
	timer_add(wheel, 42, 10, test_callback, NULL);

	/* Advance time past deadline */
	size_t fired_count = 0;
	TimerEntry *fired = timer_tick(wheel, now + 20, &fired_count);

	ASSERT_EQ(1, fired_count);
	ASSERT(fired != NULL);
	ASSERT_EQ(42, fired->block_pid);

	/* Execute callback */
	fired->callback(fired->callback_ctx, fired->block_pid);
	ASSERT_EQ(1, callback_count);
	ASSERT_EQ(42, last_callback_pid);

	timer_wheel_free(wheel);
}

/* Test 5: Cancel timer */
void test_timer_cancel(void)
{
	TimerWheel *wheel = timer_wheel_new(NULL);
	reset_callback_tracking();

	TimerEntry *entry = timer_add(wheel, 100, 100, test_callback, NULL);
	ASSERT(timer_has_pending(wheel));

	/* Cancel before firing - returns true on success */
	ASSERT(timer_cancel(wheel, entry));
	/* Note: After cancel, entry is returned to free list, so we can't
	 * access entry->cancelled safely (it's use-after-free). */

	/* Tick should not fire cancelled timer */
	size_t fired_count = 0;
	uint64_t now = timer_current_time_ms();
	timer_tick(wheel, now + 200, &fired_count);

	ASSERT_EQ(0, fired_count);
	ASSERT_EQ(0, callback_count);

	timer_wheel_free(wheel);
}

/* Test 6: Multiple timers */
void test_multiple_timers(void)
{
	TimerConfig config = {
		.wheel_size = 256,
		.tick_ms = 1,
	};
	TimerWheel *wheel = timer_wheel_new(&config);
	reset_callback_tracking();

	uint64_t now = timer_current_time_ms();

	/* Add timers with different deadlines */
	timer_add(wheel, 1, 10, test_callback, NULL);
	timer_add(wheel, 2, 20, test_callback, NULL);
	timer_add(wheel, 3, 30, test_callback, NULL);

	ASSERT(timer_has_pending(wheel));

	/* Tick at 15ms - only first should fire */
	size_t fired_count = 0;
	TimerEntry *fired = timer_tick(wheel, now + 15, &fired_count);
	ASSERT_EQ(1, fired_count);
	ASSERT_EQ(1, fired->block_pid);

	/* Tick at 25ms - second should fire */
	fired = timer_tick(wheel, now + 25, &fired_count);
	ASSERT_EQ(1, fired_count);
	ASSERT_EQ(2, fired->block_pid);

	/* Tick at 35ms - third should fire */
	fired = timer_tick(wheel, now + 35, &fired_count);
	ASSERT_EQ(1, fired_count);
	ASSERT_EQ(3, fired->block_pid);

	/* No more pending */
	ASSERT(!timer_has_pending(wheel));

	timer_wheel_free(wheel);
}

/* Test 7: Timer with context */
void test_timer_with_context(void)
{
	TimerWheel *wheel = timer_wheel_new(NULL);

	int context_value = 42;
	TimerEntry *entry = timer_add(wheel, 1, 10, test_callback, &context_value);

	ASSERT(entry->callback_ctx == &context_value);

	timer_wheel_free(wheel);
}

/* Test 8: Next deadline calculation */
void test_next_deadline(void)
{
	TimerConfig config = {
		.wheel_size = 256,
		.tick_ms = 1,
	};
	TimerWheel *wheel = timer_wheel_new(&config);

	/* No timers - deadline returns 0 */
	uint64_t deadline = timer_next_deadline(wheel);
	ASSERT_EQ(0, deadline);

	/* Add timer */
	uint64_t now = timer_current_time_ms();
	timer_add(wheel, 1, 50, test_callback, NULL);

	/* Deadline should be around now + 50 */
	deadline = timer_next_deadline(wheel);
	ASSERT(deadline >= now + 40);
	ASSERT(deadline <= now + 60);

	timer_wheel_free(wheel);
}

/* Test 9: Timer already expired */
void test_timer_already_expired(void)
{
	TimerConfig config = {
		.wheel_size = 256,
		.tick_ms = 1,
	};
	TimerWheel *wheel = timer_wheel_new(&config);
	reset_callback_tracking();

	/* Add timer with 0 timeout (immediate) */
	timer_add(wheel, 1, 0, test_callback, NULL);

	/* Should fire immediately */
	size_t fired_count = 0;
	TimerEntry *fired = timer_tick(wheel, timer_current_time_ms() + 1, &fired_count);
	ASSERT_EQ(1, fired_count);
	ASSERT(fired != NULL);

	timer_wheel_free(wheel);
}

/* Test 10: Cancel non-existent timer */
void test_cancel_nonexistent(void)
{
	TimerWheel *wheel = timer_wheel_new(NULL);

	/* Try to cancel NULL */
	ASSERT(!timer_cancel(wheel, NULL));

	timer_wheel_free(wheel);
}

/* Test 11: High volume timers */
void test_high_volume_timers(void)
{
	TimerConfig config = {
		.wheel_size = 512,
		.tick_ms = 1,
	};
	TimerWheel *wheel = timer_wheel_new(&config);
	reset_callback_tracking();

	const int NUM_TIMERS = 100;
	uint64_t now = timer_current_time_ms();

	/* Add many timers */
	for (int i = 0; i < NUM_TIMERS; i++) {
		timer_add(wheel, (Pid)i, (uint64_t)(i * 10), test_callback, NULL);
	}

	ASSERT(timer_has_pending(wheel));

	/* Fire all */
	int total_fired = 0;
	for (int t = 0; t <= NUM_TIMERS * 10; t += 5) {
		size_t fired_count = 0;
		TimerEntry *fired = timer_tick(wheel, now + t, &fired_count);
		while (fired) {
			total_fired++;
			fired->callback(fired->callback_ctx, fired->block_pid);
			TimerEntry *next = fired->next;
			fired = next;
		}
	}

	ASSERT_EQ(NUM_TIMERS, total_fired);
	ASSERT(!timer_has_pending(wheel));

	timer_wheel_free(wheel);
}

/* Test 12: Timer wheel wrap-around */
void test_wheel_wraparound(void)
{
	TimerConfig config = {
		.wheel_size = 8,  /* Small wheel to force wrap */
		.tick_ms = 10,
	};
	TimerWheel *wheel = timer_wheel_new(&config);
	reset_callback_tracking();

	uint64_t now = timer_current_time_ms();

	/* Add timer far in future (will wrap) */
	timer_add(wheel, 1, 1000, test_callback, NULL);

	ASSERT(timer_has_pending(wheel));

	/* Fire at deadline */
	size_t fired_count = 0;
	timer_tick(wheel, now + 1100, &fired_count);
	ASSERT_EQ(1, fired_count);

	timer_wheel_free(wheel);
}

/* Test 13: Current time function */
void test_current_time(void)
{
	uint64_t t1 = timer_current_time_ms();
	usleep(10000);  /* 10ms */
	uint64_t t2 = timer_current_time_ms();

	/* Time should advance */
	ASSERT(t2 > t1);
	ASSERT(t2 - t1 >= 5);  /* At least 5ms elapsed */
}

/* Test 14: Timer entry reuse (free list) */
void test_timer_entry_reuse(void)
{
	TimerWheel *wheel = timer_wheel_new(NULL);
	reset_callback_tracking();

	uint64_t base_time = timer_current_time_ms();

	/* Add and fire first timer */
	timer_add(wheel, 1, 10, test_callback, NULL);
	size_t fired_count = 0;
	timer_tick(wheel, base_time + 20, &fired_count);
	ASSERT_EQ(1, fired_count);

	/* Add another timer - may reuse entry from free list */
	timer_add(wheel, 2, 10, test_callback, NULL);
	timer_tick(wheel, base_time + 40, &fired_count);
	ASSERT_EQ(1, fired_count);

	timer_wheel_free(wheel);
}

/* Test 15: Timer allocation count */
void test_timer_allocation_count(void)
{
	TimerWheel *wheel = timer_wheel_new(NULL);

	size_t initial = wheel->allocated;

	timer_add(wheel, 1, 100, test_callback, NULL);
	timer_add(wheel, 2, 100, test_callback, NULL);
	timer_add(wheel, 3, 100, test_callback, NULL);

	ASSERT_EQ(initial + 3, wheel->allocated);

	timer_wheel_free(wheel);
}

int main(void)
{
	printf("=== E2E Timer Tests ===\n\n");

	RUN_TEST(test_timer_wheel_creation);
	RUN_TEST(test_default_timer_config);
	RUN_TEST(test_timer_add);
	RUN_TEST(test_timer_fires);
	RUN_TEST(test_timer_cancel);
	RUN_TEST(test_multiple_timers);
	RUN_TEST(test_timer_with_context);
	RUN_TEST(test_next_deadline);
	RUN_TEST(test_timer_already_expired);
	RUN_TEST(test_cancel_nonexistent);
	RUN_TEST(test_high_volume_timers);
	RUN_TEST(test_wheel_wraparound);
	RUN_TEST(test_current_time);
	RUN_TEST(test_timer_entry_reuse);
	RUN_TEST(test_timer_allocation_count);

	return TEST_RESULT();
}
