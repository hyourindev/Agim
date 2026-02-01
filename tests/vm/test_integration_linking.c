/*
 * Agim - Integration Tests for Process Linking
 *
 * P1.2.2: Tests for block linking behavior in a multi-block environment.
 * - Basic linking between two blocks
 * - Bidirectional links
 * - Link propagation on exit
 * - Linked blocks with trap_exit
 * - Unlinking
 * - Multiple linked blocks
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

/* Helper: Create bytecode that crashes */
static Bytecode *create_crash_bytecode(void) {
    Bytecode *code = bytecode_new();
    if (!code) return NULL;
    Chunk *chunk = code->main;
    /* Divide by zero to crash */
    chunk_add_constant(chunk, value_int(1));
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_add_constant(chunk, value_int(0));
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 1, 2);
    chunk_write_opcode(chunk, OP_DIV, 3);
    chunk_write_opcode(chunk, OP_HALT, 4);
    return code;
}

/* ============================================================================
 * Basic Linking Tests
 * ============================================================================ */

void test_link_two_blocks(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code1 = create_receive_halt_bytecode();
    Bytecode *code2 = create_receive_halt_bytecode();

    /* Spawn two blocks with link capability */
    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_LINK | CAP_RECEIVE, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_LINK | CAP_RECEIVE, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);
    Block *block2 = scheduler_get_block(sched, pid2);

    ASSERT(block1 != NULL);
    ASSERT(block2 != NULL);

    /* Link block1 to block2 */
    bool linked = block_link(block1, pid2);
    ASSERT(linked);

    /* Verify link exists in block1 */
    size_t link_count;
    const Pid *links = block_get_links(block1, &link_count);
    ASSERT_EQ(1, link_count);
    ASSERT_EQ(pid2, links[0]);

    scheduler_free(sched);
}

void test_bidirectional_links(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code1 = create_receive_halt_bytecode();
    Bytecode *code2 = create_receive_halt_bytecode();

    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_LINK | CAP_RECEIVE, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_LINK | CAP_RECEIVE, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);
    Block *block2 = scheduler_get_block(sched, pid2);

    /* Link in both directions */
    block_link(block1, pid2);
    block_link(block2, pid1);

    /* Verify links in block1 */
    size_t count1;
    const Pid *links1 = block_get_links(block1, &count1);
    ASSERT_EQ(1, count1);
    ASSERT_EQ(pid2, links1[0]);

    /* Verify links in block2 */
    size_t count2;
    const Pid *links2 = block_get_links(block2, &count2);
    ASSERT_EQ(1, count2);
    ASSERT_EQ(pid1, links2[0]);

    scheduler_free(sched);
}

void test_link_multiple_blocks(void) {
    Scheduler *sched = create_test_scheduler();

    Bytecode *code = create_receive_halt_bytecode();
    Pid pids[5];

    for (int i = 0; i < 5; i++) {
        pids[i] = scheduler_spawn_ex(sched, code, "block", CAP_LINK | CAP_RECEIVE, NULL);
    }

    Block *main_block = scheduler_get_block(sched, pids[0]);

    /* Link main block to all others */
    for (int i = 1; i < 5; i++) {
        block_link(main_block, pids[i]);
    }

    /* Verify links */
    size_t link_count;
    (void)block_get_links(main_block, &link_count);
    ASSERT_EQ(4, link_count);

    scheduler_free(sched);
}

void test_link_same_block_twice_idempotent(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code1 = create_receive_halt_bytecode();
    Bytecode *code2 = create_receive_halt_bytecode();

    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_LINK | CAP_RECEIVE, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_LINK | CAP_RECEIVE, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);

    /* Link to same block multiple times */
    block_link(block1, pid2);
    block_link(block1, pid2);
    block_link(block1, pid2);

    /* Should still have only one link */
    size_t link_count;
    const Pid *links = block_get_links(block1, &link_count);
    ASSERT_EQ(1, link_count);
    ASSERT_EQ(pid2, links[0]);

    scheduler_free(sched);
}

/* ============================================================================
 * Unlinking Tests
 * ============================================================================ */

void test_unlink_blocks(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code1 = create_receive_halt_bytecode();
    Bytecode *code2 = create_receive_halt_bytecode();

    Pid pid1 = scheduler_spawn_ex(sched, code1, "block1", CAP_LINK | CAP_RECEIVE, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code2, "block2", CAP_LINK | CAP_RECEIVE, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);

    /* Link then unlink */
    block_link(block1, pid2);

    size_t count_before;
    block_get_links(block1, &count_before);
    ASSERT_EQ(1, count_before);

    block_unlink(block1, pid2);

    size_t count_after;
    block_get_links(block1, &count_after);
    ASSERT_EQ(0, count_after);

    scheduler_free(sched);
}

