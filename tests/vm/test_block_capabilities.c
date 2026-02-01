/*
 * Agim - Block Capabilities Tests
 *
 * P1.1.4.2: Tests for block capability operations.
 * - block_grant adds caps
 * - block_revoke removes caps
 * - block_has_cap checks
 * - block_check_cap crashes on deny
 * - CAP_NONE default
 * - Each capability individually
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/capability.h"

/*
 * Test: Block starts with CAP_NONE
 */
void test_capabilities_default_none(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(CAP_NONE, block->capabilities);

    block_free(block);
}

/*
 * Test: block_grant adds single capability
 */
void test_capabilities_grant_single(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_SPAWN));

    block_grant(block, CAP_SPAWN);

    ASSERT(block_has_cap(block, CAP_SPAWN));

    block_free(block);
}

/*
 * Test: block_grant adds multiple capabilities
 */
void test_capabilities_grant_multiple(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_SPAWN);
    block_grant(block, CAP_SEND);
    block_grant(block, CAP_RECEIVE);

    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));

    block_free(block);
}

/*
 * Test: block_grant with combined capabilities
 */
void test_capabilities_grant_combined(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    CapabilitySet caps = CAP_SPAWN | CAP_SEND | CAP_RECEIVE;
    block_grant(block, caps);

    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));

    block_free(block);
}

/*
 * Test: block_grant is idempotent
 */
void test_capabilities_grant_idempotent(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_SPAWN);
    block_grant(block, CAP_SPAWN);  /* Grant again */
    block_grant(block, CAP_SPAWN);  /* And again */

    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT_EQ(CAP_SPAWN, block->capabilities);

    block_free(block);
}

/*
 * Test: block_grant with NULL is safe
 */
void test_capabilities_grant_null(void) {
    block_grant(NULL, CAP_SPAWN);
    ASSERT(1);  /* Should not crash */
}

/*
 * Test: block_revoke removes single capability
 */
void test_capabilities_revoke_single(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_SPAWN | CAP_SEND);
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));

    block_revoke(block, CAP_SPAWN);

    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));  /* Unchanged */

    block_free(block);
}

/*
 * Test: block_revoke removes multiple capabilities
 */
void test_capabilities_revoke_multiple(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_ALL);
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_FILE_READ));

    block_revoke(block, CAP_SPAWN | CAP_SEND);

    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_FILE_READ));  /* Unchanged */

    block_free(block);
}

/*
 * Test: block_revoke non-existent capability is no-op
 */
void test_capabilities_revoke_nonexistent(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_SEND);
    ASSERT_EQ(CAP_SEND, block->capabilities);

    block_revoke(block, CAP_SPAWN);  /* Not granted */

    ASSERT_EQ(CAP_SEND, block->capabilities);  /* Unchanged */

    block_free(block);
}

/*
 * Test: block_revoke with NULL is safe
 */
void test_capabilities_revoke_null(void) {
    block_revoke(NULL, CAP_SPAWN);
    ASSERT(1);  /* Should not crash */
}

/*
 * Test: block_has_cap returns true for granted cap
 */
void test_capabilities_has_cap_true(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_SPAWN);

    ASSERT(block_has_cap(block, CAP_SPAWN));

    block_free(block);
}

/*
 * Test: block_has_cap returns false for non-granted cap
 */
void test_capabilities_has_cap_false(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_SPAWN);

    ASSERT(!block_has_cap(block, CAP_SEND));
    ASSERT(!block_has_cap(block, CAP_FILE_READ));

    block_free(block);
}

/*
 * Test: block_has_cap with NULL returns false
 */
void test_capabilities_has_cap_null(void) {
    ASSERT(!block_has_cap(NULL, CAP_SPAWN));
}

/*
 * Test: block_has_cap requires all bits set
 */
void test_capabilities_has_cap_requires_all(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_SPAWN);

    /* Checking for combined caps should fail if not all granted */
    ASSERT(!block_has_cap(block, CAP_SPAWN | CAP_SEND));

    block_grant(block, CAP_SEND);
    ASSERT(block_has_cap(block, CAP_SPAWN | CAP_SEND));

    block_free(block);
}

/*
 * Test: block_check_cap returns true if has cap
 */
void test_capabilities_check_cap_success(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_SPAWN);

    bool allowed = block_check_cap(block, CAP_SPAWN);
    ASSERT(allowed);
    ASSERT(block_is_alive(block));  /* Block should still be alive */

    block_free(block);
}

/*
 * Test: block_check_cap crashes block if denied
 */
void test_capabilities_check_cap_denies(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* Don't grant CAP_SPAWN */

    bool allowed = block_check_cap(block, CAP_SPAWN);
    ASSERT(!allowed);
    ASSERT(!block_is_alive(block));  /* Block should be crashed */
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    block_free(block);
}

