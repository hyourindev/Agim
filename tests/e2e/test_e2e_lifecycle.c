/*
 * Agim - End-to-End Process Lifecycle Tests
 *
 * Tests the complete lifecycle of processes (blocks) from spawning through
 * termination, including state transitions, resource management, linking,
 * and cleanup. These tests verify Erlang-like process semantics.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "runtime/supervisor.h"
#include "vm/bytecode.h"
#include "vm/value.h"

#include <unistd.h>

/*
 * Helper: Create minimal bytecode that returns immediately
 *
 * Generates bytecode: CONST 0, RETURN
 */
static Bytecode *make_return_bytecode(int return_value)
{
	Bytecode *code = bytecode_new();
	Value *val = value_int(return_value);
	size_t const_idx = chunk_add_constant(code->main, val);

	chunk_write_opcode(code->main, OP_CONST, 1);
	chunk_write_arg(code->main, (uint16_t)const_idx, 1);  /* 2-byte index */
	chunk_write_opcode(code->main, OP_RETURN, 1);

	return code;
}

/*
 * Helper: Create bytecode that yields then returns
 *
 * Generates bytecode: YIELD, CONST 0, RETURN
 */
static Bytecode *make_yield_bytecode(void)
{
	Bytecode *code = bytecode_new();
	Value *val = value_int(0);
	size_t const_idx = chunk_add_constant(code->main, val);

	chunk_write_opcode(code->main, OP_YIELD, 1);
	chunk_write_opcode(code->main, OP_CONST, 1);
	chunk_write_arg(code->main, (uint16_t)const_idx, 1);  /* 2-byte index */
	chunk_write_opcode(code->main, OP_RETURN, 1);

	return code;
}

/*
 * Helper: Create bytecode with infinite loop (for testing kill)
 *
 * Generates bytecode: YIELD, LOOP (jump back 4)
 * Layout: [0]=YIELD [1]=LOOP [2]=high [3]=low
 * After reading 2-byte arg, IP is at 4, so offset must be 4 to reach 0.
 */
static Bytecode *make_infinite_loop_bytecode(void)
{
	Bytecode *code = bytecode_new();

	chunk_write_opcode(code->main, OP_YIELD, 1);
	chunk_write_opcode(code->main, OP_LOOP, 1);
	chunk_write_arg(code->main, 0x0004, 1);  /* jump back 4 bytes */

	return code;
}

/* Test 1: Basic process spawn and terminate */
void test_spawn_and_terminate(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);
	ASSERT(sched != NULL);

	Bytecode *code = make_return_bytecode(42);
	ASSERT(code != NULL);

	/* Spawn process */
	Pid pid = scheduler_spawn(sched, code, "test_proc");
	ASSERT(pid != 0);

	/* Verify process exists */
	Block *block = scheduler_get_block(sched, pid);
	ASSERT(block != NULL);
	ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));
	ASSERT(block_is_alive(block));

	/* Run until completion */
	while (scheduler_step(sched)) {
		/* keep stepping */
	}

	/* Process should be dead */
	block = scheduler_get_block(sched, pid);
	ASSERT(block != NULL);
	ASSERT_EQ(BLOCK_DEAD, block_state(block));
	ASSERT(!block_is_alive(block));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 2: Process state transitions */
