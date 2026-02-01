/*
 * Agim - Block Lifecycle Tests
 *
 * P1.1.4.1: Tests for block lifecycle operations.
 * - block_new allocation
 * - block_new with limits
 * - block_free cleanup
 * - block_load bytecode
 * - block state transitions
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "vm/bytecode.h"

/* Helper: Create minimal bytecode */
static Bytecode *create_minimal_bytecode(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_NIL, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/*
 * Test: block_limits_default returns valid defaults
 */
void test_block_limits_default(void) {
    BlockLimits limits = block_limits_default();

    ASSERT(limits.max_heap_size > 0);
    ASSERT(limits.max_stack_depth > 0);
    ASSERT(limits.max_call_depth > 0);
    ASSERT(limits.max_reductions > 0);
    ASSERT(limits.max_mailbox_size > 0);

    /* Verify specific default values */
    ASSERT_EQ(1 * 1024 * 1024, limits.max_heap_size);
    ASSERT_EQ(256, limits.max_stack_depth);
    ASSERT_EQ(64, limits.max_call_depth);
    ASSERT_EQ(10000, limits.max_reductions);
    ASSERT_EQ(100, limits.max_mailbox_size);
}

/*
 * Test: block_new allocates block
 */
void test_block_new_allocates(void) {
    Block *block = block_new(1, "test_block", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(1, block->pid);
    ASSERT_STR_EQ("test_block", block->name);

    block_free(block);
}

/*
 * Test: block_new with NULL name
 */
void test_block_new_null_name(void) {
    Block *block = block_new(1, NULL, NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(1, block->pid);
    ASSERT(block->name == NULL);

    block_free(block);
}

/*
 * Test: block_new initializes VM
 */
void test_block_new_initializes_vm(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(block->vm != NULL);
    ASSERT(block->vm->block == block);

    block_free(block);
}

/*
 * Test: block_new initializes heap
 */
void test_block_new_initializes_heap(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(block->heap != NULL);

    block_free(block);
}

/*
 * Test: block_new initializes mailbox
 */
void test_block_new_initializes_mailbox(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(!block_has_messages(block));

    block_free(block);
}

/*
 * Test: block_new sets default capabilities
 */
void test_block_new_default_capabilities(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(CAP_NONE, block->capabilities);

    block_free(block);
}

/*
 * Test: block_new sets default limits
 */
void test_block_new_default_limits(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    BlockLimits defaults = block_limits_default();
    ASSERT_EQ(defaults.max_heap_size, block->limits.max_heap_size);
    ASSERT_EQ(defaults.max_stack_depth, block->limits.max_stack_depth);
    ASSERT_EQ(defaults.max_call_depth, block->limits.max_call_depth);
    ASSERT_EQ(defaults.max_reductions, block->limits.max_reductions);
    ASSERT_EQ(defaults.max_mailbox_size, block->limits.max_mailbox_size);

    block_free(block);
}

/*
 * Test: block_new with custom limits
 */
void test_block_new_custom_limits(void) {
    BlockLimits limits = {
        .max_heap_size = 512 * 1024,
        .max_stack_depth = 128,
        .max_call_depth = 32,
        .max_reductions = 5000,
        .max_mailbox_size = 50,
    };

    Block *block = block_new(1, "test", &limits);
    ASSERT(block != NULL);

    ASSERT_EQ(512 * 1024, block->limits.max_heap_size);
    ASSERT_EQ(128, block->limits.max_stack_depth);
    ASSERT_EQ(32, block->limits.max_call_depth);
    ASSERT_EQ(5000, block->limits.max_reductions);
    ASSERT_EQ(50, block->limits.max_mailbox_size);

    block_free(block);
}

/*
 * Test: block_new initializes counters to zero
 */
void test_block_new_initializes_counters(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(0, block->counters.reductions);
    ASSERT_EQ(0, block->counters.messages_sent);
    ASSERT_EQ(0, atomic_load(&block->counters.messages_received));
    ASSERT_EQ(0, block->counters.gc_collections);

    block_free(block);
}

/*
 * Test: block_new initializes empty links
 */
void test_block_new_empty_links(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(block->links == NULL);
    ASSERT_EQ(0, block->link_count);
    ASSERT_EQ(0, block->link_capacity);

    block_free(block);
}

/*
 * Test: block_new initializes empty monitors
 */
void test_block_new_empty_monitors(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(block->monitors == NULL);
    ASSERT_EQ(0, block->monitor_count);
    ASSERT_EQ(0, block->monitor_capacity);

    ASSERT(block->monitored_by == NULL);
    ASSERT_EQ(0, block->monitored_by_count);
    ASSERT_EQ(0, block->monitored_by_capacity);

    block_free(block);
}

/*
 * Test: block_new sets parent to invalid
 */
void test_block_new_no_parent(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(PID_INVALID, block->parent);
    ASSERT(block->supervisor == NULL);

    block_free(block);
}

/*
 * Test: block_new sets state to RUNNABLE
 */
void test_block_new_state_runnable(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    block_free(block);
}

/*
 * Test: block_new initializes next/prev to NULL
 */
void test_block_new_queue_pointers_null(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(block->next == NULL);
    ASSERT(block->prev == NULL);

    block_free(block);
}

/*
 * Test: block_free with NULL is safe
 */
void test_block_free_null_safe(void) {
    block_free(NULL);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: block_free cleans up properly
 */
void test_block_free_cleanup(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* Add some links and monitors to test cleanup */
    block_link(block, 2);
    block_link(block, 3);
    block_monitor(block, 4);

    block_free(block);  /* Should not leak or crash */
    ASSERT(1);
}

/*
 * Test: block_load sets bytecode
 */
void test_block_load_sets_bytecode(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    bool loaded = block_load(block, code);
    ASSERT(loaded);
    ASSERT(block->code == code);

    block_free(block);
}

/*
 * Test: block_load with NULL block fails
 */
void test_block_load_null_block_fails(void) {
    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    bool loaded = block_load(NULL, code);
    ASSERT(!loaded);

    bytecode_free(code);
}

/*
 * Test: block_load with NULL code fails
 */
void test_block_load_null_code_fails(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    bool loaded = block_load(block, NULL);
    ASSERT(!loaded);

    block_free(block);
}

/*
 * Test: block_load sets state to RUNNABLE
 */
void test_block_load_sets_runnable(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    Bytecode *code = create_minimal_bytecode();
    block_load(block, code);

    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    block_free(block);
}

/*
 * Test: block_state returns correct state
 */
void test_block_state_returns_state(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    atomic_store(&block->state, BLOCK_WAITING);
    ASSERT_EQ(BLOCK_WAITING, block_state(block));

    atomic_store(&block->state, BLOCK_DEAD);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    block_free(block);
}

/*
 * Test: block_state with NULL returns DEAD
 */
void test_block_state_null_returns_dead(void) {
    ASSERT_EQ(BLOCK_DEAD, block_state(NULL));
}

/*
 * Test: block_set_state changes state
 */
void test_block_set_state(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_set_state(block, BLOCK_WAITING);
    ASSERT_EQ(BLOCK_WAITING, block_state(block));

    block_set_state(block, BLOCK_RUNNING);
    ASSERT_EQ(BLOCK_RUNNING, block_state(block));

    block_free(block);
}

/*
 * Test: block_set_state with NULL is safe
 */
void test_block_set_state_null_safe(void) {
    block_set_state(NULL, BLOCK_DEAD);
    ASSERT(1);
}

/*
 * Test: block_try_transition succeeds
 */
void test_block_try_transition_success(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    bool success = block_try_transition(block, BLOCK_RUNNABLE, BLOCK_RUNNING);
    ASSERT(success);
    ASSERT_EQ(BLOCK_RUNNING, block_state(block));

    block_free(block);
}

/*
 * Test: block_try_transition fails on mismatch
 */
void test_block_try_transition_mismatch(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    /* Try to transition from wrong state */
    bool success = block_try_transition(block, BLOCK_WAITING, BLOCK_RUNNING);
    ASSERT(!success);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));  /* Unchanged */

    block_free(block);
}

/*
 * Test: block_try_transition with NULL fails
 */
void test_block_try_transition_null(void) {
    bool success = block_try_transition(NULL, BLOCK_RUNNABLE, BLOCK_RUNNING);
    ASSERT(!success);
}

/*
 * Test: block_exit terminates block
 */
void test_block_exit_terminates(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(block_is_alive(block));

    block_exit(block, 0);

    ASSERT(!block_is_alive(block));
    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT_EQ(0, block->u.exit.exit_code);

    block_free(block);
}

/*
 * Test: block_exit with code
 */
void test_block_exit_with_code(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_exit(block, 42);

    ASSERT_EQ(42, block->u.exit.exit_code);

    block_free(block);
}

/*
 * Test: block_crash terminates with reason
 */
void test_block_crash_terminates(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT(block_is_alive(block));

    block_crash(block, "test error");

    ASSERT(!block_is_alive(block));
    ASSERT_EQ(BLOCK_DEAD, block_state(block));
    ASSERT(block->u.exit.exit_reason != NULL);
    ASSERT_STR_EQ("test error", block->u.exit.exit_reason);

    block_free(block);
}

/*
 * Test: block_is_alive for different states
 */
void test_block_is_alive_states(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    atomic_store(&block->state, BLOCK_RUNNABLE);
    ASSERT(block_is_alive(block));

    atomic_store(&block->state, BLOCK_RUNNING);
    ASSERT(block_is_alive(block));

    atomic_store(&block->state, BLOCK_WAITING);
    ASSERT(block_is_alive(block));

    atomic_store(&block->state, BLOCK_DEAD);
    ASSERT(!block_is_alive(block));

    block_free(block);
}

/*
 * Test: block_is_alive with NULL returns false
 */
void test_block_is_alive_null(void) {
    ASSERT(!block_is_alive(NULL));
}

/*
 * Test: block_state_name returns strings
 */
void test_block_state_name(void) {
    const char *name;

    name = block_state_name(BLOCK_RUNNABLE);
    ASSERT(name != NULL);

    name = block_state_name(BLOCK_RUNNING);
    ASSERT(name != NULL);

    name = block_state_name(BLOCK_WAITING);
    ASSERT(name != NULL);

    name = block_state_name(BLOCK_DEAD);
    ASSERT(name != NULL);
}

/*
 * Test: block_run executes to completion
 */
void test_block_run_executes(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    Bytecode *code = create_minimal_bytecode();
    block_load(block, code);

    BlockRunResult result = block_run(block);

    /* Block should halt/complete */
    ASSERT(result == BLOCK_RUN_OK || result == BLOCK_RUN_HALTED);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    block_free(block);
}

/*
 * Test: block_run with NULL returns error
 */
void test_block_run_null(void) {
    BlockRunResult result = block_run(NULL);
    ASSERT_EQ(BLOCK_RUN_ERROR, result);
}

/*
 * Test: block_run on dead block returns halted
 */
void test_block_run_dead_block(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    Bytecode *code = create_minimal_bytecode();
    block_load(block, code);

    block_exit(block, 0);  /* Kill the block */

    BlockRunResult result = block_run(block);
    ASSERT_EQ(BLOCK_RUN_HALTED, result);

    block_free(block);
}

/*
 * Test: Multiple blocks can be created
 */
void test_multiple_blocks(void) {
    Block *blocks[10];

    for (int i = 0; i < 10; i++) {
        blocks[i] = block_new(i + 1, "block", NULL);
        ASSERT(blocks[i] != NULL);
        ASSERT_EQ(i + 1, blocks[i]->pid);
    }

    for (int i = 0; i < 10; i++) {
        block_free(blocks[i]);
    }
}

/*
 * Test: block_new with very small heap limit
 */
void test_block_new_small_heap(void) {
    BlockLimits limits = block_limits_default();
    limits.max_heap_size = 4096;  /* 4KB */

    Block *block = block_new(1, "small", &limits);
    ASSERT(block != NULL);
    ASSERT_EQ(4096, block->limits.max_heap_size);

    block_free(block);
}

/*
 * Test: block_new with large heap limit
 */
void test_block_new_large_heap(void) {
    BlockLimits limits = block_limits_default();
    limits.max_heap_size = 100 * 1024 * 1024;  /* 100MB */

    Block *block = block_new(1, "large", &limits);
    ASSERT(block != NULL);
    ASSERT_EQ(100 * 1024 * 1024, block->limits.max_heap_size);

    block_free(block);
}

int main(void) {
    printf("Running block lifecycle tests...\n");

    printf("\nDefault limits tests:\n");
    RUN_TEST(test_block_limits_default);

    printf("\nblock_new tests:\n");
    RUN_TEST(test_block_new_allocates);
    RUN_TEST(test_block_new_null_name);
    RUN_TEST(test_block_new_initializes_vm);
    RUN_TEST(test_block_new_initializes_heap);
    RUN_TEST(test_block_new_initializes_mailbox);
    RUN_TEST(test_block_new_default_capabilities);
    RUN_TEST(test_block_new_default_limits);
    RUN_TEST(test_block_new_custom_limits);
    RUN_TEST(test_block_new_initializes_counters);
    RUN_TEST(test_block_new_empty_links);
    RUN_TEST(test_block_new_empty_monitors);
    RUN_TEST(test_block_new_no_parent);
    RUN_TEST(test_block_new_state_runnable);
    RUN_TEST(test_block_new_queue_pointers_null);

    printf("\nblock_free tests:\n");
    RUN_TEST(test_block_free_null_safe);
    RUN_TEST(test_block_free_cleanup);

    printf("\nblock_load tests:\n");
    RUN_TEST(test_block_load_sets_bytecode);
    RUN_TEST(test_block_load_null_block_fails);
    RUN_TEST(test_block_load_null_code_fails);
    RUN_TEST(test_block_load_sets_runnable);

    printf("\nState management tests:\n");
    RUN_TEST(test_block_state_returns_state);
    RUN_TEST(test_block_state_null_returns_dead);
    RUN_TEST(test_block_set_state);
    RUN_TEST(test_block_set_state_null_safe);
    RUN_TEST(test_block_try_transition_success);
    RUN_TEST(test_block_try_transition_mismatch);
    RUN_TEST(test_block_try_transition_null);

    printf("\nTermination tests:\n");
    RUN_TEST(test_block_exit_terminates);
    RUN_TEST(test_block_exit_with_code);
    RUN_TEST(test_block_crash_terminates);
    RUN_TEST(test_block_is_alive_states);
    RUN_TEST(test_block_is_alive_null);
    RUN_TEST(test_block_state_name);

    printf("\nExecution tests:\n");
    RUN_TEST(test_block_run_executes);
    RUN_TEST(test_block_run_null);
    RUN_TEST(test_block_run_dead_block);

    printf("\nMultiple blocks tests:\n");
    RUN_TEST(test_multiple_blocks);
    RUN_TEST(test_block_new_small_heap);
    RUN_TEST(test_block_new_large_heap);

    return TEST_RESULT();
}
