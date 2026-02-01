/*
 * Agim - Block Monitoring Tests
 *
 * P1.1.4.4: Tests for block monitoring operations.
 * - block_monitor adds monitor
 * - block_demonitor removes monitor
 * - block_add_monitored_by
 * - block_remove_monitored_by
 * - block_get_monitors
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"

/*
 * Test: Block starts with no monitors
 */
void test_monitoring_initially_empty(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    size_t count;
    const Pid *monitors = block_get_monitors(block, &count);

    ASSERT_EQ(0, count);
    ASSERT(monitors == NULL);
    ASSERT_EQ(0, block->monitored_by_count);

    block_free(block);
}

/*
 * Test: block_monitor adds single monitor
 */
void test_monitoring_add_single(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    bool monitored = block_monitor(block, 2);
    ASSERT(monitored);

    size_t count;
    const Pid *monitors = block_get_monitors(block, &count);
    ASSERT_EQ(1, count);
    ASSERT_EQ(2, monitors[0]);

    block_free(block);
}

/*
 * Test: block_monitor adds multiple monitors
 */
void test_monitoring_add_multiple(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_monitor(block, 2);
    block_monitor(block, 3);
    block_monitor(block, 4);

    size_t count;
    const Pid *monitors = block_get_monitors(block, &count);
    ASSERT_EQ(3, count);

    /* Verify all monitors present */
    int found_2 = 0, found_3 = 0, found_4 = 0;
    for (size_t i = 0; i < count; i++) {
        if (monitors[i] == 2) found_2 = 1;
        if (monitors[i] == 3) found_3 = 1;
        if (monitors[i] == 4) found_4 = 1;
    }
    ASSERT(found_2);
    ASSERT(found_3);
    ASSERT(found_4);

    block_free(block);
}

/*
 * Test: block_monitor is idempotent
 */
void test_monitoring_idempotent(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_monitor(block, 2);
    block_monitor(block, 2);  /* Monitor again */
    block_monitor(block, 2);  /* And again */

    size_t count;
    block_get_monitors(block, &count);
    ASSERT_EQ(1, count);  /* Should only have one monitor */

    block_free(block);
}

/*
 * Test: block_monitor with NULL block fails
 */
void test_monitoring_null_block(void) {
    bool monitored = block_monitor(NULL, 2);
    ASSERT(!monitored);
}

/*
 * Test: block_monitor with PID_INVALID fails
 */
void test_monitoring_invalid_pid(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    bool monitored = block_monitor(block, PID_INVALID);
    ASSERT(!monitored);

    size_t count;
    block_get_monitors(block, &count);
    ASSERT_EQ(0, count);

    block_free(block);
}

/*
 * Test: block_demonitor removes monitor
 */
void test_demonitor_removes(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_monitor(block, 2);
    block_monitor(block, 3);
    block_monitor(block, 4);

    block_demonitor(block, 3);

    size_t count;
    const Pid *monitors = block_get_monitors(block, &count);
    ASSERT_EQ(2, count);

    /* Verify 3 is not present */
    for (size_t i = 0; i < count; i++) {
        ASSERT(monitors[i] != 3);
    }

    block_free(block);
}

/*
 * Test: block_demonitor non-existent is no-op
 */
void test_demonitor_nonexistent(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_monitor(block, 2);
    block_monitor(block, 3);

    block_demonitor(block, 99);  /* Non-existent */

    size_t count;
    block_get_monitors(block, &count);
    ASSERT_EQ(2, count);

    block_free(block);
}

/*
 * Test: block_demonitor with NULL block is safe
 */
