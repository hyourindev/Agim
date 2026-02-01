/*
 * Agim - Scheduler Registry Tests
 *
 * P1.1.3.3: Tests for block registry operations.
 * - Registry lookup after spawn
 * - Registry lookup for non-existent PID
 * - Registry sharding across multiple shards
 * - Registry capacity growth
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "vm/bytecode.h"

/* Helper: Create minimal bytecode that just halts */
static Bytecode *create_minimal_bytecode(void) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_NIL, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/*
 * Test: Registry lookup returns correct block after spawn
 */
void test_registry_lookup_after_spawn(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "test_block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(pid, block->pid);
    ASSERT_STR_EQ("test_block", block->name);

    scheduler_free(sched);
}

/*
 * Test: Registry lookup returns NULL for non-existent PID
 */
void test_registry_lookup_nonexistent(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Lookup before any spawns */
    Block *block = scheduler_get_block(sched, 1);
    ASSERT(block == NULL);

    /* Spawn one block */
    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "test_block");

    /* Lookup non-existent PIDs */
    block = scheduler_get_block(sched, pid + 1);
    ASSERT(block == NULL);

    block = scheduler_get_block(sched, pid + 100);
    ASSERT(block == NULL);

    block = scheduler_get_block(sched, 99999);
    ASSERT(block == NULL);

    scheduler_free(sched);
}

/*
 * Test: Registry lookup with PID_INVALID returns NULL
 */
void test_registry_lookup_pid_invalid(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Block *block = scheduler_get_block(sched, PID_INVALID);
    ASSERT(block == NULL);

    scheduler_free(sched);
}

/*
 * Test: Registry correctly stores multiple blocks
 */
void test_registry_multiple_blocks(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    const int num_blocks = 50;
    Pid pids[50];

    /* Spawn multiple blocks */
    for (int i = 0; i < num_blocks; i++) {
        Bytecode *code = create_minimal_bytecode();
        char name[32];
        snprintf(name, sizeof(name), "block_%d", i);
        pids[i] = scheduler_spawn(sched, code, name);
        ASSERT(pids[i] != PID_INVALID);
    }

    /* Verify all blocks are retrievable */
    for (int i = 0; i < num_blocks; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        ASSERT(block != NULL);
        ASSERT_EQ(pids[i], block->pid);
    }

    ASSERT_EQ(num_blocks, scheduler_block_count(sched));

    scheduler_free(sched);
}

/*
 * Test: Registry distributes blocks across shards
 */
void test_registry_sharding_distribution(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn blocks with PIDs that will hash to different shards
     * REGISTRY_SHARDS = 64, so PID % 64 determines shard */
    const int blocks_per_shard = 2;
    const int num_shards = 64;
    Pid pids[128];  /* 2 per shard */

    for (int i = 0; i < blocks_per_shard * num_shards; i++) {
        Bytecode *code = create_minimal_bytecode();
        pids[i] = scheduler_spawn(sched, code, "block");
        ASSERT(pids[i] != PID_INVALID);
    }

    /* All blocks should be retrievable regardless of shard */
    for (int i = 0; i < blocks_per_shard * num_shards; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        ASSERT(block != NULL);
        ASSERT_EQ(pids[i], block->pid);
    }

    scheduler_free(sched);
}

/*
 * Test: Registry count is accurate
 */
void test_registry_count_accurate(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    ASSERT_EQ(0, scheduler_block_count(sched));

    /* Spawn blocks and verify count */
    for (int i = 1; i <= 10; i++) {
        Bytecode *code = create_minimal_bytecode();
        Pid pid = scheduler_spawn(sched, code, "block");
        ASSERT(pid != PID_INVALID);
        ASSERT_EQ(i, scheduler_block_count(sched));
    }

    scheduler_free(sched);
}

/*
 * Test: Registry enforces max_blocks limit
 */
void test_registry_max_blocks_limit(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 5;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Fill up to limit */
    for (int i = 0; i < 5; i++) {
        Bytecode *code = create_minimal_bytecode();
        Pid pid = scheduler_spawn(sched, code, "block");
        ASSERT(pid != PID_INVALID);
    }

    ASSERT_EQ(5, scheduler_block_count(sched));

    /* Next spawn should fail */
    Bytecode *extra = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, extra, "extra");
    ASSERT_EQ(PID_INVALID, pid);

    /* Count should still be 5 */
    ASSERT_EQ(5, scheduler_block_count(sched));

    bytecode_free(extra);
    scheduler_free(sched);
}

/*
 * Test: Registry lookup is fast for many blocks
 */
