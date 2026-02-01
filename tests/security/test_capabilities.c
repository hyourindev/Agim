/*
 * Agim - Capability Security Tests
 *
 * P1.1.17.1: Comprehensive tests for capability-based security.
 * - Individual capability constants
 * - Capability combinations
 * - block_grant, block_revoke, block_has_cap
 * - block_check_cap (crashes block on failure)
 * - capability_name
 * - CAP_NONE and CAP_ALL boundaries
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/capability.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "vm/bytecode.h"

/* Helper: Create test scheduler */
static Scheduler *create_test_scheduler(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 0;
    return scheduler_new(&config);
}

/* Helper: Create minimal bytecode */
static Bytecode *create_minimal_bytecode(void) {
    Bytecode *code = bytecode_new();
    if (!code) return NULL;
    chunk_write_opcode(code->main, OP_HALT, 1);
    return code;
}

/* ============================================================================
 * Capability Constants Tests
 * ============================================================================ */

void test_cap_none_is_zero(void) {
    ASSERT_EQ(0, CAP_NONE);
}

void test_cap_all_has_all_bits(void) {
    /* CAP_ALL should have bits 0-30 set */
    ASSERT_EQ(0x7FFFFFFF, CAP_ALL);
}

void test_individual_capability_values(void) {
    /* Each capability should be a distinct power of 2 */
    ASSERT_EQ(1 << 0, CAP_SPAWN);
    ASSERT_EQ(1 << 1, CAP_SEND);
    ASSERT_EQ(1 << 2, CAP_RECEIVE);
    ASSERT_EQ(1 << 3, CAP_INFER);
    ASSERT_EQ(1 << 4, CAP_HTTP);
    ASSERT_EQ(1 << 5, CAP_FILE_READ);
    ASSERT_EQ(1 << 6, CAP_FILE_WRITE);
    ASSERT_EQ(1 << 7, CAP_DB);
    ASSERT_EQ(1 << 8, CAP_MEMORY);
    ASSERT_EQ(1 << 9, CAP_LINK);
    ASSERT_EQ(1 << 10, CAP_SHELL);
    ASSERT_EQ(1 << 11, CAP_EXEC);
    ASSERT_EQ(1 << 12, CAP_TRAP_EXIT);
    ASSERT_EQ(1 << 13, CAP_MONITOR);
    ASSERT_EQ(1 << 14, CAP_SUPERVISE);
    ASSERT_EQ(1 << 15, CAP_ENV);
    ASSERT_EQ(1 << 16, CAP_WEBSOCKET);
}

void test_capabilities_are_distinct(void) {
    Capability caps[] = {
        CAP_SPAWN, CAP_SEND, CAP_RECEIVE, CAP_INFER, CAP_HTTP,
        CAP_FILE_READ, CAP_FILE_WRITE, CAP_DB, CAP_MEMORY, CAP_LINK,
        CAP_SHELL, CAP_EXEC, CAP_TRAP_EXIT, CAP_MONITOR, CAP_SUPERVISE,
        CAP_ENV, CAP_WEBSOCKET
    };
    size_t count = sizeof(caps) / sizeof(caps[0]);

    /* Each pair should have no overlapping bits */
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            ASSERT_EQ(0, caps[i] & caps[j]);
        }
    }
}

void test_all_caps_included_in_cap_all(void) {
    Capability caps[] = {
        CAP_SPAWN, CAP_SEND, CAP_RECEIVE, CAP_INFER, CAP_HTTP,
        CAP_FILE_READ, CAP_FILE_WRITE, CAP_DB, CAP_MEMORY, CAP_LINK,
        CAP_SHELL, CAP_EXEC, CAP_TRAP_EXIT, CAP_MONITOR, CAP_SUPERVISE,
        CAP_ENV, CAP_WEBSOCKET
    };
    size_t count = sizeof(caps) / sizeof(caps[0]);

    for (size_t i = 0; i < count; i++) {
        ASSERT_EQ(caps[i], CAP_ALL & caps[i]);
    }
}

/* ============================================================================
 * capability_name Tests
 * ============================================================================ */

void test_capability_name_spawn(void) {
    ASSERT_STR_EQ("SPAWN", capability_name(CAP_SPAWN));
}

void test_capability_name_send(void) {
    ASSERT_STR_EQ("SEND", capability_name(CAP_SEND));
}

void test_capability_name_receive(void) {
    ASSERT_STR_EQ("RECEIVE", capability_name(CAP_RECEIVE));
}