/*
 * Test: CAP_SPAWN capability
 */
void test_capability_spawn(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_SPAWN));
    block_grant(block, CAP_SPAWN);
    ASSERT(block_has_cap(block, CAP_SPAWN));
    block_revoke(block, CAP_SPAWN);
    ASSERT(!block_has_cap(block, CAP_SPAWN));

    block_free(block);
}

/*
 * Test: CAP_SEND capability
 */
void test_capability_send(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_SEND));
    block_grant(block, CAP_SEND);
    ASSERT(block_has_cap(block, CAP_SEND));
    block_revoke(block, CAP_SEND);
    ASSERT(!block_has_cap(block, CAP_SEND));

    block_free(block);
}

/*
 * Test: CAP_RECEIVE capability
 */
void test_capability_receive(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_RECEIVE));
    block_grant(block, CAP_RECEIVE);
    ASSERT(block_has_cap(block, CAP_RECEIVE));
    block_revoke(block, CAP_RECEIVE);
    ASSERT(!block_has_cap(block, CAP_RECEIVE));

    block_free(block);
}

/*
 * Test: CAP_INFER capability
 */
void test_capability_infer(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_INFER));
    block_grant(block, CAP_INFER);
    ASSERT(block_has_cap(block, CAP_INFER));
    block_revoke(block, CAP_INFER);
    ASSERT(!block_has_cap(block, CAP_INFER));

    block_free(block);
}

/*
 * Test: CAP_HTTP capability
 */
void test_capability_http(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_HTTP));
    block_grant(block, CAP_HTTP);
    ASSERT(block_has_cap(block, CAP_HTTP));
    block_revoke(block, CAP_HTTP);
    ASSERT(!block_has_cap(block, CAP_HTTP));

    block_free(block);
}

/*
 * Test: CAP_FILE_READ capability
 */
void test_capability_file_read(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_FILE_READ));
    block_grant(block, CAP_FILE_READ);
    ASSERT(block_has_cap(block, CAP_FILE_READ));
    block_revoke(block, CAP_FILE_READ);
    ASSERT(!block_has_cap(block, CAP_FILE_READ));

    block_free(block);
}

/*
 * Test: CAP_FILE_WRITE capability
 */
void test_capability_file_write(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_FILE_WRITE));
    block_grant(block, CAP_FILE_WRITE);
    ASSERT(block_has_cap(block, CAP_FILE_WRITE));
    block_revoke(block, CAP_FILE_WRITE);
    ASSERT(!block_has_cap(block, CAP_FILE_WRITE));

    block_free(block);
}

/*
 * Test: CAP_DB capability
 */
void test_capability_db(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_DB));
    block_grant(block, CAP_DB);
    ASSERT(block_has_cap(block, CAP_DB));
    block_revoke(block, CAP_DB);
    ASSERT(!block_has_cap(block, CAP_DB));

    block_free(block);
}

/*
 * Test: CAP_MEMORY capability
 */
void test_capability_memory(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_MEMORY));
    block_grant(block, CAP_MEMORY);
    ASSERT(block_has_cap(block, CAP_MEMORY));
    block_revoke(block, CAP_MEMORY);
    ASSERT(!block_has_cap(block, CAP_MEMORY));

    block_free(block);
}

/*
 * Test: CAP_LINK capability
 */
void test_capability_link(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_LINK));
    block_grant(block, CAP_LINK);
    ASSERT(block_has_cap(block, CAP_LINK));
    block_revoke(block, CAP_LINK);
    ASSERT(!block_has_cap(block, CAP_LINK));

    block_free(block);
}

/*
 * Test: CAP_SHELL capability
 */
void test_capability_shell(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_SHELL));
    block_grant(block, CAP_SHELL);
    ASSERT(block_has_cap(block, CAP_SHELL));
    block_revoke(block, CAP_SHELL);
    ASSERT(!block_has_cap(block, CAP_SHELL));

    block_free(block);
}

/*
 * Test: CAP_EXEC capability
 */
void test_capability_exec(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_EXEC));
    block_grant(block, CAP_EXEC);
    ASSERT(block_has_cap(block, CAP_EXEC));
    block_revoke(block, CAP_EXEC);
    ASSERT(!block_has_cap(block, CAP_EXEC));

    block_free(block);
}

/*
 * Test: CAP_TRAP_EXIT capability
 */
void test_capability_trap_exit(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_TRAP_EXIT));
    block_grant(block, CAP_TRAP_EXIT);
    ASSERT(block_has_cap(block, CAP_TRAP_EXIT));
    block_revoke(block, CAP_TRAP_EXIT);
    ASSERT(!block_has_cap(block, CAP_TRAP_EXIT));

    block_free(block);
}

