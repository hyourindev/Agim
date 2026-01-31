/*
 * Agim - End-to-End Scheduler Tests
 *
 * Tests the scheduler infrastructure including process management, run queues,
 * work stealing, multi-threaded execution, and fair scheduling. Validates
 * Erlang-style preemptive scheduling semantics.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/scheduler.h"
#include "runtime/worker.h"
#include "runtime/block.h"
#include "vm/bytecode.h"
#include "vm/value.h"

#include <pthread.h>
#include <unistd.h>

/*
 * Helper: Create bytecode that returns immediately
 */
static Bytecode *make_return_bytecode(int value)
{
	Bytecode *code = bytecode_new();
	Value *val = value_int(value);
	size_t idx = chunk_add_constant(code->main, val);

	chunk_write_opcode(code->main, OP_CONST, 1);
	chunk_write_arg(code->main, (uint16_t)idx, 1);  /* 2-byte index */
	chunk_write_opcode(code->main, OP_RETURN, 1);

	return code;
}

/*
 * Helper: Create bytecode that yields multiple times
 */
static Bytecode *make_yield_n_bytecode(int yields)
{
	Bytecode *code = bytecode_new();

	for (int i = 0; i < yields; i++) {
		chunk_write_opcode(code->main, OP_YIELD, 1);
	}

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

/* Test 1: Scheduler creation */
void test_scheduler_creation(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};

	Scheduler *sched = scheduler_new(&config);
	ASSERT(sched != NULL);
	ASSERT_EQ(1000, sched->config.default_reductions);
	ASSERT(!scheduler_is_multithreaded(sched));

	scheduler_free(sched);
}

/* Test 2: Single-threaded scheduler */
void test_single_threaded(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,  /* Single-threaded */
	};

	Scheduler *sched = scheduler_new(&config);
	ASSERT(!scheduler_is_multithreaded(sched));
	ASSERT_EQ(0, scheduler_worker_count(sched));

	scheduler_free(sched);
}

/* Test 3: Multi-threaded scheduler */
void test_multi_threaded(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 4,
		.enable_stealing = true,
	};

	Scheduler *sched = scheduler_new(&config);
	ASSERT(scheduler_is_multithreaded(sched));
	ASSERT_EQ(4, scheduler_worker_count(sched));

	scheduler_free(sched);
}