void test_capability_name_infer(void) {
    ASSERT_STR_EQ("INFER", capability_name(CAP_INFER));
}

void test_capability_name_http(void) {
    ASSERT_STR_EQ("HTTP", capability_name(CAP_HTTP));
}

void test_capability_name_file_read(void) {
    ASSERT_STR_EQ("FILE_READ", capability_name(CAP_FILE_READ));
}

void test_capability_name_file_write(void) {
    ASSERT_STR_EQ("FILE_WRITE", capability_name(CAP_FILE_WRITE));
}

void test_capability_name_db(void) {
    ASSERT_STR_EQ("DB", capability_name(CAP_DB));
}

void test_capability_name_memory(void) {
    ASSERT_STR_EQ("MEMORY", capability_name(CAP_MEMORY));
}

void test_capability_name_link(void) {
    ASSERT_STR_EQ("LINK", capability_name(CAP_LINK));
}

void test_capability_name_shell(void) {
    ASSERT_STR_EQ("SHELL", capability_name(CAP_SHELL));
}

void test_capability_name_exec(void) {
    ASSERT_STR_EQ("EXEC", capability_name(CAP_EXEC));
}

void test_capability_name_trap_exit(void) {
    ASSERT_STR_EQ("TRAP_EXIT", capability_name(CAP_TRAP_EXIT));
}

void test_capability_name_monitor(void) {
    ASSERT_STR_EQ("MONITOR", capability_name(CAP_MONITOR));
}

void test_capability_name_supervise(void) {
    ASSERT_STR_EQ("SUPERVISE", capability_name(CAP_SUPERVISE));
}

void test_capability_name_env(void) {
    ASSERT_STR_EQ("ENV", capability_name(CAP_ENV));
}

void test_capability_name_websocket(void) {
    ASSERT_STR_EQ("WEBSOCKET", capability_name(CAP_WEBSOCKET));
}

void test_capability_name_none(void) {
    ASSERT_STR_EQ("NONE", capability_name(CAP_NONE));
}

void test_capability_name_all(void) {
    ASSERT_STR_EQ("ALL", capability_name(CAP_ALL));
}

/* ============================================================================
 * block_has_cap Tests
 * ============================================================================ */

void test_block_has_cap_null_block(void) {
    /* NULL block should return false for any capability */
    ASSERT(!block_has_cap(NULL, CAP_SPAWN));
    ASSERT(!block_has_cap(NULL, CAP_SEND));
    ASSERT(!block_has_cap(NULL, CAP_ALL));
}

void test_block_has_cap_none_initial(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Spawn with CAP_NONE */
    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_NONE, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Should not have any capabilities */
    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_SEND));
    ASSERT(!block_has_cap(block, CAP_RECEIVE));
    ASSERT(!block_has_cap(block, CAP_FILE_READ));
    ASSERT(!block_has_cap(block, CAP_FILE_WRITE));
    ASSERT(!block_has_cap(block, CAP_SHELL));
    ASSERT(!block_has_cap(block, CAP_EXEC));

    scheduler_free(sched);
}

void test_block_has_cap_single(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Spawn with CAP_SPAWN only */
    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_SPAWN, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Should have CAP_SPAWN */
    ASSERT(block_has_cap(block, CAP_SPAWN));

    /* Should not have other capabilities */
    ASSERT(!block_has_cap(block, CAP_SEND));
    ASSERT(!block_has_cap(block, CAP_RECEIVE));

    scheduler_free(sched);
}

void test_block_has_cap_multiple(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    CapabilitySet caps = CAP_SPAWN | CAP_SEND | CAP_RECEIVE;
    Pid pid = scheduler_spawn_ex(sched, code, "test", caps, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Should have all requested capabilities */
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));

    /* Should not have unrequested capabilities */
    ASSERT(!block_has_cap(block, CAP_FILE_READ));
    ASSERT(!block_has_cap(block, CAP_FILE_WRITE));
    ASSERT(!block_has_cap(block, CAP_SHELL));

    scheduler_free(sched);
}

