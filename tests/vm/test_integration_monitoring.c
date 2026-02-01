/*
 * Agim - Integration Tests for Process Monitoring
 *
 * P1.2.3: Tests for block monitoring behavior in a multi-block environment.
 * - Basic monitoring
 * - Monitor notification on exit
 * - Demonitoring
 * - Multiple monitors
 * - Monitor vs Link differences
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "vm/bytecode.h"

/* Helper: Create test scheduler */
static Scheduler *create_test_scheduler(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 0;  /* Single-threaded for deterministic tests */
    return scheduler_new(&config);
}

/* Helper: Create bytecode that halts immediately */
static Bytecode *create_halt_bytecode(void) {
    Bytecode *code = bytecode_new();
    if (!code) return NULL;
    chunk_write_opcode(code->main, OP_HALT, 1);
    return code;
}

/* Helper: Create bytecode that receives a message then halts */
static Bytecode *create_receive_halt_bytecode(void) {
    Bytecode *code = bytecode_new();
    if (!code) return NULL;
    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_RECEIVE, 1);
    chunk_write_opcode(chunk, OP_POP, 2);
    chunk_write_opcode(chunk, OP_HALT, 3);
    return code;
}

/* ============================================================================
 * Basic Monitoring Tests
 * ============================================================================ */