void test_demonitor_null_block(void) {
    block_demonitor(NULL, 2);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: block_demonitor all monitors
 */
void test_demonitor_all(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_monitor(block, 2);
    block_monitor(block, 3);
    block_monitor(block, 4);

    block_demonitor(block, 2);
    block_demonitor(block, 3);
    block_demonitor(block, 4);

    size_t count;
    block_get_monitors(block, &count);
    ASSERT_EQ(0, count);

    block_free(block);
}

/*
 * Test: block_get_monitors with NULL block
 */
void test_get_monitors_null_block(void) {
    size_t count = 999;
    const Pid *monitors = block_get_monitors(NULL, &count);

    ASSERT(monitors == NULL);
    ASSERT_EQ(0, count);
}

/*
 * Test: block_add_monitored_by adds entry
 */
void test_add_monitored_by(void) {
    Block *block = block_new(1, "target", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(0, block->monitored_by_count);

    bool added = block_add_monitored_by(block, 2);
    ASSERT(added);
    ASSERT_EQ(1, block->monitored_by_count);
    ASSERT_EQ(2, block->monitored_by[0]);

    block_free(block);
}

/*
 * Test: block_add_monitored_by multiple
 */
void test_add_monitored_by_multiple(void) {
    Block *block = block_new(1, "target", NULL);
    ASSERT(block != NULL);

    block_add_monitored_by(block, 2);
    block_add_monitored_by(block, 3);
    block_add_monitored_by(block, 4);

    ASSERT_EQ(3, block->monitored_by_count);

    block_free(block);
}

/*
 * Test: block_add_monitored_by with NULL fails
 */
void test_add_monitored_by_null(void) {
    bool added = block_add_monitored_by(NULL, 2);
    ASSERT(!added);
}

/*
 * Test: block_add_monitored_by with PID_INVALID fails
 */
void test_add_monitored_by_invalid_pid(void) {
    Block *block = block_new(1, "target", NULL);
    ASSERT(block != NULL);

    bool added = block_add_monitored_by(block, PID_INVALID);
    ASSERT(!added);
    ASSERT_EQ(0, block->monitored_by_count);

    block_free(block);
}

/*
 * Test: block_remove_monitored_by removes entry
 */
void test_remove_monitored_by(void) {
    Block *block = block_new(1, "target", NULL);
    ASSERT(block != NULL);

    block_add_monitored_by(block, 2);
    block_add_monitored_by(block, 3);
    block_add_monitored_by(block, 4);

    block_remove_monitored_by(block, 3);

    ASSERT_EQ(2, block->monitored_by_count);

    /* Verify 3 is not present */
    for (uint32_t i = 0; i < block->monitored_by_count; i++) {
        ASSERT(block->monitored_by[i] != 3);
    }

    block_free(block);
}

/*
 * Test: block_remove_monitored_by non-existent is no-op
 */
void test_remove_monitored_by_nonexistent(void) {
    Block *block = block_new(1, "target", NULL);
    ASSERT(block != NULL);

    block_add_monitored_by(block, 2);

    block_remove_monitored_by(block, 99);  /* Non-existent */

    ASSERT_EQ(1, block->monitored_by_count);

    block_free(block);
}

/*
 * Test: block_remove_monitored_by with NULL is safe
 */
void test_remove_monitored_by_null(void) {
    block_remove_monitored_by(NULL, 2);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: Monitor array grows
 */
void test_monitoring_array_growth(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* Monitor many PIDs to trigger growth */
    for (Pid p = 2; p <= 20; p++) {
        bool monitored = block_monitor(block, p);
        ASSERT(monitored);
    }

    size_t count;
    block_get_monitors(block, &count);
    ASSERT_EQ(19, count);

    block_free(block);
}

/*
 * Test: monitored_by array grows
 */
void test_monitored_by_array_growth(void) {
    Block *block = block_new(1, "target", NULL);
    ASSERT(block != NULL);

    /* Add many monitors to trigger growth */
    for (Pid p = 2; p <= 20; p++) {
        bool added = block_add_monitored_by(block, p);
        ASSERT(added);
    }

    ASSERT_EQ(19, block->monitored_by_count);

    block_free(block);
}

/*
 * Test: Monitor and demonitor interleaved
 */
void test_monitoring_interleaved(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_monitor(block, 2);
    block_monitor(block, 3);
    block_demonitor(block, 2);
    block_monitor(block, 4);
    block_demonitor(block, 3);
    block_monitor(block, 5);

    size_t count;
    const Pid *monitors = block_get_monitors(block, &count);
    ASSERT_EQ(2, count);

    /* Should have 4 and 5 */
    int found_4 = 0, found_5 = 0;
    for (size_t i = 0; i < count; i++) {
        if (monitors[i] == 4) found_4 = 1;
        if (monitors[i] == 5) found_5 = 1;
    }
    ASSERT(found_4);
    ASSERT(found_5);

    block_free(block);
}

/*
 * Test: Monitor count accuracy
 */
void test_monitoring_count_accuracy(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(0, block->monitor_count);

    block_monitor(block, 2);
    ASSERT_EQ(1, block->monitor_count);

    block_monitor(block, 3);
    ASSERT_EQ(2, block->monitor_count);

    block_monitor(block, 3);  /* Duplicate */
    ASSERT_EQ(2, block->monitor_count);

    block_demonitor(block, 2);
    ASSERT_EQ(1, block->monitor_count);

    block_demonitor(block, 3);
    ASSERT_EQ(0, block->monitor_count);

    block_free(block);
}

/*
 * Test: Monitors vs monitored_by are independent
 */
void test_monitoring_independence(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* This block monitors others */
    block_monitor(block, 10);
    block_monitor(block, 11);

    /* This block is monitored by others */
    block_add_monitored_by(block, 20);
    block_add_monitored_by(block, 21);
    block_add_monitored_by(block, 22);

    size_t count;
    block_get_monitors(block, &count);
    ASSERT_EQ(2, count);  /* Monitors */
    ASSERT_EQ(3, block->monitored_by_count);  /* Monitored by */

    block_free(block);
}

/*
 * Test: Self-monitoring (block monitoring itself)
 */
void test_monitoring_self(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* Self-monitoring is technically allowed at the block level */
    bool monitored = block_monitor(block, 1);
    ASSERT(monitored);

    size_t count;
    const Pid *monitors = block_get_monitors(block, &count);
    ASSERT_EQ(1, count);
    ASSERT_EQ(1, monitors[0]);

    block_free(block);
}

/*
 * Test: Large number of monitors
 */
void test_monitoring_many(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* Add 100 monitors */
    for (Pid p = 2; p <= 101; p++) {
        bool monitored = block_monitor(block, p);
        ASSERT(monitored);
    }

    size_t count;
    block_get_monitors(block, &count);
    ASSERT_EQ(100, count);

    block_free(block);
}

/*
 * Test: Large number of monitored_by
 */
void test_monitored_by_many(void) {
    Block *block = block_new(1, "target", NULL);
    ASSERT(block != NULL);

    /* Add 100 monitored_by entries */
    for (Pid p = 2; p <= 101; p++) {
        bool added = block_add_monitored_by(block, p);
        ASSERT(added);
    }

    ASSERT_EQ(100, block->monitored_by_count);

    block_free(block);
}

/*
 * Test: block_add_monitored_by is idempotent
 */
void test_add_monitored_by_idempotent(void) {
    Block *block = block_new(1, "target", NULL);
    ASSERT(block != NULL);

    block_add_monitored_by(block, 2);
    block_add_monitored_by(block, 2);  /* Add again */
    block_add_monitored_by(block, 2);  /* And again */

    ASSERT_EQ(1, block->monitored_by_count);

    block_free(block);
}

int main(void) {
    printf("Running block monitoring tests...\n");

    printf("\nInitial state tests:\n");
    RUN_TEST(test_monitoring_initially_empty);

    printf("\nblock_monitor tests:\n");
    RUN_TEST(test_monitoring_add_single);
    RUN_TEST(test_monitoring_add_multiple);
    RUN_TEST(test_monitoring_idempotent);
    RUN_TEST(test_monitoring_null_block);
    RUN_TEST(test_monitoring_invalid_pid);

    printf("\nblock_demonitor tests:\n");
    RUN_TEST(test_demonitor_removes);
    RUN_TEST(test_demonitor_nonexistent);
    RUN_TEST(test_demonitor_null_block);
    RUN_TEST(test_demonitor_all);

    printf("\nblock_get_monitors tests:\n");
    RUN_TEST(test_get_monitors_null_block);

    printf("\nblock_add_monitored_by tests:\n");
    RUN_TEST(test_add_monitored_by);
    RUN_TEST(test_add_monitored_by_multiple);
    RUN_TEST(test_add_monitored_by_null);
    RUN_TEST(test_add_monitored_by_invalid_pid);
    RUN_TEST(test_add_monitored_by_idempotent);

    printf("\nblock_remove_monitored_by tests:\n");
    RUN_TEST(test_remove_monitored_by);
    RUN_TEST(test_remove_monitored_by_nonexistent);
    RUN_TEST(test_remove_monitored_by_null);

    printf("\nArray growth tests:\n");
    RUN_TEST(test_monitoring_array_growth);
    RUN_TEST(test_monitored_by_array_growth);

    printf("\nMixed operations tests:\n");
    RUN_TEST(test_monitoring_interleaved);
    RUN_TEST(test_monitoring_count_accuracy);
    RUN_TEST(test_monitoring_independence);
    RUN_TEST(test_monitoring_self);

    printf("\nLarge scale tests:\n");
    RUN_TEST(test_monitoring_many);
    RUN_TEST(test_monitored_by_many);

    return TEST_RESULT();
}