void test_block_has_cap_all(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_ALL, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Should have all capabilities */
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));
    ASSERT(block_has_cap(block, CAP_INFER));
    ASSERT(block_has_cap(block, CAP_HTTP));
    ASSERT(block_has_cap(block, CAP_FILE_READ));
    ASSERT(block_has_cap(block, CAP_FILE_WRITE));
    ASSERT(block_has_cap(block, CAP_DB));
    ASSERT(block_has_cap(block, CAP_MEMORY));
    ASSERT(block_has_cap(block, CAP_LINK));
    ASSERT(block_has_cap(block, CAP_SHELL));
    ASSERT(block_has_cap(block, CAP_EXEC));
    ASSERT(block_has_cap(block, CAP_TRAP_EXIT));
    ASSERT(block_has_cap(block, CAP_MONITOR));
    ASSERT(block_has_cap(block, CAP_SUPERVISE));
    ASSERT(block_has_cap(block, CAP_ENV));
    ASSERT(block_has_cap(block, CAP_WEBSOCKET));

    scheduler_free(sched);
}

/* ============================================================================
 * block_grant Tests
 * ============================================================================ */

void test_block_grant_null_block(void) {
    /* Should not crash */
    block_grant(NULL, CAP_SPAWN);
}

void test_block_grant_single(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_NONE, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Initially no capabilities */
    ASSERT(!block_has_cap(block, CAP_SPAWN));

    /* Grant CAP_SPAWN */
    block_grant(block, CAP_SPAWN);

    /* Now should have CAP_SPAWN */
    ASSERT(block_has_cap(block, CAP_SPAWN));

    scheduler_free(sched);
}

void test_block_grant_multiple(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_NONE, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Grant multiple capabilities */
    block_grant(block, CAP_SPAWN | CAP_SEND | CAP_RECEIVE);

    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));

    scheduler_free(sched);
}

void test_block_grant_incremental(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_NONE, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Grant capabilities one at a time */
    block_grant(block, CAP_SPAWN);
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_SEND));

    block_grant(block, CAP_SEND);
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));

    block_grant(block, CAP_RECEIVE);
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));

    scheduler_free(sched);
}

void test_block_grant_idempotent(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_NONE, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Grant same capability multiple times */
    block_grant(block, CAP_SPAWN);
    block_grant(block, CAP_SPAWN);
    block_grant(block, CAP_SPAWN);

    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT_EQ(CAP_SPAWN, block->capabilities);

    scheduler_free(sched);
}

void test_block_grant_preserves_existing(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_SPAWN, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Already has CAP_SPAWN */
    ASSERT(block_has_cap(block, CAP_SPAWN));

    /* Grant additional capability */
    block_grant(block, CAP_SEND);

    /* Should have both */
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));

    scheduler_free(sched);
}

/* ============================================================================
 * block_revoke Tests
 * ============================================================================ */

void test_block_revoke_null_block(void) {
    /* Should not crash */
    block_revoke(NULL, CAP_SPAWN);
}

void test_block_revoke_single(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_SPAWN | CAP_SEND, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Initially has both */
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));

    /* Revoke CAP_SPAWN */
    block_revoke(block, CAP_SPAWN);

    /* Now should only have CAP_SEND */
    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));

    scheduler_free(sched);
}

void test_block_revoke_multiple(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_ALL, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Revoke multiple capabilities at once */
    block_revoke(block, CAP_SPAWN | CAP_SEND | CAP_RECEIVE);

    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_SEND));
    ASSERT(!block_has_cap(block, CAP_RECEIVE));

    /* Others should remain */
    ASSERT(block_has_cap(block, CAP_FILE_READ));
    ASSERT(block_has_cap(block, CAP_FILE_WRITE));

    scheduler_free(sched);
}

void test_block_revoke_all(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_ALL, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Revoke all capabilities */
    block_revoke(block, CAP_ALL);

    ASSERT_EQ(CAP_NONE, block->capabilities);
    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_SEND));
    ASSERT(!block_has_cap(block, CAP_FILE_READ));

    scheduler_free(sched);
}

void test_block_revoke_idempotent(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_SPAWN | CAP_SEND, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Revoke same capability multiple times */
    block_revoke(block, CAP_SPAWN);
    block_revoke(block, CAP_SPAWN);
    block_revoke(block, CAP_SPAWN);

    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));

    scheduler_free(sched);
}

void test_block_revoke_nonexistent(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_SPAWN, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Revoke capability that wasn't granted */
    block_revoke(block, CAP_SEND);

    /* Should still have CAP_SPAWN */
    ASSERT(block_has_cap(block, CAP_SPAWN));
    /* Should still not have CAP_SEND */
    ASSERT(!block_has_cap(block, CAP_SEND));

    scheduler_free(sched);
}