void test_unlink_nonexistent_is_safe(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "block", CAP_LINK | CAP_RECEIVE, NULL);
    Block *block = scheduler_get_block(sched, pid);

    /* Unlink something that was never linked - should not crash */
    block_unlink(block, 9999);

    size_t link_count;
    block_get_links(block, &link_count);
    ASSERT_EQ(0, link_count);

    scheduler_free(sched);
}

void test_unlink_one_of_many(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    Pid pids[4];
    for (int i = 0; i < 4; i++) {
        pids[i] = scheduler_spawn_ex(sched, code, "block", CAP_LINK | CAP_RECEIVE, NULL);
    }

    Block *main_block = scheduler_get_block(sched, pids[0]);

    /* Link to all */
    for (int i = 1; i < 4; i++) {
        block_link(main_block, pids[i]);
    }

    size_t count_before;
    block_get_links(main_block, &count_before);
    ASSERT_EQ(3, count_before);

    /* Unlink middle one */
    block_unlink(main_block, pids[2]);

    size_t count_after;
    const Pid *links = block_get_links(main_block, &count_after);
    ASSERT_EQ(2, count_after);

    /* Verify the unlinked one is gone */
    for (size_t i = 0; i < count_after; i++) {
        ASSERT(links[i] != pids[2]);
    }

    scheduler_free(sched);
}

/* ============================================================================
 * Link Exit Propagation Tests
 * ============================================================================ */

void test_linked_block_killed_on_exit(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *crash_code = create_crash_bytecode();  /* Use crash, not normal halt */
    Bytecode *recv_code = create_receive_halt_bytecode();

    /* Spawn both blocks */
    Pid main_pid = scheduler_spawn_ex(sched, crash_code, "main", CAP_LINK, NULL);
    Pid linked_pid = scheduler_spawn_ex(sched, recv_code, "linked", CAP_LINK | CAP_RECEIVE, NULL);

    Block *main_block = scheduler_get_block(sched, main_pid);
    Block *linked_block = scheduler_get_block(sched, linked_pid);

    /* Link main to linked - main's links array contains linked_pid */
    block_link(main_block, linked_pid);

    /* Also add reverse link */
    block_link(linked_block, main_pid);

    /* Verify links are set up */
    size_t main_links_count;
    block_get_links(main_block, &main_links_count);
    ASSERT_EQ(1, main_links_count);

    /* Run scheduler until it completes (main will crash, linked waits) */
    scheduler_run(sched);

    /* At this point, main should be dead from crash */
    ASSERT_EQ(BLOCK_DEAD, block_state(main_block));

    /* Exit propagation should have killed the linked block because:
     * 1. main had linked in its links array
     * 2. linked does not have CAP_TRAP_EXIT
     * 3. main exited abnormally (crashed), which propagates to linked processes
     * Note: Normal exits do NOT propagate - only crashes do (Erlang semantics) */
    ASSERT_EQ(BLOCK_DEAD, block_state(linked_block));

    scheduler_free(sched);
}

void test_linked_block_with_trap_exit_receives_message(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *halt_code = create_halt_bytecode();
    Bytecode *recv_code = create_receive_halt_bytecode();

    /* Spawn main block that will exit */
    Pid main_pid = scheduler_spawn_ex(sched, halt_code, "main", CAP_LINK, NULL);

    /* Spawn linked block with trap_exit - should receive exit message instead of dying */
    Pid linked_pid = scheduler_spawn_ex(sched, recv_code, "linked",
                                         CAP_LINK | CAP_RECEIVE | CAP_TRAP_EXIT, NULL);

    Block *main_block = scheduler_get_block(sched, main_pid);
    Block *linked_block = scheduler_get_block(sched, linked_pid);

    /* Link blocks */
    block_link(main_block, linked_pid);
    block_link(linked_block, main_pid);

    /* Run main block to completion */
    scheduler_step(sched);  /* Execute main block - it halts */
    scheduler_propagate_exit(sched, main_block);

    /* Linked block with trap_exit should have received an exit message */
    ASSERT(block_has_messages(linked_block));

    /* Run linked block to process the message */
    scheduler_step(sched);

    /* Both should eventually be dead */
    ASSERT_EQ(BLOCK_DEAD, block_state(main_block));
    ASSERT_EQ(BLOCK_DEAD, block_state(linked_block));

    scheduler_free(sched);
}

/* ============================================================================
 * Link Safety Tests
 * ============================================================================ */

void test_link_null_block(void) {
    bool result = block_link(NULL, 123);
    ASSERT(!result);
}

void test_link_invalid_pid(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_halt_bytecode();

    Pid pid = scheduler_spawn_ex(sched, code, "block", CAP_LINK, NULL);
    Block *block = scheduler_get_block(sched, pid);

    bool result = block_link(block, PID_INVALID);
    ASSERT(!result);

    size_t link_count;
    block_get_links(block, &link_count);
    ASSERT_EQ(0, link_count);

    scheduler_free(sched);
}