/*
 * Test: CAP_MONITOR capability
 */
void test_capability_monitor(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_MONITOR));
    block_grant(block, CAP_MONITOR);
    ASSERT(block_has_cap(block, CAP_MONITOR));
    block_revoke(block, CAP_MONITOR);
    ASSERT(!block_has_cap(block, CAP_MONITOR));

    block_free(block);
}

/*
 * Test: CAP_SUPERVISE capability
 */
void test_capability_supervise(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_SUPERVISE));
    block_grant(block, CAP_SUPERVISE);
    ASSERT(block_has_cap(block, CAP_SUPERVISE));
    block_revoke(block, CAP_SUPERVISE);
    ASSERT(!block_has_cap(block, CAP_SUPERVISE));

    block_free(block);
}

/*
 * Test: CAP_ENV capability
 */
void test_capability_env(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_ENV));
    block_grant(block, CAP_ENV);
    ASSERT(block_has_cap(block, CAP_ENV));
    block_revoke(block, CAP_ENV);
    ASSERT(!block_has_cap(block, CAP_ENV));

    block_free(block);
}

/*
 * Test: CAP_WEBSOCKET capability
 */
void test_capability_websocket(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_cap(block, CAP_WEBSOCKET));
    block_grant(block, CAP_WEBSOCKET);
    ASSERT(block_has_cap(block, CAP_WEBSOCKET));
    block_revoke(block, CAP_WEBSOCKET);
    ASSERT(!block_has_cap(block, CAP_WEBSOCKET));

    block_free(block);
}

/*
 * Test: CAP_ALL includes all capabilities
 */
void test_capability_all(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_ALL);

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

    block_free(block);
}

/*
 * Test: Revoking all capabilities
 */
void test_capability_revoke_all(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_grant(block, CAP_ALL);
    block_revoke(block, CAP_ALL);

    ASSERT_EQ(CAP_NONE, block->capabilities);
    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_SEND));

    block_free(block);
}

/*
 * Test: capability_name returns string
 */
void test_capability_name(void) {
    const char *name;

    name = capability_name(CAP_SPAWN);
    ASSERT(name != NULL);

    name = capability_name(CAP_SEND);
    ASSERT(name != NULL);

    name = capability_name(CAP_FILE_READ);
    ASSERT(name != NULL);
}

int main(void) {
    printf("Running block capabilities tests...\n");

    printf("\nDefault capabilities tests:\n");
    RUN_TEST(test_capabilities_default_none);

    printf("\nGrant tests:\n");
    RUN_TEST(test_capabilities_grant_single);
    RUN_TEST(test_capabilities_grant_multiple);
    RUN_TEST(test_capabilities_grant_combined);
    RUN_TEST(test_capabilities_grant_idempotent);
    RUN_TEST(test_capabilities_grant_null);

    printf("\nRevoke tests:\n");
    RUN_TEST(test_capabilities_revoke_single);
    RUN_TEST(test_capabilities_revoke_multiple);
    RUN_TEST(test_capabilities_revoke_nonexistent);
    RUN_TEST(test_capabilities_revoke_null);

    printf("\nHas cap tests:\n");
    RUN_TEST(test_capabilities_has_cap_true);
    RUN_TEST(test_capabilities_has_cap_false);
    RUN_TEST(test_capabilities_has_cap_null);
    RUN_TEST(test_capabilities_has_cap_requires_all);

    printf("\nCheck cap tests:\n");
    RUN_TEST(test_capabilities_check_cap_success);
    RUN_TEST(test_capabilities_check_cap_denies);

    printf("\nIndividual capability tests:\n");
    RUN_TEST(test_capability_spawn);
    RUN_TEST(test_capability_send);
    RUN_TEST(test_capability_receive);
    RUN_TEST(test_capability_infer);
    RUN_TEST(test_capability_http);
    RUN_TEST(test_capability_file_read);
    RUN_TEST(test_capability_file_write);
    RUN_TEST(test_capability_db);
    RUN_TEST(test_capability_memory);
    RUN_TEST(test_capability_link);
    RUN_TEST(test_capability_shell);
    RUN_TEST(test_capability_exec);
    RUN_TEST(test_capability_trap_exit);
    RUN_TEST(test_capability_monitor);
    RUN_TEST(test_capability_supervise);
    RUN_TEST(test_capability_env);
    RUN_TEST(test_capability_websocket);

    printf("\nCAP_ALL tests:\n");
    RUN_TEST(test_capability_all);
    RUN_TEST(test_capability_revoke_all);

    printf("\nCapability name tests:\n");
    RUN_TEST(test_capability_name);

    return TEST_RESULT();
}