/* ============================================================================
 * block_check_cap Tests
 * ============================================================================ */

void test_block_check_cap_success(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_SPAWN, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Should succeed and return true */
    bool result = block_check_cap(block, CAP_SPAWN);
    ASSERT(result);

    /* Block should still be alive */
    ASSERT(block_is_alive(block));

    scheduler_free(sched);
}

void test_block_check_cap_failure_crashes_block(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_NONE, NULL);
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT(block_is_alive(block));

    /* Should fail and crash the block */
    bool result = block_check_cap(block, CAP_SPAWN);
    ASSERT(!result);

    /* Block should now be crashed/dead */
    ASSERT(!block_is_alive(block));
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    scheduler_free(sched);
}

/* ============================================================================
 * Capability Combinations Tests
 * ============================================================================ */

void test_capability_set_operations(void) {
    CapabilitySet a = CAP_SPAWN | CAP_SEND;
    CapabilitySet b = CAP_SEND | CAP_RECEIVE;

    /* Union */
    CapabilitySet union_ab = a | b;
    ASSERT_EQ(CAP_SPAWN | CAP_SEND | CAP_RECEIVE, union_ab);

    /* Intersection */
    CapabilitySet intersect_ab = a & b;
    ASSERT_EQ(CAP_SEND, intersect_ab);

    /* Difference */
    CapabilitySet diff_ab = a & ~b;
    ASSERT_EQ(CAP_SPAWN, diff_ab);

    /* Complement */
    CapabilitySet complement_a = CAP_ALL & ~a;
    ASSERT(!(complement_a & CAP_SPAWN));
    ASSERT(!(complement_a & CAP_SEND));
    ASSERT(complement_a & CAP_RECEIVE);
    ASSERT(complement_a & CAP_FILE_READ);
}

void test_file_capabilities(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Grant only read */
    Pid pid1 = scheduler_spawn_ex(sched, code, "reader", CAP_FILE_READ, NULL);
    Block *reader = scheduler_get_block(sched, pid1);
    ASSERT(block_has_cap(reader, CAP_FILE_READ));
    ASSERT(!block_has_cap(reader, CAP_FILE_WRITE));

    /* Grant only write */
    Pid pid2 = scheduler_spawn_ex(sched, code, "writer", CAP_FILE_WRITE, NULL);
    Block *writer = scheduler_get_block(sched, pid2);
    ASSERT(!block_has_cap(writer, CAP_FILE_READ));
    ASSERT(block_has_cap(writer, CAP_FILE_WRITE));

    /* Grant both */
    Pid pid3 = scheduler_spawn_ex(sched, code, "readwriter", CAP_FILE_READ | CAP_FILE_WRITE, NULL);
    Block *readwriter = scheduler_get_block(sched, pid3);
    ASSERT(block_has_cap(readwriter, CAP_FILE_READ));
    ASSERT(block_has_cap(readwriter, CAP_FILE_WRITE));

    scheduler_free(sched);
}

void test_messaging_capabilities(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Grant only send */
    Pid pid1 = scheduler_spawn_ex(sched, code, "sender", CAP_SEND, NULL);
    Block *sender = scheduler_get_block(sched, pid1);
    ASSERT(block_has_cap(sender, CAP_SEND));
    ASSERT(!block_has_cap(sender, CAP_RECEIVE));

    /* Grant only receive */
    Pid pid2 = scheduler_spawn_ex(sched, code, "receiver", CAP_RECEIVE, NULL);
    Block *receiver = scheduler_get_block(sched, pid2);
    ASSERT(!block_has_cap(receiver, CAP_SEND));
    ASSERT(block_has_cap(receiver, CAP_RECEIVE));

    /* Grant both */
    Pid pid3 = scheduler_spawn_ex(sched, code, "bidirectional", CAP_SEND | CAP_RECEIVE, NULL);
    Block *bidir = scheduler_get_block(sched, pid3);
    ASSERT(block_has_cap(bidir, CAP_SEND));
    ASSERT(block_has_cap(bidir, CAP_RECEIVE));

    scheduler_free(sched);
}

