/*
 * Agim - Scheduler Spawn Tests
 *
 * P1.1.3.2: Tests for scheduler_spawn and scheduler_spawn_ex functions.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "runtime/capability.h"
#include "vm/bytecode.h"

/* Helper: Create minimal bytecode that just halts */
static Bytecode *create_minimal_bytecode(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_NIL, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Helper: Create bytecode that loops N times then halts */
static Bytecode *create_loop_bytecode(int iterations) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Push counter */
    chunk_add_constant(chunk, value_int(iterations));
    chunk_add_constant(chunk, value_int(1));
    chunk_add_constant(chunk, value_int(0));

    /* counter = iterations */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, 0, 1);
    chunk_write_byte(chunk, 0, 1);

    /* loop: if counter <= 0, jump to end */
    size_t loop_start = chunk->code_size;

    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, 0, 2);
    chunk_write_byte(chunk, 2, 2);
    chunk_write_opcode(chunk, OP_LE, 2);

    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);

    /* Pop the condition result (false means we continue) */
    chunk_write_opcode(chunk, OP_POP, 2);

    /* counter = counter - 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, 0, 3);
    chunk_write_byte(chunk, 1, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    /* jump back to loop */
    chunk_write_opcode(chunk, OP_LOOP, 4);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, offset & 0xFF, 4);

    /* end: halt */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);
    chunk_write_opcode(chunk, OP_HALT, 5);

    return code;
}

/*
 * Test: scheduler_spawn returns valid PID
 */
void test_spawn_returns_valid_pid(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);
    ASSERT(pid > 0);

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn increments next_pid
 */