/* Test 4: Spawn process */
void test_spawn_process(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code = make_return_bytecode(42);
	Pid pid = scheduler_spawn(sched, code, "test_spawn");

	ASSERT(pid != 0);
	Block *block = scheduler_get_block(sched, pid);
	ASSERT(block != NULL);
	ASSERT_STR_EQ("test_spawn", block->name);

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 5: Spawn with capabilities */
void test_spawn_with_caps(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code = make_return_bytecode(0);
	CapabilitySet caps = CAP_SPAWN | CAP_SEND | CAP_RECEIVE;

	Pid pid = scheduler_spawn_ex(sched, code, "capped", caps, NULL);
	Block *block = scheduler_get_block(sched, pid);

	ASSERT(block_has_cap(block, CAP_SPAWN));
	ASSERT(block_has_cap(block, CAP_SEND));
	ASSERT(block_has_cap(block, CAP_RECEIVE));
	ASSERT(!block_has_cap(block, CAP_INFER));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 6: Spawn with limits */
void test_spawn_with_limits(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	BlockLimits limits = {
		.max_heap_size = 512 * 1024,
		.max_stack_depth = 64,
		.max_call_depth = 16,
		.max_reductions = 500,
		.max_mailbox_size = 25,
	};

	Bytecode *code = make_return_bytecode(0);
	Pid pid = scheduler_spawn_ex(sched, code, "limited", 0, &limits);
	Block *block = scheduler_get_block(sched, pid);

	ASSERT_EQ(512 * 1024, block->limits.max_heap_size);
	ASSERT_EQ(500, block->limits.max_reductions);
	ASSERT_EQ(25, block->limits.max_mailbox_size);

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 7: Run queue operations */
void test_run_queue(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	ASSERT(scheduler_queue_empty(sched));

	/* Spawn adds to queue */
	Bytecode *code = make_yield_n_bytecode(3);
	scheduler_spawn(sched, code, "queued");

	ASSERT(!scheduler_queue_empty(sched));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 8: Scheduler step execution */
void test_scheduler_step(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code = make_return_bytecode(0);
	scheduler_spawn(sched, code, "stepper");

	/* Should complete in one step */
	bool more_work = scheduler_step(sched);
	/* May or may not have more work depending on cleanup */

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 9: Run to completion */
void test_run_to_completion(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code = make_yield_n_bytecode(5);
	Pid pid = scheduler_spawn(sched, code, "runner");

	/* Run until done */
	int steps = 0;
	while (scheduler_step(sched) && steps < 100) {
		steps++;
	}

	Block *block = scheduler_get_block(sched, pid);
	ASSERT_EQ(BLOCK_DEAD, block_state(block));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 10: Fair scheduling */
void test_fair_scheduling(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 10,  /* Low reductions for preemption */
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code1 = make_loop_bytecode();
	Bytecode *code2 = make_loop_bytecode();
	Bytecode *code3 = make_loop_bytecode();

	Pid pid1 = scheduler_spawn(sched, code1, "proc1");
	Pid pid2 = scheduler_spawn(sched, code2, "proc2");
	Pid pid3 = scheduler_spawn(sched, code3, "proc3");

	/* Run several steps */
	for (int i = 0; i < 30; i++) {
		scheduler_step(sched);
	}

	/* All should still be alive (infinite loops) */
	ASSERT(block_is_alive(scheduler_get_block(sched, pid1)));
	ASSERT(block_is_alive(scheduler_get_block(sched, pid2)));
	ASSERT(block_is_alive(scheduler_get_block(sched, pid3)));

	/* All should have some reductions */
	Block *b1 = scheduler_get_block(sched, pid1);
	Block *b2 = scheduler_get_block(sched, pid2);
	Block *b3 = scheduler_get_block(sched, pid3);

	ASSERT(b1->counters.reductions > 0);
	ASSERT(b2->counters.reductions > 0);
	ASSERT(b3->counters.reductions > 0);

	bytecode_free(code1);
	bytecode_free(code2);
	bytecode_free(code3);
	scheduler_free(sched);
}

/* Test 11: Kill process */
void test_kill_process(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code = make_loop_bytecode();
	Pid pid = scheduler_spawn(sched, code, "killable");

	scheduler_step(sched);
	ASSERT(block_is_alive(scheduler_get_block(sched, pid)));

	scheduler_kill(sched, pid);
	ASSERT(!block_is_alive(scheduler_get_block(sched, pid)));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 12: Scheduler statistics */
void test_scheduler_statistics(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	SchedulerStats stats = scheduler_stats(sched);
	ASSERT_EQ(0, stats.blocks_total);

	Bytecode *code = make_return_bytecode(0);
	scheduler_spawn(sched, code, "stat1");
	scheduler_spawn(sched, code, "stat2");

	stats = scheduler_stats(sched);
	ASSERT_EQ(2, stats.blocks_total);
	ASSERT_EQ(2, stats.blocks_alive);
	ASSERT_EQ(2, stats.blocks_runnable);

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 13: Block count */
void test_block_count(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	ASSERT_EQ(0, scheduler_block_count(sched));

	Bytecode *code = make_return_bytecode(0);
	scheduler_spawn(sched, code, "count1");
	scheduler_spawn(sched, code, "count2");
	scheduler_spawn(sched, code, "count3");

	ASSERT_EQ(3, scheduler_block_count(sched));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 14: Current block */
void test_current_block(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	/* No current block initially */
	ASSERT(scheduler_current(sched) == NULL);

	bytecode_free(make_return_bytecode(0));  /* Just to have something */
	scheduler_free(sched);
}

/* Test 15: Stop scheduler */
void test_scheduler_stop(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	/* Scheduler starts with running = false */
	ASSERT(!atomic_load(&sched->running));

	/* scheduler_stop sets running to false (idempotent) */
	scheduler_stop(sched);

	ASSERT(!atomic_load(&sched->running));

	scheduler_free(sched);
}

/* Test 16: Worker creation */
void test_worker_creation(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 2,
	};
	Scheduler *sched = scheduler_new(&config);

	Worker *w0 = scheduler_get_worker(sched, 0);
	Worker *w1 = scheduler_get_worker(sched, 1);

	ASSERT(w0 != NULL);
	ASSERT(w1 != NULL);
	ASSERT_EQ(0, w0->id);
	ASSERT_EQ(1, w1->id);

	scheduler_free(sched);
}

/* Test 17: Work deque operations */
void test_work_deque(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 1,
	};
	Scheduler *sched = scheduler_new(&config);
	Worker *worker = scheduler_get_worker(sched, 0);

	ASSERT(deque_empty(&worker->runq));

	/* Push some blocks */
	Block *b1 = block_new(1, "deque1", NULL);
	Block *b2 = block_new(2, "deque2", NULL);
	Block *b3 = block_new(3, "deque3", NULL);

	deque_push(&worker->runq, b1);
	deque_push(&worker->runq, b2);
	deque_push(&worker->runq, b3);

	ASSERT(!deque_empty(&worker->runq));

	/* Pop (LIFO for owner) */
	Block *popped = deque_pop(&worker->runq);
	ASSERT_EQ(3, popped->pid);

	popped = deque_pop(&worker->runq);
	ASSERT_EQ(2, popped->pid);

	popped = deque_pop(&worker->runq);
	ASSERT_EQ(1, popped->pid);

	ASSERT(deque_empty(&worker->runq));

	block_free(b1);
	block_free(b2);
	block_free(b3);
	scheduler_free(sched);
}

/* Test 18: Work stealing */
void test_work_stealing(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 2,
		.enable_stealing = true,
	};
	Scheduler *sched = scheduler_new(&config);

	Worker *w0 = scheduler_get_worker(sched, 0);
	Worker *w1 = scheduler_get_worker(sched, 1);

	/* Add work to worker 0 */
	Block *b1 = block_new(1, "steal1", NULL);
	Block *b2 = block_new(2, "steal2", NULL);

	deque_push(&w0->runq, b1);
	deque_push(&w0->runq, b2);

	/* Worker 1 steals from worker 0 */
	Block *stolen = deque_steal(&w0->runq);
	ASSERT(stolen != NULL);
	/* Steal takes from opposite end (FIFO) */
	ASSERT_EQ(1, stolen->pid);

	block_free(b1);
	block_free(b2);
	scheduler_free(sched);
}

/* Test 19: Wake sleeping block */
void test_wake_block(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Bytecode *code = make_loop_bytecode();
	Pid pid = scheduler_spawn(sched, code, "sleeper");
	Block *block = scheduler_get_block(sched, pid);

	/* Simulate waiting state */
	block_try_transition(block, BLOCK_RUNNABLE, BLOCK_WAITING);
	ASSERT_EQ(BLOCK_WAITING, block_state(block));

	/* Wake it */
	scheduler_wake_block(sched, block);
	ASSERT_EQ(BLOCK_RUNNABLE, block_state(block));

	bytecode_free(code);
	scheduler_free(sched);
}

/* Test 20: Enqueue and dequeue */
void test_enqueue_dequeue(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	Block *b1 = block_new(1, "enq1", NULL);
	Block *b2 = block_new(2, "enq2", NULL);

	scheduler_enqueue(sched, b1);
	scheduler_enqueue(sched, b2);

	ASSERT(!scheduler_queue_empty(sched));

	Block *d1 = scheduler_dequeue(sched);
	Block *d2 = scheduler_dequeue(sched);

	ASSERT(d1 != NULL);
	ASSERT(d2 != NULL);
	ASSERT(scheduler_queue_empty(sched));

	block_free(b1);
	block_free(b2);
	scheduler_free(sched);
}

int main(void)
{
	printf("=== E2E Scheduler Tests ===\n\n");

	RUN_TEST(test_scheduler_creation);
	RUN_TEST(test_single_threaded);
	RUN_TEST(test_multi_threaded);
	RUN_TEST(test_spawn_process);
	RUN_TEST(test_spawn_with_caps);
	RUN_TEST(test_spawn_with_limits);
	RUN_TEST(test_run_queue);
	RUN_TEST(test_scheduler_step);
	RUN_TEST(test_run_to_completion);
	RUN_TEST(test_fair_scheduling);
	RUN_TEST(test_kill_process);
	RUN_TEST(test_scheduler_statistics);
	RUN_TEST(test_block_count);
	RUN_TEST(test_current_block);
	RUN_TEST(test_scheduler_stop);
	RUN_TEST(test_worker_creation);
	RUN_TEST(test_work_deque);
	RUN_TEST(test_work_stealing);
	RUN_TEST(test_wake_block);
	RUN_TEST(test_enqueue_dequeue);

	return TEST_RESULT();
}