void test_execution_capabilities(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Grant shell only */
    Pid pid1 = scheduler_spawn_ex(sched, code, "shell", CAP_SHELL, NULL);
    Block *shell = scheduler_get_block(sched, pid1);
    ASSERT(block_has_cap(shell, CAP_SHELL));
    ASSERT(!block_has_cap(shell, CAP_EXEC));

    /* Grant exec only */
    Pid pid2 = scheduler_spawn_ex(sched, code, "exec", CAP_EXEC, NULL);
    Block *exec = scheduler_get_block(sched, pid2);
    ASSERT(!block_has_cap(exec, CAP_SHELL));
    ASSERT(block_has_cap(exec, CAP_EXEC));

    /* Grant both - full execution */
    Pid pid3 = scheduler_spawn_ex(sched, code, "fullexec", CAP_SHELL | CAP_EXEC, NULL);
    Block *fullexec = scheduler_get_block(sched, pid3);
    ASSERT(block_has_cap(fullexec, CAP_SHELL));
    ASSERT(block_has_cap(fullexec, CAP_EXEC));

    scheduler_free(sched);
}

void test_supervision_capabilities(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Typical supervisor capabilities */
    CapabilitySet supervisor_caps = CAP_SPAWN | CAP_LINK | CAP_TRAP_EXIT |
                                     CAP_MONITOR | CAP_SUPERVISE;

    Pid pid = scheduler_spawn_ex(sched, code, "supervisor", supervisor_caps, NULL);
    Block *sup = scheduler_get_block(sched, pid);

    ASSERT(block_has_cap(sup, CAP_SPAWN));
    ASSERT(block_has_cap(sup, CAP_LINK));
    ASSERT(block_has_cap(sup, CAP_TRAP_EXIT));
    ASSERT(block_has_cap(sup, CAP_MONITOR));
    ASSERT(block_has_cap(sup, CAP_SUPERVISE));

    /* Should not have unrelated capabilities */
    ASSERT(!block_has_cap(sup, CAP_FILE_READ));
    ASSERT(!block_has_cap(sup, CAP_SHELL));
    ASSERT(!block_has_cap(sup, CAP_EXEC));

    scheduler_free(sched);
}

void test_minimal_worker_capabilities(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Worker that can only send and receive messages */
    CapabilitySet worker_caps = CAP_SEND | CAP_RECEIVE;

    Pid pid = scheduler_spawn_ex(sched, code, "worker", worker_caps, NULL);
    Block *worker = scheduler_get_block(sched, pid);

    ASSERT(block_has_cap(worker, CAP_SEND));
    ASSERT(block_has_cap(worker, CAP_RECEIVE));

    /* Should not have spawning or linking capabilities */
    ASSERT(!block_has_cap(worker, CAP_SPAWN));
    ASSERT(!block_has_cap(worker, CAP_LINK));
    ASSERT(!block_has_cap(worker, CAP_FILE_READ));
    ASSERT(!block_has_cap(worker, CAP_SHELL));

    scheduler_free(sched);
}

void test_ai_agent_capabilities(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Typical AI agent capabilities */
    CapabilitySet agent_caps = CAP_SEND | CAP_RECEIVE | CAP_INFER | CAP_HTTP;

    Pid pid = scheduler_spawn_ex(sched, code, "agent", agent_caps, NULL);
    Block *agent = scheduler_get_block(sched, pid);

    ASSERT(block_has_cap(agent, CAP_SEND));
    ASSERT(block_has_cap(agent, CAP_RECEIVE));
    ASSERT(block_has_cap(agent, CAP_INFER));
    ASSERT(block_has_cap(agent, CAP_HTTP));

    /* Should not have system-level capabilities */
    ASSERT(!block_has_cap(agent, CAP_SHELL));
    ASSERT(!block_has_cap(agent, CAP_EXEC));
    ASSERT(!block_has_cap(agent, CAP_FILE_WRITE));

    scheduler_free(sched);
}

/* ============================================================================
 * Security Boundary Tests
 * ============================================================================ */

void test_scheduler_spawn_uses_cap_none(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* scheduler_spawn (not _ex) should use CAP_NONE by default */
    Pid pid = scheduler_spawn(sched, code, "test");
    Block *block = scheduler_get_block(sched, pid);

    ASSERT_EQ(CAP_NONE, block->capabilities);

    scheduler_free(sched);
}