void test_spawn_increments_pid(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code1 = create_minimal_bytecode();
    Bytecode *code2 = create_minimal_bytecode();
    Bytecode *code3 = create_minimal_bytecode();
    ASSERT(code1 != NULL);
    ASSERT(code2 != NULL);
    ASSERT(code3 != NULL);

    Pid pid1 = scheduler_spawn(sched, code1, "block1");
    Pid pid2 = scheduler_spawn(sched, code2, "block2");
    Pid pid3 = scheduler_spawn(sched, code3, "block3");

    ASSERT(pid1 != PID_INVALID);
    ASSERT(pid2 != PID_INVALID);
    ASSERT(pid3 != PID_INVALID);

    /* PIDs should be sequential */
    ASSERT_EQ(pid1 + 1, pid2);
    ASSERT_EQ(pid2 + 1, pid3);

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn registers block in registry
 */
void test_spawn_registers_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    /* Block should be retrievable via scheduler_get_block */
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(pid, block->pid);

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn enqueues block in run queue
 */
void test_spawn_enqueues_block(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Run queue should be empty initially */
    ASSERT(scheduler_queue_empty(sched));

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    /* Run queue should now have one block */
    ASSERT(!scheduler_queue_empty(sched));

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn with name
 */
void test_spawn_with_name(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "my_test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT(block->name != NULL);
    ASSERT_STR_EQ("my_test_block", block->name);

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn with NULL name
 */
void test_spawn_with_null_name(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, NULL);
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    /* Name can be NULL */

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn with NULL bytecode fails
 */
void test_spawn_null_bytecode_fails(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Pid pid = scheduler_spawn(sched, NULL, "test_block");
    ASSERT_EQ(PID_INVALID, pid);

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn with NULL scheduler fails
 */
void test_spawn_null_scheduler_fails(void) {
    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(NULL, code, "test_block");
    ASSERT_EQ(PID_INVALID, pid);

    bytecode_free(code);
}

/*
 * Test: scheduler_spawn_ex with CAP_NONE (default)
 */
void test_spawn_ex_with_cap_none(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn_ex(sched, code, "test_block", CAP_NONE, NULL);
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(CAP_NONE, block->capabilities);

    /* Block should not have any capabilities */
    ASSERT(!block_has_cap(block, CAP_SPAWN));
    ASSERT(!block_has_cap(block, CAP_SEND));
    ASSERT(!block_has_cap(block, CAP_RECEIVE));
    ASSERT(!block_has_cap(block, CAP_FILE_READ));

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn_ex with specific capabilities
 */
void test_spawn_ex_with_capabilities(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    CapabilitySet caps = CAP_SPAWN | CAP_SEND | CAP_RECEIVE;
    Pid pid = scheduler_spawn_ex(sched, code, "test_block", caps, NULL);
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(caps, block->capabilities);

    /* Block should have the requested capabilities */
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));

    /* Block should not have unrequested capabilities */
    ASSERT(!block_has_cap(block, CAP_FILE_READ));
    ASSERT(!block_has_cap(block, CAP_FILE_WRITE));
    ASSERT(!block_has_cap(block, CAP_SHELL));

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn_ex with CAP_ALL
 */
void test_spawn_ex_with_cap_all(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn_ex(sched, code, "test_block", CAP_ALL, NULL);
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(CAP_ALL, block->capabilities);

    /* Block should have all capabilities */
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));
    ASSERT(block_has_cap(block, CAP_FILE_READ));
    ASSERT(block_has_cap(block, CAP_FILE_WRITE));
    ASSERT(block_has_cap(block, CAP_SHELL));
    ASSERT(block_has_cap(block, CAP_EXEC));
    ASSERT(block_has_cap(block, CAP_INFER));
    ASSERT(block_has_cap(block, CAP_DB));
    ASSERT(block_has_cap(block, CAP_TRAP_EXIT));
    ASSERT(block_has_cap(block, CAP_MONITOR));
    ASSERT(block_has_cap(block, CAP_SUPERVISE));

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn_ex with custom limits
 */
void test_spawn_ex_with_limits(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    BlockLimits limits = {
        .max_heap_size = 1024 * 1024,  /* 1 MB */
        .max_stack_depth = 512,
        .max_call_depth = 64,
        .max_reductions = 5000,
        .max_mailbox_size = 100,
    };

    Pid pid = scheduler_spawn_ex(sched, code, "test_block", CAP_NONE, &limits);
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Verify limits were applied */
    ASSERT_EQ(limits.max_heap_size, block->limits.max_heap_size);
    ASSERT_EQ(limits.max_stack_depth, block->limits.max_stack_depth);
    ASSERT_EQ(limits.max_call_depth, block->limits.max_call_depth);
    ASSERT_EQ(limits.max_reductions, block->limits.max_reductions);
    ASSERT_EQ(limits.max_mailbox_size, block->limits.max_mailbox_size);

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn_ex with NULL limits uses defaults
 */
void test_spawn_ex_null_limits_uses_defaults(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn_ex(sched, code, "test_block", CAP_NONE, NULL);
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Should have default limits (non-zero) */
    BlockLimits defaults = block_limits_default();
    ASSERT_EQ(defaults.max_heap_size, block->limits.max_heap_size);
    ASSERT_EQ(defaults.max_stack_depth, block->limits.max_stack_depth);
    ASSERT_EQ(defaults.max_call_depth, block->limits.max_call_depth);
    ASSERT_EQ(defaults.max_reductions, block->limits.max_reductions);
    ASSERT_EQ(defaults.max_mailbox_size, block->limits.max_mailbox_size);

    scheduler_free(sched);
}

/*
 * Test: spawn at max_blocks fails
 */
void test_spawn_at_max_blocks_fails(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 3;  /* Very low limit */

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    Bytecode *code1 = create_minimal_bytecode();
    Bytecode *code2 = create_minimal_bytecode();
    Bytecode *code3 = create_minimal_bytecode();
    Bytecode *code4 = create_minimal_bytecode();
    ASSERT(code1 != NULL);
    ASSERT(code2 != NULL);
    ASSERT(code3 != NULL);
    ASSERT(code4 != NULL);

    /* Spawn up to max_blocks */
    Pid pid1 = scheduler_spawn(sched, code1, "block1");
    Pid pid2 = scheduler_spawn(sched, code2, "block2");
    Pid pid3 = scheduler_spawn(sched, code3, "block3");

    ASSERT(pid1 != PID_INVALID);
    ASSERT(pid2 != PID_INVALID);
    ASSERT(pid3 != PID_INVALID);

    /* Fourth spawn should fail */
    Pid pid4 = scheduler_spawn(sched, code4, "block4");
    ASSERT_EQ(PID_INVALID, pid4);

    /* Verify we have exactly max_blocks */
    ASSERT_EQ(3, scheduler_block_count(sched));

    /* Clean up the unused bytecode */
    bytecode_free(code4);

    scheduler_free(sched);
}

/*
 * Test: spawn increments total_spawned
 */
void test_spawn_increments_total_spawned(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    SchedulerStats stats_before = scheduler_stats(sched);
    ASSERT_EQ(0, stats_before.blocks_total);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    scheduler_spawn(sched, code, "test_block");

    SchedulerStats stats_after = scheduler_stats(sched);
    ASSERT_EQ(1, stats_after.blocks_total);

    scheduler_free(sched);
}

/*
 * Test: multiple spawns update stats correctly
 */
void test_multiple_spawns_update_stats(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    for (int i = 0; i < 10; i++) {
        Bytecode *code = create_minimal_bytecode();
        ASSERT(code != NULL);

        Pid pid = scheduler_spawn(sched, code, "test_block");
        ASSERT(pid != PID_INVALID);
    }

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(10, stats.blocks_total);
    ASSERT_EQ(10, scheduler_block_count(sched));

    scheduler_free(sched);
}

/*
 * Test: spawn initializes block state to RUNNABLE
 */
void test_spawn_initializes_state_runnable(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    scheduler_free(sched);
}

/*
 * Test: spawn sets up VM in block
 */
void test_spawn_sets_up_vm(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT(block->vm != NULL);
    ASSERT(block->heap != NULL);
    ASSERT(block->code != NULL);

    /* VM should reference the scheduler */
    ASSERT(block->vm->scheduler == sched);

    scheduler_free(sched);
}

/*
 * Test: spawn sets parent PID to invalid (no parent)
 */
void test_spawn_has_no_parent(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Blocks spawned via scheduler_spawn have no parent */
    ASSERT_EQ(PID_INVALID, block->parent);

    scheduler_free(sched);
}

/*
 * Test: spawn initializes empty mailbox
 */
void test_spawn_initializes_empty_mailbox(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Mailbox should be empty */
    ASSERT(!block_has_messages(block));

    scheduler_free(sched);
}

/*
 * Test: spawn initializes empty links
 */
void test_spawn_initializes_empty_links(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* No links initially */
    size_t link_count;
    const Pid *links = block_get_links(block, &link_count);
    ASSERT_EQ(0, link_count);
    (void)links;  /* Silence unused warning */

    scheduler_free(sched);
}

/*
 * Test: spawn initializes counters to zero
 */
void test_spawn_initializes_counters(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Counters should start at zero */
    ASSERT_EQ(0, block->counters.reductions);
    ASSERT_EQ(0, block->counters.messages_sent);
    ASSERT_EQ(0, atomic_load(&block->counters.messages_received));
    ASSERT_EQ(0, block->counters.gc_collections);

    scheduler_free(sched);
}

/*
 * Test: spawned block is alive
 */
void test_spawned_block_is_alive(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT(block_is_alive(block));

    scheduler_free(sched);
}

/*
 * Test: scheduler_spawn uses CAP_NONE by default
 */
void test_spawn_uses_cap_none_by_default(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    /* scheduler_spawn (not _ex) should use CAP_NONE */
    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(CAP_NONE, block->capabilities);

    scheduler_free(sched);
}

/*
 * Test: spawning many blocks works correctly
 */
void test_spawn_many_blocks(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 1000;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Spawn 100 blocks */
    Pid first_pid = PID_INVALID;
    Pid last_pid = PID_INVALID;

    for (int i = 0; i < 100; i++) {
        Bytecode *code = create_minimal_bytecode();
        ASSERT(code != NULL);

        Pid pid = scheduler_spawn(sched, code, "block");
        ASSERT(pid != PID_INVALID);

        if (i == 0) first_pid = pid;
        if (i == 99) last_pid = pid;

        /* Verify block is retrievable */
        Block *block = scheduler_get_block(sched, pid);
        ASSERT(block != NULL);
        ASSERT_EQ(pid, block->pid);
    }

    /* All PIDs should be sequential */
    ASSERT_EQ(99, last_pid - first_pid);

    /* All blocks should be registered */
    ASSERT_EQ(100, scheduler_block_count(sched));

    scheduler_free(sched);
}

/*
 * Test: get_block returns NULL for invalid PID
 */
void test_get_block_invalid_pid_returns_null(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Block *block = scheduler_get_block(sched, PID_INVALID);
    ASSERT(block == NULL);

    /* Non-existent PID */
    block = scheduler_get_block(sched, 99999);
    ASSERT(block == NULL);

    scheduler_free(sched);
}

/*
 * Test: get_block returns NULL for NULL scheduler
 */
void test_get_block_null_scheduler_returns_null(void) {
    Block *block = scheduler_get_block(NULL, 1);
    ASSERT(block == NULL);
}

/*
 * Test: spawn_ex with combined capabilities
 */
void test_spawn_ex_combined_capabilities(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    /* Combine multiple capabilities */
    CapabilitySet caps = CAP_SPAWN | CAP_SEND | CAP_RECEIVE |
                          CAP_FILE_READ | CAP_INFER | CAP_TRAP_EXIT;

    Pid pid = scheduler_spawn_ex(sched, code, "test_block", caps, NULL);
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Verify each capability */
    ASSERT(block_has_cap(block, CAP_SPAWN));
    ASSERT(block_has_cap(block, CAP_SEND));
    ASSERT(block_has_cap(block, CAP_RECEIVE));
    ASSERT(block_has_cap(block, CAP_FILE_READ));
    ASSERT(block_has_cap(block, CAP_INFER));
    ASSERT(block_has_cap(block, CAP_TRAP_EXIT));

    /* Verify unrequested capabilities are absent */
    ASSERT(!block_has_cap(block, CAP_FILE_WRITE));
    ASSERT(!block_has_cap(block, CAP_SHELL));
    ASSERT(!block_has_cap(block, CAP_EXEC));
    ASSERT(!block_has_cap(block, CAP_DB));

    scheduler_free(sched);
}

/*
 * Test: spawn with very restrictive limits
 */
void test_spawn_with_restrictive_limits(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    ASSERT(code != NULL);

    BlockLimits limits = {
        .max_heap_size = 4096,      /* Very small: 4KB */
        .max_stack_depth = 16,       /* Very shallow */
        .max_call_depth = 4,         /* Very limited recursion */
        .max_reductions = 100,       /* Very few instructions */
        .max_mailbox_size = 5,       /* Very small mailbox */
    };

    Pid pid = scheduler_spawn_ex(sched, code, "restricted_block", CAP_NONE, &limits);
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);

    /* Verify restrictive limits applied */
    ASSERT_EQ(4096, block->limits.max_heap_size);
    ASSERT_EQ(16, block->limits.max_stack_depth);
    ASSERT_EQ(4, block->limits.max_call_depth);
    ASSERT_EQ(100, block->limits.max_reductions);
    ASSERT_EQ(5, block->limits.max_mailbox_size);

    scheduler_free(sched);
}

/*
 * Test: PID uniqueness across registry shards
 */
void test_pid_uniqueness_across_shards(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn enough blocks to span multiple shards (REGISTRY_SHARDS = 64) */
    Pid pids[128];

    for (int i = 0; i < 128; i++) {
        Bytecode *code = create_minimal_bytecode();
        ASSERT(code != NULL);

        pids[i] = scheduler_spawn(sched, code, "block");
        ASSERT(pids[i] != PID_INVALID);
    }

    /* Verify all PIDs are unique */
    for (int i = 0; i < 128; i++) {
        for (int j = i + 1; j < 128; j++) {
            ASSERT(pids[i] != pids[j]);
        }
    }

    /* Verify all blocks are retrievable */
    for (int i = 0; i < 128; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        ASSERT(block != NULL);
        ASSERT_EQ(pids[i], block->pid);
    }

    scheduler_free(sched);
}

/*
 * Test: spawn with looping bytecode (verify block executes)
 */
void test_spawn_with_loop_bytecode(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Create bytecode that loops a few times */
    Bytecode *code = create_loop_bytecode(10);
    ASSERT(code != NULL);

    Pid pid = scheduler_spawn(sched, code, "looper");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    /* Run the scheduler to completion */
    scheduler_run(sched);

    /* Block should have completed */
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    /* Should have accumulated some reductions */
    SchedulerStats stats = scheduler_stats(sched);
    ASSERT(stats.total_reductions > 0);

    scheduler_free(sched);
}

int main(void) {
    printf("Running scheduler spawn tests...\n");

    printf("\nBasic spawn tests:\n");
    RUN_TEST(test_spawn_returns_valid_pid);
    RUN_TEST(test_spawn_increments_pid);
    RUN_TEST(test_spawn_registers_block);
    RUN_TEST(test_spawn_enqueues_block);
    RUN_TEST(test_spawn_with_name);
    RUN_TEST(test_spawn_with_null_name);

    printf("\nSpawn failure tests:\n");
    RUN_TEST(test_spawn_null_bytecode_fails);
    RUN_TEST(test_spawn_null_scheduler_fails);
    RUN_TEST(test_spawn_at_max_blocks_fails);

    printf("\nScheduler_spawn_ex capability tests:\n");
    RUN_TEST(test_spawn_ex_with_cap_none);
    RUN_TEST(test_spawn_ex_with_capabilities);
    RUN_TEST(test_spawn_ex_with_cap_all);
    RUN_TEST(test_spawn_ex_combined_capabilities);
    RUN_TEST(test_spawn_uses_cap_none_by_default);

    printf("\nScheduler_spawn_ex limits tests:\n");
    RUN_TEST(test_spawn_ex_with_limits);
    RUN_TEST(test_spawn_ex_null_limits_uses_defaults);
    RUN_TEST(test_spawn_with_restrictive_limits);

    printf("\nBlock initialization tests:\n");
    RUN_TEST(test_spawn_initializes_state_runnable);
    RUN_TEST(test_spawn_sets_up_vm);
    RUN_TEST(test_spawn_has_no_parent);
    RUN_TEST(test_spawn_initializes_empty_mailbox);
    RUN_TEST(test_spawn_initializes_empty_links);
    RUN_TEST(test_spawn_initializes_counters);
    RUN_TEST(test_spawned_block_is_alive);

    printf("\nStatistics tests:\n");
    RUN_TEST(test_spawn_increments_total_spawned);
    RUN_TEST(test_multiple_spawns_update_stats);

    printf("\nRegistry tests:\n");
    RUN_TEST(test_get_block_invalid_pid_returns_null);
    RUN_TEST(test_get_block_null_scheduler_returns_null);
    RUN_TEST(test_spawn_many_blocks);
    RUN_TEST(test_pid_uniqueness_across_shards);

    printf("\nExecution tests:\n");
    RUN_TEST(test_spawn_with_loop_bytecode);

    return TEST_RESULT();
}