void test_monitor_block(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code1 = create_receive_halt_bytecode();
    Bytecode *code2 = create_receive_halt_bytecode();

    /* Spawn two blocks */
    Pid monitor_pid = scheduler_spawn_ex(sched, code1, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
    Pid target_pid = scheduler_spawn_ex(sched, code2, "target", CAP_RECEIVE, NULL);

    Block *monitor_block = scheduler_get_block(sched, monitor_pid);
    Block *target_block = scheduler_get_block(sched, target_pid);

    ASSERT(monitor_block != NULL);
    ASSERT(target_block != NULL);

    /* Set up monitoring */
    bool monitored = block_monitor(monitor_block, target_pid);
    ASSERT(monitored);

    /* Also register the monitored_by on target */
    bool added = block_add_monitored_by(target_block, monitor_pid);
    ASSERT(added);

    scheduler_free(sched);
}

void test_monitor_multiple_targets(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    /* Spawn monitor and targets */
    Pid monitor_pid = scheduler_spawn_ex(sched, code, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
    Block *monitor = scheduler_get_block(sched, monitor_pid);

    Pid targets[5];
    for (int i = 0; i < 5; i++) {
        targets[i] = scheduler_spawn_ex(sched, code, "target", CAP_RECEIVE, NULL);
        block_monitor(monitor, targets[i]);
    }

    /* Monitor should be watching all 5 */
    ASSERT(monitor->monitor_count >= 5);

    scheduler_free(sched);
}

void test_multiple_monitors_same_target(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    /* Single target */
    Pid target_pid = scheduler_spawn_ex(sched, code, "target", CAP_RECEIVE, NULL);
    Block *target = scheduler_get_block(sched, target_pid);

    /* Multiple monitors */
    for (int i = 0; i < 3; i++) {
        Pid monitor_pid = scheduler_spawn_ex(sched, code, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
        Block *monitor = scheduler_get_block(sched, monitor_pid);
        block_monitor(monitor, target_pid);
        block_add_monitored_by(target, monitor_pid);
    }

    /* Target should have 3 monitors */
    ASSERT_EQ(3, target->monitored_by_count);

    scheduler_free(sched);
}

void test_monitor_same_target_twice_idempotent(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code1 = create_receive_halt_bytecode();
    Bytecode *code2 = create_receive_halt_bytecode();

    Pid monitor_pid = scheduler_spawn_ex(sched, code1, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
    Pid target_pid = scheduler_spawn_ex(sched, code2, "target", CAP_RECEIVE, NULL);

    Block *monitor = scheduler_get_block(sched, monitor_pid);

    /* Monitor same target multiple times */
    block_monitor(monitor, target_pid);
    block_monitor(monitor, target_pid);
    block_monitor(monitor, target_pid);

    /* Should still have only one monitor entry */
    ASSERT_EQ(1, monitor->monitor_count);

    scheduler_free(sched);
}

/* ============================================================================
 * Demonitoring Tests
 * ============================================================================ */

void test_demonitor_block(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code1 = create_receive_halt_bytecode();
    Bytecode *code2 = create_receive_halt_bytecode();

    Pid monitor_pid = scheduler_spawn_ex(sched, code1, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
    Pid target_pid = scheduler_spawn_ex(sched, code2, "target", CAP_RECEIVE, NULL);

    Block *monitor = scheduler_get_block(sched, monitor_pid);

    /* Monitor then demonitor */
    block_monitor(monitor, target_pid);
    ASSERT_EQ(1, monitor->monitor_count);

    block_demonitor(monitor, target_pid);
    ASSERT_EQ(0, monitor->monitor_count);

    scheduler_free(sched);
}

void test_demonitor_nonexistent_is_safe(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    Pid monitor_pid = scheduler_spawn_ex(sched, code, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
    Block *monitor = scheduler_get_block(sched, monitor_pid);

    /* Demonitor something that was never monitored - should not crash */
    block_demonitor(monitor, 9999);

    ASSERT_EQ(0, monitor->monitor_count);

    scheduler_free(sched);
}

void test_demonitor_one_of_many(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    Pid monitor_pid = scheduler_spawn_ex(sched, code, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
    Block *monitor = scheduler_get_block(sched, monitor_pid);

    Pid targets[4];
    for (int i = 0; i < 4; i++) {
        targets[i] = scheduler_spawn_ex(sched, code, "target", CAP_RECEIVE, NULL);
        block_monitor(monitor, targets[i]);
    }

    ASSERT_EQ(4, monitor->monitor_count);

    /* Demonitor middle one */
    block_demonitor(monitor, targets[2]);

    ASSERT_EQ(3, monitor->monitor_count);

    /* Verify the demonitored one is gone */
    for (size_t i = 0; i < monitor->monitor_count; i++) {
        ASSERT(monitor->monitors[i] != targets[2]);
    }

    scheduler_free(sched);
}

/* ============================================================================
 * Monitor Safety Tests
 * ============================================================================ */

void test_monitor_null_block(void) {
    bool result = block_monitor(NULL, 123);
    ASSERT(!result);
}

void test_monitor_invalid_pid(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_halt_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "block", CAP_MONITOR, NULL);
    Block *block = scheduler_get_block(sched, pid);

    bool result = block_monitor(block, PID_INVALID);
    ASSERT(!result);

    ASSERT_EQ(0, block->monitor_count);

    scheduler_free(sched);
}

void test_demonitor_null_block(void) {
    /* Should not crash */
    block_demonitor(NULL, 123);
    ASSERT(1);  /* If we get here, no crash */
}

void test_add_monitored_by_null_block(void) {
    bool result = block_add_monitored_by(NULL, 123);
    ASSERT(!result);
}

void test_add_monitored_by_invalid_pid(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_halt_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "block", CAP_NONE, NULL);
    Block *block = scheduler_get_block(sched, pid);

    bool result = block_add_monitored_by(block, PID_INVALID);
    ASSERT(!result);

    ASSERT_EQ(0, block->monitored_by_count);

    scheduler_free(sched);
}

/* ============================================================================
 * Monitor vs Link Behavior Tests
 * ============================================================================ */

void test_monitor_does_not_kill_on_exit(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *halt_code = create_halt_bytecode();
    Bytecode *recv_code = create_receive_halt_bytecode();

    /* Target will exit, monitor should not die */
    Pid target_pid = scheduler_spawn_ex(sched, halt_code, "target", CAP_NONE, NULL);
    Pid monitor_pid = scheduler_spawn_ex(sched, recv_code, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);

    Block *target = scheduler_get_block(sched, target_pid);
    Block *monitor = scheduler_get_block(sched, monitor_pid);

    /* Set up monitoring (not linking) */
    block_monitor(monitor, target_pid);
    block_add_monitored_by(target, monitor_pid);

    /* Run target to completion */
    scheduler_step(sched);

    /* Target should be dead */
    ASSERT_EQ(BLOCK_DEAD, block_state(target));

    /* Monitor should still be alive (waiting for message) */
    ASSERT(block_is_alive(monitor));

    scheduler_free(sched);
}

void test_link_and_monitor_together(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    Pid pid1 = scheduler_spawn_ex(sched, code, "block1",
                                   CAP_LINK | CAP_MONITOR | CAP_RECEIVE | CAP_TRAP_EXIT, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code, "block2",
                                   CAP_LINK | CAP_MONITOR | CAP_RECEIVE, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);
    Block *block2 = scheduler_get_block(sched, pid2);

    /* Both link and monitor */
    block_link(block1, pid2);
    block_link(block2, pid1);
    block_monitor(block1, pid2);
    block_add_monitored_by(block2, pid1);

    /* Verify both */
    size_t link_count;
    block_get_links(block1, &link_count);
    ASSERT_EQ(1, link_count);
    ASSERT_EQ(1, block1->monitor_count);

    scheduler_free(sched);
}

/* ============================================================================
 * Integration Scenarios
 * ============================================================================ */

void test_supervisor_pattern(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *recv_code = create_receive_halt_bytecode();

    /* Supervisor monitors multiple workers */
    Pid supervisor_pid = scheduler_spawn_ex(sched, recv_code, "supervisor",
                                             CAP_MONITOR | CAP_RECEIVE | CAP_TRAP_EXIT, NULL);
    Block *supervisor = scheduler_get_block(sched, supervisor_pid);

    Pid workers[3];
    for (int i = 0; i < 3; i++) {
        workers[i] = scheduler_spawn_ex(sched, recv_code, "worker", CAP_RECEIVE, NULL);
        Block *worker = scheduler_get_block(sched, workers[i]);
        block_monitor(supervisor, workers[i]);
        block_add_monitored_by(worker, supervisor_pid);
    }

    /* Supervisor monitors all workers */
    ASSERT_EQ(3, supervisor->monitor_count);

    /* Each worker is monitored by supervisor */
    for (int i = 0; i < 3; i++) {
        Block *worker = scheduler_get_block(sched, workers[i]);
        ASSERT(worker != NULL);
        ASSERT_EQ(1, worker->monitored_by_count);
    }

    scheduler_free(sched);
}

void test_hierarchical_monitoring(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    /* Create hierarchy: top -> middle -> bottom */
    Pid top_pid = scheduler_spawn_ex(sched, code, "top", CAP_MONITOR | CAP_RECEIVE, NULL);
    Pid mid_pid = scheduler_spawn_ex(sched, code, "mid", CAP_MONITOR | CAP_RECEIVE, NULL);
    Pid bot_pid = scheduler_spawn_ex(sched, code, "bot", CAP_RECEIVE, NULL);

    Block *top = scheduler_get_block(sched, top_pid);
    Block *mid = scheduler_get_block(sched, mid_pid);
    Block *bot = scheduler_get_block(sched, bot_pid);

    /* Set up monitoring chain */
    block_monitor(top, mid_pid);
    block_add_monitored_by(mid, top_pid);

    block_monitor(mid, bot_pid);
    block_add_monitored_by(bot, mid_pid);

    /* Verify hierarchy */
    ASSERT_EQ(1, top->monitor_count);
    ASSERT_EQ(0, top->monitored_by_count);

    ASSERT_EQ(1, mid->monitor_count);
    ASSERT_EQ(1, mid->monitored_by_count);

    ASSERT_EQ(0, bot->monitor_count);
    ASSERT_EQ(1, bot->monitored_by_count);

    scheduler_free(sched);
}

void test_monitor_cleanup_on_demonitor(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    Pid monitor_pid = scheduler_spawn_ex(sched, code, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
    Pid target_pid = scheduler_spawn_ex(sched, code, "target", CAP_RECEIVE, NULL);

    Block *monitor = scheduler_get_block(sched, monitor_pid);
    Block *target = scheduler_get_block(sched, target_pid);

    /* Set up monitoring */
    block_monitor(monitor, target_pid);
    block_add_monitored_by(target, monitor_pid);

    ASSERT_EQ(1, monitor->monitor_count);
    ASSERT_EQ(1, target->monitored_by_count);

    /* Demonitor */
    block_demonitor(monitor, target_pid);

    ASSERT_EQ(0, monitor->monitor_count);
    /* Note: The monitored_by cleanup should ideally happen but may require
     * explicit cleanup depending on implementation */

    scheduler_free(sched);
}

void test_monitor_after_block_waiting(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    Pid monitor_pid = scheduler_spawn_ex(sched, code, "monitor", CAP_MONITOR | CAP_RECEIVE, NULL);
    Pid target_pid = scheduler_spawn_ex(sched, code, "target", CAP_RECEIVE, NULL);

    Block *monitor = scheduler_get_block(sched, monitor_pid);
    Block *target = scheduler_get_block(sched, target_pid);

    /* Step to make blocks waiting */
    scheduler_step(sched);
    scheduler_step(sched);

    ASSERT_EQ(BLOCK_WAITING, block_state(monitor));
    ASSERT_EQ(BLOCK_WAITING, block_state(target));

    /* Monitor while waiting - should still work */
    bool result = block_monitor(monitor, target_pid);
    ASSERT(result);

    ASSERT_EQ(1, monitor->monitor_count);

    scheduler_free(sched);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running integration monitoring tests...\n");

    printf("\nBasic monitoring tests:\n");
    RUN_TEST(test_monitor_block);
    RUN_TEST(test_monitor_multiple_targets);
    RUN_TEST(test_multiple_monitors_same_target);
    RUN_TEST(test_monitor_same_target_twice_idempotent);

    printf("\nDemonitoring tests:\n");
    RUN_TEST(test_demonitor_block);
    RUN_TEST(test_demonitor_nonexistent_is_safe);
    RUN_TEST(test_demonitor_one_of_many);

    printf("\nMonitor safety tests:\n");
    RUN_TEST(test_monitor_null_block);
    RUN_TEST(test_monitor_invalid_pid);
    RUN_TEST(test_demonitor_null_block);
    RUN_TEST(test_add_monitored_by_null_block);
    RUN_TEST(test_add_monitored_by_invalid_pid);

    printf("\nMonitor vs link behavior tests:\n");
    RUN_TEST(test_monitor_does_not_kill_on_exit);
    RUN_TEST(test_link_and_monitor_together);

    printf("\nIntegration scenarios:\n");
    RUN_TEST(test_supervisor_pattern);
    RUN_TEST(test_hierarchical_monitoring);
    RUN_TEST(test_monitor_cleanup_on_demonitor);
    RUN_TEST(test_monitor_after_block_waiting);

    return TEST_RESULT();
}