void test_state_transitions(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code = make_yield_bytecode();
	Pid pid = scheduler_spawn(sched, code, "state_test");
	Block *block = scheduler_get_block(sched, pid);

	/* Initial state: RUNNABLE */
	ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

	/* After one step: should have yielded */
	scheduler_step(sched);

	/* Step again to complete */
	scheduler_step(sched);

	/* Final state: DEAD */
	ASSERT_EQ(BLOCK_DEAD, block_state(block));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 3: Process kill while running */
void test_process_kill(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 100,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code = make_infinite_loop_bytecode();
	Pid pid = scheduler_spawn(sched, code, "infinite");
	Block *block = scheduler_get_block(sched, pid);

	ASSERT(block_is_alive(block));

	/* Run a few steps */
	for (int i = 0; i < 5; i++) {
		scheduler_step(sched);
	}

	/* Process should still be alive (infinite loop) */
	ASSERT(block_is_alive(block));

	/* Kill it */
	scheduler_kill(sched, pid);

	/* Should be dead now */
	ASSERT(!block_is_alive(block));
	ASSERT_EQ(BLOCK_DEAD, block_state(block));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 4: Multiple concurrent processes */
void test_multiple_processes(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 100,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code1 = make_yield_bytecode();
	Bytecode *code2 = make_yield_bytecode();
	Bytecode *code3 = make_yield_bytecode();

	Pid pid1 = scheduler_spawn(sched, code1, "proc1");
	Pid pid2 = scheduler_spawn(sched, code2, "proc2");
	Pid pid3 = scheduler_spawn(sched, code3, "proc3");

	/* All should be alive */
	ASSERT(block_is_alive(scheduler_get_block(sched, pid1)));
	ASSERT(block_is_alive(scheduler_get_block(sched, pid2)));
	ASSERT(block_is_alive(scheduler_get_block(sched, pid3)));

	/* PIDs should be unique */
	ASSERT(pid1 != pid2);
	ASSERT(pid2 != pid3);
	ASSERT(pid1 != pid3);

	/* Run until all complete */
	while (scheduler_step(sched)) {
		/* keep stepping */
	}

	/* All should be dead */
	ASSERT(!block_is_alive(scheduler_get_block(sched, pid1)));
	ASSERT(!block_is_alive(scheduler_get_block(sched, pid2)));
	ASSERT(!block_is_alive(scheduler_get_block(sched, pid3)));

	bytecode_free(code1);
	bytecode_free(code2);
	bytecode_free(code3);
	scheduler_free(sched);
}

/* Test 5: Process linking - bidirectional */
void test_process_linking(void)
{
	Block *block1 = block_new(1, "linker1", NULL);
	Block *block2 = block_new(2, "linker2", NULL);

	/* Link block1 to block2 */
	ASSERT(block_link(block1, 2));

	/* Verify link exists in block1 */
	size_t count;
	const Pid *links = block_get_links(block1, &count);
	ASSERT_EQ(1, count);
	ASSERT_EQ(2, links[0]);

	/* Link should be one-directional at block level */
	/* (bidirectional linking is managed by scheduler) */

	/* Add more links */
	ASSERT(block_link(block1, 3));
	ASSERT(block_link(block1, 4));

	links = block_get_links(block1, &count);
	ASSERT_EQ(3, count);

	/* Unlink one */
	block_unlink(block1, 3);
	links = block_get_links(block1, &count);
	ASSERT_EQ(2, count);

	/* Verify correct PIDs remain */
	bool has_2 = false, has_4 = false;
	for (size_t i = 0; i < count; i++) {
		if (links[i] == 2) has_2 = true;
		if (links[i] == 4) has_4 = true;
	}
	ASSERT(has_2 && has_4);

	block_free(block1);
	block_free(block2);
}

/* Test 6: Process monitoring */
void test_process_monitoring(void)
{
	Block *watcher = block_new(1, "watcher", NULL);
	Block *target = block_new(2, "target", NULL);

	/* Watcher monitors target */
	ASSERT(block_monitor(watcher, 2));

	/* Verify monitoring relationship */
	size_t count;
	const Pid *monitors = block_get_monitors(watcher, &count);
	ASSERT_EQ(1, count);
	ASSERT_EQ(2, monitors[0]);

	/* Remove monitor */
	block_demonitor(watcher, 2);
	monitors = block_get_monitors(watcher, &count);
	ASSERT_EQ(0, count);

	block_free(watcher);
	block_free(target);
}

/* Test 7: Resource limits enforcement */
void test_resource_limits(void)
{
	BlockLimits limits = {
		.max_heap_size = 1024,
		.max_stack_depth = 32,
		.max_call_depth = 8,
		.max_reductions = 100,
		.max_mailbox_size = 10,
	};

	Block *block = block_new(1, "limited", &limits);

	ASSERT_EQ(1024, block->limits.max_heap_size);
	ASSERT_EQ(32, block->limits.max_stack_depth);
	ASSERT_EQ(8, block->limits.max_call_depth);
	ASSERT_EQ(100, block->limits.max_reductions);
	ASSERT_EQ(10, block->limits.max_mailbox_size);

	block_free(block);
}

/* Test 8: Capability-based security */
void test_capabilities(void)
{
	Block *block = block_new(1, "secure", NULL);

	/* No capabilities by default */
	ASSERT(!block_has_cap(block, CAP_SPAWN));
	ASSERT(!block_has_cap(block, CAP_SEND));
	ASSERT(!block_has_cap(block, CAP_RECEIVE));
	ASSERT(!block_has_cap(block, CAP_INFER));
	ASSERT(!block_has_cap(block, CAP_HTTP));

	/* Grant specific capabilities */
	CapabilitySet caps = CAP_SPAWN | CAP_SEND | CAP_RECEIVE;
	block_grant(block, caps);

	ASSERT(block_has_cap(block, CAP_SPAWN));
	ASSERT(block_has_cap(block, CAP_SEND));
	ASSERT(block_has_cap(block, CAP_RECEIVE));
	ASSERT(!block_has_cap(block, CAP_INFER));

	/* Revoke one capability */
	block_revoke(block, CAP_SPAWN);
	ASSERT(!block_has_cap(block, CAP_SPAWN));
	ASSERT(block_has_cap(block, CAP_SEND));

	/* Grant all then revoke all */
	block_grant(block, CAP_ALL);
	ASSERT(block_has_cap(block, CAP_INFER));
	ASSERT(block_has_cap(block, CAP_HTTP));

	block_revoke(block, CAP_ALL);
	ASSERT(!block_has_cap(block, CAP_SPAWN));
	ASSERT(!block_has_cap(block, CAP_INFER));

	block_free(block);
}

/* Test 9: Process exit with code */
void test_process_exit_code(void)
{
	Block *block = block_new(1, "exiter", NULL);

	ASSERT(block_is_alive(block));
	ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

	/* Normal exit with code */
	block_exit(block, 42);

	ASSERT(!block_is_alive(block));
	ASSERT_EQ(BLOCK_DEAD, block_state(block));
	ASSERT_EQ(42, block->u.exit.exit_code);

	block_free(block);
}

/* Test 10: Process crash with reason */
void test_process_crash(void)
{
	Block *block = block_new(1, "crasher", NULL);

	ASSERT(block_is_alive(block));

	/* Crash with reason */
	block_crash(block, "out of memory");

	ASSERT(!block_is_alive(block));
	ASSERT_EQ(BLOCK_DEAD, block_state(block));
	ASSERT_STR_EQ("out of memory", block->u.exit.exit_reason);

	block_free(block);
}

/* Test 11: Scheduler statistics */
void test_scheduler_stats(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	SchedulerStats stats = scheduler_stats(sched);
	ASSERT_EQ(0, stats.blocks_total);
	ASSERT_EQ(0, stats.blocks_alive);

	Bytecode *code = make_return_bytecode(0);
	scheduler_spawn(sched, code, "stat_test");

	stats = scheduler_stats(sched);
	ASSERT_EQ(1, stats.blocks_total);
	ASSERT_EQ(1, stats.blocks_alive);
	ASSERT_EQ(1, stats.blocks_runnable);

	while (scheduler_step(sched)) {
		/* run to completion */
	}

	stats = scheduler_stats(sched);
	ASSERT_EQ(1, stats.blocks_dead);
	ASSERT_EQ(0, stats.blocks_alive);

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 12: Block counters tracking */
void test_block_counters(void)
{
	Block *block = block_new(1, "counter_test", NULL);

	/* Initial counters should be zero */
	ASSERT_EQ(0, block->counters.messages_sent);
	ASSERT_EQ(0, block->counters.messages_received);
	ASSERT_EQ(0, block->counters.reductions);

	/* Counters can be updated */
	block->counters.reductions = 100;
	block->counters.messages_sent = 5;
	block->counters.messages_received = 3;

	ASSERT_EQ(100, block->counters.reductions);
	ASSERT_EQ(5, block->counters.messages_sent);
	ASSERT_EQ(3, block->counters.messages_received);

	block_free(block);
}

int main(void)
{
	printf("=== E2E Process Lifecycle Tests ===\n\n");

	RUN_TEST(test_spawn_and_terminate);
	RUN_TEST(test_state_transitions);
	RUN_TEST(test_process_kill);
	RUN_TEST(test_multiple_processes);
	RUN_TEST(test_process_linking);
	RUN_TEST(test_process_monitoring);
	RUN_TEST(test_resource_limits);
	RUN_TEST(test_capabilities);
	RUN_TEST(test_process_exit_code);
	RUN_TEST(test_process_crash);
	RUN_TEST(test_scheduler_stats);
	RUN_TEST(test_block_counters);

	return TEST_RESULT();
}