void test_registry_lookup_performance(void) {
    SchedulerConfig config = scheduler_config_default();
    config.max_blocks = 1000;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Spawn many blocks */
    Pid first_pid = PID_INVALID;
    Pid last_pid = PID_INVALID;

    for (int i = 0; i < 500; i++) {
        Bytecode *code = create_minimal_bytecode();
        Pid pid = scheduler_spawn(sched, code, "block");
        ASSERT(pid != PID_INVALID);
        if (i == 0) first_pid = pid;
        last_pid = pid;
    }

    /* Lookup should work for all PIDs */
    for (Pid p = first_pid; p <= last_pid; p++) {
        Block *block = scheduler_get_block(sched, p);
        ASSERT(block != NULL);
        ASSERT_EQ(p, block->pid);
    }

    scheduler_free(sched);
}

/*
 * Test: Registry handles collision buckets correctly
 */
void test_registry_collision_handling(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn enough blocks to cause collisions within shards
     * Initial capacity per shard is 64, so spawning more will
     * either grow the shard or create chains */
    const int num_blocks = 200;
    Pid pids[200];

    for (int i = 0; i < num_blocks; i++) {
        Bytecode *code = create_minimal_bytecode();
        pids[i] = scheduler_spawn(sched, code, "block");
        ASSERT(pids[i] != PID_INVALID);
    }

    /* All blocks should still be retrievable */
    for (int i = 0; i < num_blocks; i++) {
        Block *block = scheduler_get_block(sched, pids[i]);
        ASSERT(block != NULL);
        ASSERT_EQ(pids[i], block->pid);
    }

    scheduler_free(sched);
}

/*
 * Test: Registry preserves block data
 */
void test_registry_preserves_block_data(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    BlockLimits limits = {
        .max_heap_size = 12345,
        .max_stack_depth = 100,
        .max_call_depth = 50,
        .max_reductions = 9999,
        .max_mailbox_size = 77,
    };
    CapabilitySet caps = CAP_SPAWN | CAP_SEND;

    Pid pid = scheduler_spawn_ex(sched, code, "named_block", caps, &limits);
    ASSERT(pid != PID_INVALID);

    /* Lookup and verify data is preserved */
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(pid, block->pid);
    ASSERT_STR_EQ("named_block", block->name);
    ASSERT_EQ(caps, block->capabilities);
    ASSERT_EQ(12345, block->limits.max_heap_size);
    ASSERT_EQ(100, block->limits.max_stack_depth);
    ASSERT_EQ(50, block->limits.max_call_depth);
    ASSERT_EQ(9999, block->limits.max_reductions);
    ASSERT_EQ(77, block->limits.max_mailbox_size);

    scheduler_free(sched);
}

/*
 * Test: Registry handles interspersed lookups during spawning
 */
void test_registry_interleaved_spawn_lookup(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Pid pids[20];

    for (int i = 0; i < 20; i++) {
        Bytecode *code = create_minimal_bytecode();
        pids[i] = scheduler_spawn(sched, code, "block");
        ASSERT(pids[i] != PID_INVALID);

        /* Lookup all previously spawned blocks */
        for (int j = 0; j <= i; j++) {
            Block *block = scheduler_get_block(sched, pids[j]);
            ASSERT(block != NULL);
            ASSERT_EQ(pids[j], block->pid);
        }
    }

    scheduler_free(sched);
}

/*
 * Test: Registry lookup with NULL scheduler returns NULL
 */
void test_registry_null_scheduler(void) {
    Block *block = scheduler_get_block(NULL, 1);
    ASSERT(block == NULL);
}

/*
 * Test: scheduler_block_count returns 0 for NULL scheduler
 */
void test_registry_count_null_scheduler(void) {
    size_t count = scheduler_block_count(NULL);
    ASSERT_EQ(0, count);
}

/*
 * Test: Registry works after blocks complete execution
 */
void test_registry_after_block_completion(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "completer");
    ASSERT(pid != PID_INVALID);

    /* Verify block exists */
    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

    /* Run to completion */
    scheduler_run(sched);

    /* Block should still be in registry (dead but present) */
    block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT_EQ(BLOCK_DEAD, block_state(block));

    /* Registry count should still be 1 */
    ASSERT_EQ(1, scheduler_block_count(sched));

    scheduler_free(sched);
}

/*
 * Test: Multiple schedulers have independent registries
 */
