/*
 * Agim - End-to-End Supervision Tests
 *
 * Tests the OTP-style supervisor tree implementation including restart
 * strategies (one-for-one, one-for-all, rest-for-one), child management,
 * restart limits, and cascading failures. Validates Erlang supervision
 * semantics.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/supervisor.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "vm/bytecode.h"

/*
 * Helper: Create bytecode that returns immediately
 */
__attribute__((unused))
static Bytecode *make_simple_bytecode(void)
{
	Bytecode *code = bytecode_new();
	Value *val = value_int(0);
	size_t idx = chunk_add_constant(code->main, val);

	chunk_write_opcode(code->main, OP_CONST, 1);
	chunk_write_arg(code->main, (uint16_t)idx, 1);  /* 2-byte index */
	chunk_write_opcode(code->main, OP_RETURN, 1);

	return code;
}

/*
 * Helper: Create bytecode with infinite loop
 * Layout: [0]=YIELD [1]=LOOP [2]=high [3]=low
 * After reading 2-byte arg, IP is at 4, so offset must be 4 to reach 0.
 */
static Bytecode *make_loop_bytecode(void)
{
	Bytecode *code = bytecode_new();

	chunk_write_opcode(code->main, OP_YIELD, 1);
	chunk_write_opcode(code->main, OP_LOOP, 1);
	chunk_write_arg(code->main, 0x0004, 1);  /* jump back 4 bytes */

	return code;
}

/* Test 1: Supervisor creation */
void test_supervisor_creation(void)
{
	Supervisor *sup = supervisor_new(SUP_ONE_FOR_ONE);
	ASSERT(sup != NULL);
	ASSERT_EQ(SUP_ONE_FOR_ONE, sup->strategy);
	ASSERT_EQ(0, sup->child_count);
	ASSERT(!sup->shutting_down);

	supervisor_free(sup);
}

/* Test 2: Different supervisor strategies */
void test_supervisor_strategies(void)
{
	Supervisor *one_for_one = supervisor_new(SUP_ONE_FOR_ONE);
	Supervisor *one_for_all = supervisor_new(SUP_ONE_FOR_ALL);
	Supervisor *rest_for_one = supervisor_new(SUP_REST_FOR_ONE);

	ASSERT_EQ(SUP_ONE_FOR_ONE, one_for_one->strategy);
	ASSERT_EQ(SUP_ONE_FOR_ALL, one_for_all->strategy);
	ASSERT_EQ(SUP_REST_FOR_ONE, rest_for_one->strategy);

	supervisor_free(one_for_one);
	supervisor_free(one_for_all);
	supervisor_free(rest_for_one);
}