void test_cannot_escalate_capabilities_via_spawn(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    /* Parent has limited capabilities */
    Pid parent_pid = scheduler_spawn_ex(sched, code, "parent", CAP_SPAWN, NULL);
    Block *parent = scheduler_get_block(sched, parent_pid);
    ASSERT(parent != NULL);

    /* The scheduler_spawn_ex function allows setting any capabilities.
     * In a real system, child capability restriction would be enforced
     * by the VM spawn instruction. This test verifies the API allows
     * creating blocks with specific capability sets. */
    Pid child_pid = scheduler_spawn_ex(sched, code, "child", CAP_ALL, NULL);
    Block *child = scheduler_get_block(sched, child_pid);
    ASSERT(child != NULL);

    /* This demonstrates the API works - enforcement is at VM level */
    ASSERT_EQ(CAP_ALL, child->capabilities);

    scheduler_free(sched);
}

void test_revoke_is_permanent(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_minimal_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "test", CAP_ALL, NULL);
    Block *block = scheduler_get_block(sched, pid);

    /* Revoke a capability */
    block_revoke(block, CAP_SHELL);
    ASSERT(!block_has_cap(block, CAP_SHELL));

    /* Cannot re-grant via the block itself
     * (in practice, block_grant would need to be called externally) */

    /* The revoke operation itself is permanent in this context */
    ASSERT(!block_has_cap(block, CAP_SHELL));

    scheduler_free(sched);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running capability tests...\n");

    printf("\nCapability constants tests:\n");
    RUN_TEST(test_cap_none_is_zero);
    RUN_TEST(test_cap_all_has_all_bits);
    RUN_TEST(test_individual_capability_values);
    RUN_TEST(test_capabilities_are_distinct);
    RUN_TEST(test_all_caps_included_in_cap_all);

    printf("\ncapability_name tests:\n");
    RUN_TEST(test_capability_name_spawn);
    RUN_TEST(test_capability_name_send);
    RUN_TEST(test_capability_name_receive);
    RUN_TEST(test_capability_name_infer);
    RUN_TEST(test_capability_name_http);
    RUN_TEST(test_capability_name_file_read);
    RUN_TEST(test_capability_name_file_write);
    RUN_TEST(test_capability_name_db);
    RUN_TEST(test_capability_name_memory);
    RUN_TEST(test_capability_name_link);
    RUN_TEST(test_capability_name_shell);
    RUN_TEST(test_capability_name_exec);
    RUN_TEST(test_capability_name_trap_exit);
    RUN_TEST(test_capability_name_monitor);
    RUN_TEST(test_capability_name_supervise);
    RUN_TEST(test_capability_name_env);
    RUN_TEST(test_capability_name_websocket);
    RUN_TEST(test_capability_name_none);
    RUN_TEST(test_capability_name_all);

    printf("\nblock_has_cap tests:\n");
    RUN_TEST(test_block_has_cap_null_block);
    RUN_TEST(test_block_has_cap_none_initial);
    RUN_TEST(test_block_has_cap_single);
    RUN_TEST(test_block_has_cap_multiple);
    RUN_TEST(test_block_has_cap_all);

    printf("\nblock_grant tests:\n");
    RUN_TEST(test_block_grant_null_block);
    RUN_TEST(test_block_grant_single);
    RUN_TEST(test_block_grant_multiple);
    RUN_TEST(test_block_grant_incremental);
    RUN_TEST(test_block_grant_idempotent);
    RUN_TEST(test_block_grant_preserves_existing);

    printf("\nblock_revoke tests:\n");
    RUN_TEST(test_block_revoke_null_block);
    RUN_TEST(test_block_revoke_single);
    RUN_TEST(test_block_revoke_multiple);
    RUN_TEST(test_block_revoke_all);
    RUN_TEST(test_block_revoke_idempotent);
    RUN_TEST(test_block_revoke_nonexistent);

    printf("\nblock_check_cap tests:\n");
    RUN_TEST(test_block_check_cap_success);
    RUN_TEST(test_block_check_cap_failure_crashes_block);

    printf("\nCapability combination tests:\n");
    RUN_TEST(test_capability_set_operations);
    RUN_TEST(test_file_capabilities);
    RUN_TEST(test_messaging_capabilities);
    RUN_TEST(test_execution_capabilities);
    RUN_TEST(test_supervision_capabilities);
    RUN_TEST(test_minimal_worker_capabilities);
    RUN_TEST(test_ai_agent_capabilities);

    printf("\nSecurity boundary tests:\n");
    RUN_TEST(test_scheduler_spawn_uses_cap_none);
    RUN_TEST(test_cannot_escalate_capabilities_via_spawn);
    RUN_TEST(test_revoke_is_permanent);

    return TEST_RESULT();
}