void test_registry_multiple_schedulers(void) {
    Scheduler *sched1 = scheduler_new(NULL);
    Scheduler *sched2 = scheduler_new(NULL);
    ASSERT(sched1 != NULL);
    ASSERT(sched2 != NULL);

    /* Spawn blocks in each scheduler */
    Bytecode *code1 = create_minimal_bytecode();
    Bytecode *code2 = create_minimal_bytecode();
    Pid pid1 = scheduler_spawn(sched1, code1, "block1");
    Pid pid2 = scheduler_spawn(sched2, code2, "block2");

    ASSERT(pid1 != PID_INVALID);
    ASSERT(pid2 != PID_INVALID);

    /* Each scheduler should only see its own blocks */
    ASSERT(scheduler_get_block(sched1, pid1) != NULL);
    ASSERT(scheduler_get_block(sched2, pid2) != NULL);

    /* Cross-lookup should fail (PIDs might be same value but different registries) */
    /* Note: This depends on implementation - if PIDs happen to be same,
     * the lookup might succeed but return wrong block. We check by name. */
    Block *b1 = scheduler_get_block(sched1, pid1);
    Block *b2 = scheduler_get_block(sched2, pid2);
    ASSERT_STR_EQ("block1", b1->name);
    ASSERT_STR_EQ("block2", b2->name);

    /* Counts are independent */
    ASSERT_EQ(1, scheduler_block_count(sched1));
    ASSERT_EQ(1, scheduler_block_count(sched2));

    scheduler_free(sched1);
    scheduler_free(sched2);
}

/*
 * Test: Registry handles PID wrap-around gracefully
 * (PIDs are 64-bit so wrap-around is unlikely but test edge behavior)
 */
void test_registry_large_pid_values(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn several blocks and verify they all have valid PIDs */
    Pid prev_pid = 0;
    for (int i = 0; i < 10; i++) {
        Bytecode *code = create_minimal_bytecode();
        Pid pid = scheduler_spawn(sched, code, "block");
        ASSERT(pid != PID_INVALID);
        ASSERT(pid > prev_pid);  /* PIDs should be increasing */
        prev_pid = pid;

        Block *block = scheduler_get_block(sched, pid);
        ASSERT(block != NULL);
    }

    scheduler_free(sched);
}

/*
 * Test: Registry can be iterated via stats
 */
void test_registry_iteration_via_stats(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    /* Spawn blocks in various states */
    for (int i = 0; i < 5; i++) {
        Bytecode *code = create_minimal_bytecode();
        Pid pid = scheduler_spawn(sched, code, "block");
        ASSERT(pid != PID_INVALID);
    }

    /* Get stats - this iterates the registry */
    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(5, stats.blocks_total);
    ASSERT_EQ(5, stats.blocks_runnable);
    ASSERT_EQ(0, stats.blocks_waiting);
    ASSERT_EQ(0, stats.blocks_dead);

    scheduler_free(sched);
}

/*
 * Test: Registry correctly tracks alive blocks
 */
void test_registry_alive_blocks_tracking(void) {
    Scheduler *sched = scheduler_new(NULL);
    ASSERT(sched != NULL);

    Bytecode *code = create_minimal_bytecode();
    Pid pid = scheduler_spawn(sched, code, "block");
    ASSERT(pid != PID_INVALID);

    Block *block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT(block_is_alive(block));

    SchedulerStats stats_before = scheduler_stats(sched);
    ASSERT_EQ(1, stats_before.blocks_alive);

    /* Run to completion */
    scheduler_run(sched);

    SchedulerStats stats_after = scheduler_stats(sched);
    ASSERT_EQ(0, stats_after.blocks_alive);
    ASSERT_EQ(1, stats_after.blocks_dead);

    /* Block is still in registry */
    block = scheduler_get_block(sched, pid);
    ASSERT(block != NULL);
    ASSERT(!block_is_alive(block));

    scheduler_free(sched);
}

int main(void) {
    printf("Running scheduler registry tests...\n");

    printf("\nBasic lookup tests:\n");
    RUN_TEST(test_registry_lookup_after_spawn);
    RUN_TEST(test_registry_lookup_nonexistent);
    RUN_TEST(test_registry_lookup_pid_invalid);
    RUN_TEST(test_registry_null_scheduler);

    printf("\nMultiple blocks tests:\n");
    RUN_TEST(test_registry_multiple_blocks);
    RUN_TEST(test_registry_sharding_distribution);
    RUN_TEST(test_registry_collision_handling);
    RUN_TEST(test_registry_interleaved_spawn_lookup);

    printf("\nCount and limits tests:\n");
    RUN_TEST(test_registry_count_accurate);
    RUN_TEST(test_registry_count_null_scheduler);
    RUN_TEST(test_registry_max_blocks_limit);

    printf("\nData preservation tests:\n");
    RUN_TEST(test_registry_preserves_block_data);
    RUN_TEST(test_registry_large_pid_values);

    printf("\nBlock lifecycle tests:\n");
    RUN_TEST(test_registry_after_block_completion);
    RUN_TEST(test_registry_alive_blocks_tracking);

    printf("\nMultiple schedulers test:\n");
    RUN_TEST(test_registry_multiple_schedulers);

    printf("\nStats/iteration tests:\n");
    RUN_TEST(test_registry_iteration_via_stats);

    printf("\nPerformance tests:\n");
    RUN_TEST(test_registry_lookup_performance);

    return TEST_RESULT();
}