void test_unlink_null_block(void) {
    /* Should not crash */
    block_unlink(NULL, 123);
    ASSERT(1);  /* If we get here, no crash */
}

void test_get_links_null_block(void) {
    size_t count = 999;
    const Pid *links = block_get_links(NULL, &count);
    ASSERT(links == NULL);
    /* count may be unchanged or set to 0 depending on implementation */
}

/* ============================================================================
 * Integration Scenarios
 * ============================================================================ */

void test_chain_of_linked_blocks(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    /* Create a chain: A -> B -> C -> D */
    Pid pids[4];
    for (int i = 0; i < 4; i++) {
        pids[i] = scheduler_spawn_ex(sched, code, "block", CAP_LINK | CAP_RECEIVE, NULL);
    }

    /* Link each to the next */
    for (int i = 0; i < 3; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        block_link(block, pids[i + 1]);
    }

    /* Verify chain */
    for (int i = 0; i < 3; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        size_t count;
        const Pid *links = block_get_links(block, &count);
        ASSERT_EQ(1, count);
        ASSERT_EQ(pids[i + 1], links[0]);
    }

    /* Last block has no outgoing links */
    Block *last = scheduler_get_block(sched, pids[3]);
    size_t last_count;
    block_get_links(last, &last_count);
    ASSERT_EQ(0, last_count);

    scheduler_free(sched);
}

void test_star_topology_links(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    /* Create star: center linked to all satellites */
    Pid center_pid = scheduler_spawn_ex(sched, code, "center", CAP_LINK | CAP_RECEIVE, NULL);
    Block *center = scheduler_get_block(sched, center_pid);

    Pid satellites[5];
    for (int i = 0; i < 5; i++) {
        satellites[i] = scheduler_spawn_ex(sched, code, "satellite", CAP_LINK | CAP_RECEIVE, NULL);
        block_link(center, satellites[i]);
    }

    /* Verify center has all links */
    size_t center_count;
    block_get_links(center, &center_count);
    ASSERT_EQ(5, center_count);

    /* Verify satellites have no outgoing links */
    for (int i = 0; i < 5; i++) {
        Block *sat = scheduler_get_block(sched, satellites[i]);
        size_t sat_count;
        block_get_links(sat, &sat_count);
        ASSERT_EQ(0, sat_count);
    }

    scheduler_free(sched);
}

void test_link_after_block_starts_running(void) {
    Scheduler *sched = create_test_scheduler();
    Bytecode *code = create_receive_halt_bytecode();

    Pid pid1 = scheduler_spawn_ex(sched, code, "block1", CAP_LINK | CAP_RECEIVE, NULL);
    Pid pid2 = scheduler_spawn_ex(sched, code, "block2", CAP_LINK | CAP_RECEIVE, NULL);

    Block *block1 = scheduler_get_block(sched, pid1);
    Block *block2 = scheduler_get_block(sched, pid2);

    /* Step to make blocks waiting */
    scheduler_step(sched);  /* block1 runs, waits for message */
    scheduler_step(sched);  /* block2 runs, waits for message */

    ASSERT_EQ(BLOCK_WAITING, block_state(block1));
    ASSERT_EQ(BLOCK_WAITING, block_state(block2));

    /* Link while waiting - should still work */
    bool linked = block_link(block1, pid2);
    ASSERT(linked);

    size_t count;
    const Pid *links = block_get_links(block1, &count);
    ASSERT_EQ(1, count);
    ASSERT_EQ(pid2, links[0]);

    scheduler_free(sched);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running integration linking tests...\n");

    printf("\nBasic linking tests:\n");
    RUN_TEST(test_link_two_blocks);
    RUN_TEST(test_bidirectional_links);
    RUN_TEST(test_link_multiple_blocks);
    RUN_TEST(test_link_same_block_twice_idempotent);

    printf("\nUnlinking tests:\n");
    RUN_TEST(test_unlink_blocks);
    RUN_TEST(test_unlink_nonexistent_is_safe);
    RUN_TEST(test_unlink_one_of_many);

    printf("\nLink exit propagation tests:\n");
    RUN_TEST(test_linked_block_killed_on_exit);
    RUN_TEST(test_linked_block_with_trap_exit_receives_message);

    printf("\nLink safety tests:\n");
    RUN_TEST(test_link_null_block);
    RUN_TEST(test_link_invalid_pid);
    RUN_TEST(test_unlink_null_block);
    RUN_TEST(test_get_links_null_block);

    printf("\nIntegration scenarios:\n");
    RUN_TEST(test_chain_of_linked_blocks);
    RUN_TEST(test_star_topology_links);
    RUN_TEST(test_link_after_block_starts_running);

    return TEST_RESULT();
}