/* Test 3: Add child to supervisor */
void test_add_child(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	/* Create supervisor block */
	Block *sup_block = block_new(1, "supervisor", NULL);
	ASSERT(supervisor_init_block(sup_block, SUP_ONE_FOR_ONE));
	ASSERT(sup_block->supervisor != NULL);

	Supervisor *sup = sup_block->supervisor;

	/* Add child */
	Bytecode *code = make_loop_bytecode();
	ASSERT(supervisor_add_child(sup, sched, sup_block, "child1", code,
				    RESTART_PERMANENT));

	ASSERT_EQ(1, sup->child_count);
	ASSERT_EQ(1, supervisor_active_count(sup));

	/* Child should have been spawned */
	ChildSpec *child = &sup->children[0];
	ASSERT_STR_EQ("child1", child->name);
	ASSERT_EQ(RESTART_PERMANENT, child->restart);
	ASSERT(child->child_pid != 0);

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 4: Add multiple children */
void test_add_multiple_children(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	Bytecode *code1 = make_loop_bytecode();
	Bytecode *code2 = make_loop_bytecode();
	Bytecode *code3 = make_loop_bytecode();

	ASSERT(supervisor_add_child(sup, sched, sup_block, "worker1", code1,
				    RESTART_PERMANENT));
	ASSERT(supervisor_add_child(sup, sched, sup_block, "worker2", code2,
				    RESTART_TRANSIENT));
	ASSERT(supervisor_add_child(sup, sched, sup_block, "worker3", code3,
				    RESTART_TEMPORARY));

	ASSERT_EQ(3, sup->child_count);
	ASSERT_EQ(3, supervisor_active_count(sup));

	/* Verify restart strategies */
	ASSERT_EQ(RESTART_PERMANENT, sup->children[0].restart);
	ASSERT_EQ(RESTART_TRANSIENT, sup->children[1].restart);
	ASSERT_EQ(RESTART_TEMPORARY, sup->children[2].restart);

	bytecode_release(code1);
	bytecode_release(code2);
	bytecode_release(code3);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 5: Remove child from supervisor */
void test_remove_child(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	Bytecode *code = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "removable", code,
			     RESTART_PERMANENT);

	ASSERT_EQ(1, sup->child_count);

	/* Remove child */
	ASSERT(supervisor_remove_child(sup, sched, "removable"));
	ASSERT_EQ(0, sup->child_count);

	/* Remove non-existent child should fail */
	ASSERT(!supervisor_remove_child(sup, sched, "nonexistent"));

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 6: Handle child exit - RESTART_PERMANENT */
void test_restart_permanent(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	Bytecode *code = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "permanent", code,
			     RESTART_PERMANENT);

	Pid original_pid = sup->children[0].child_pid;

	/* Simulate normal exit - should restart because PERMANENT */
	ASSERT(supervisor_handle_exit(sup, sched, sup_block, original_pid,
				       EXIT_NORMAL, 0, NULL));

	/* Child should have new PID */
	Pid new_pid = sup->children[0].child_pid;
	ASSERT(new_pid != original_pid);
	ASSERT_EQ(1, sup->children[0].restart_count);

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 7: Handle child exit - RESTART_TRANSIENT */
void test_restart_transient(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	Bytecode *code = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "transient", code,
			     RESTART_TRANSIENT);

	Pid original_pid = sup->children[0].child_pid;

	/* Normal exit - should NOT restart */
	supervisor_handle_exit(sup, sched, sup_block, original_pid,
			       EXIT_NORMAL, 0, NULL);

	/* PID should be 0 (not restarted) */
	ASSERT_EQ(0, sup->children[0].child_pid);

	/* Add new transient child for crash test */
	Bytecode *code2 = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "transient2", code2,
			     RESTART_TRANSIENT);
	original_pid = sup->children[1].child_pid;

	/* Crash - should restart */
	supervisor_handle_exit(sup, sched, sup_block, original_pid,
			       EXIT_CRASH, 1, "error");

	/* Should have new PID (restarted due to crash) */
	ASSERT(sup->children[1].child_pid != 0);

	bytecode_release(code);
	bytecode_release(code2);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 8: Handle child exit - RESTART_TEMPORARY */
void test_restart_temporary(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	Bytecode *code = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "temporary", code,
			     RESTART_TEMPORARY);

	Pid original_pid = sup->children[0].child_pid;

	/* Any exit - should NOT restart */
	supervisor_handle_exit(sup, sched, sup_block, original_pid,
			       EXIT_CRASH, 1, "error");

	/* PID should be 0 (not restarted) */
	ASSERT_EQ(0, sup->children[0].child_pid);

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 9: Maximum restart limit */
void test_max_restarts(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	/* Set low restart limit */
	sup->max_restarts = 3;
	sup->restart_window_ms = 60000;

	Bytecode *code = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "crasher", code,
			     RESTART_PERMANENT);

	/* Simulate multiple crashes */
	for (int i = 0; i < 3; i++) {
		Pid pid = sup->children[0].child_pid;
		if (pid == 0) break;
		supervisor_handle_exit(sup, sched, sup_block, pid,
				       EXIT_CRASH, 1, "crash");
	}

	/* Should have hit max restarts */
	ASSERT(supervisor_max_restarts_reached(sup));

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 10: Supervisor shutdown */
void test_supervisor_shutdown(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	/* Add several children */
	Bytecode *code1 = make_loop_bytecode();
	Bytecode *code2 = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "child1", code1,
			     RESTART_PERMANENT);
	supervisor_add_child(sup, sched, sup_block, "child2", code2,
			     RESTART_PERMANENT);

	ASSERT_EQ(2, supervisor_active_count(sup));
	ASSERT(!sup->shutting_down);

	/* Shutdown */
	supervisor_shutdown(sup, sched);

	ASSERT(sup->shutting_down);
	ASSERT_EQ(0, supervisor_active_count(sup));

	bytecode_release(code1);
	bytecode_release(code2);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 11: Child spec with custom restart limits */
void test_child_with_limits(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	Bytecode *code = make_loop_bytecode();
	/* Use add_child_ex with custom restart limits */
	ASSERT(supervisor_add_child_ex(sup, sched, sup_block, "limited", code,
				       RESTART_PERMANENT, 5, 10000));

	/* Verify child was created */
	Pid child_pid = sup->children[0].child_pid;
	Block *child = scheduler_get_block(sched, child_pid);
	ASSERT(child != NULL);
	ASSERT(sup->children[0].max_restarts == 5);

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 12: Supervisor attached to block */
void test_supervisor_block_attachment(void)
{
	Block *block = block_new(1, "sup_block", NULL);

	/* Initially no supervisor */
	ASSERT(block->supervisor == NULL);

	/* Initialize supervisor */
	ASSERT(supervisor_init_block(block, SUP_ONE_FOR_ONE));
	ASSERT(block->supervisor != NULL);
	ASSERT_EQ(SUP_ONE_FOR_ONE, block->supervisor->strategy);

	block_free(block);
}

/* Test 13: Restart window timing */
void test_restart_window(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	sup->max_restarts = 3;
	sup->restart_window_ms = 100;  /* 100ms window */

	Bytecode *code = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "timed", code,
			     RESTART_PERMANENT);

	/* Quick restarts should be counted */
	for (int i = 0; i < 2; i++) {
		Pid pid = sup->children[0].child_pid;
		if (pid == 0) break;
		supervisor_handle_exit(sup, sched, sup_block, pid,
				       EXIT_CRASH, 1, NULL);
	}

	ASSERT_EQ(2, sup->children[0].restart_count);

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 14: Exit reasons */
void test_exit_reasons(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	Bytecode *code = make_loop_bytecode();

	/* Test with TRANSIENT to see different behaviors */
	supervisor_add_child(sup, sched, sup_block, "reason_test", code,
			     RESTART_TRANSIENT);
	Pid pid = sup->children[0].child_pid;

	/* EXIT_KILLED is abnormal - TRANSIENT should restart */
	supervisor_handle_exit(sup, sched, sup_block, pid,
			       EXIT_KILLED, 0, "killed");
	ASSERT(sup->children[0].child_pid != 0);
	ASSERT(sup->children[0].child_pid != pid);  /* New PID */

	/* EXIT_NORMAL should NOT restart transient */
	Pid pid2 = sup->children[0].child_pid;
	supervisor_handle_exit(sup, sched, sup_block, pid2,
			       EXIT_NORMAL, 0, NULL);
	ASSERT_EQ(0, sup->children[0].child_pid);

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

/* Test 15: Link between supervisor and children */
void test_supervisor_child_links(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *sup_block = block_new(1, "supervisor", NULL);
	supervisor_init_block(sup_block, SUP_ONE_FOR_ONE);
	Supervisor *sup = sup_block->supervisor;

	Bytecode *code = make_loop_bytecode();
	supervisor_add_child(sup, sched, sup_block, "linked_child", code,
			     RESTART_PERMANENT);

	Pid child_pid = sup->children[0].child_pid;

	/* Supervisor should be linked to child */
	size_t link_count;
	const Pid *links = block_get_links(sup_block, &link_count);

	bool found_child = false;
	for (size_t i = 0; i < link_count; i++) {
		if (links[i] == child_pid) {
			found_child = true;
			break;
		}
	}
	ASSERT(found_child);

	bytecode_release(code);
	block_free(sup_block);
	scheduler_free(sched);
}

int main(void)
{
	printf("=== E2E Supervision Tests ===\n\n");

	RUN_TEST(test_supervisor_creation);
	RUN_TEST(test_supervisor_strategies);
	RUN_TEST(test_add_child);
	RUN_TEST(test_add_multiple_children);
	RUN_TEST(test_remove_child);
	RUN_TEST(test_restart_permanent);
	RUN_TEST(test_restart_transient);
	RUN_TEST(test_restart_temporary);
	RUN_TEST(test_max_restarts);
	RUN_TEST(test_supervisor_shutdown);
	RUN_TEST(test_child_with_limits);
	RUN_TEST(test_supervisor_block_attachment);
	RUN_TEST(test_restart_window);
	RUN_TEST(test_exit_reasons);
	RUN_TEST(test_supervisor_child_links);

	return TEST_RESULT();
}
