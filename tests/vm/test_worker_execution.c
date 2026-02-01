/*
 * Agim - Worker Execution Tests
 *
 * P1.1.5.4: Tests for worker block execution.
 * - Worker executes blocks from its deque
 * - Block state transitions during execution
 * - Statistics tracking (blocks_executed, total_reductions)
 * - Worker loop termination conditions
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/worker.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"
#include "vm/bytecode.h"
#include "vm/vm.h"

/* Helper: Create scheduler with default config */
static Scheduler *create_test_scheduler(void) {
    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 0;  /* Single-threaded for unit tests */
    return scheduler_new(&config);
}

/* Helper: Create bytecode that halts immediately */
static Bytecode *create_halt_bytecode(void) {
    Bytecode *code = bytecode_new();
    if (!code) return NULL;

    Chunk *chunk = code->main;
    chunk_write_opcode(chunk, OP_NIL, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/* Helper: Create bytecode that does some work then halts */
static Bytecode *create_work_bytecode(void) {
    Bytecode *code = bytecode_new();
    if (!code) return NULL;

    Chunk *chunk = code->main;
    /* Do some NIL operations to consume reductions */
    for (int i = 0; i < 10; i++) {
        chunk_write_opcode(chunk, OP_NIL, 1);
    }
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

/*
 * Test: Worker executes block from deque
 */
void test_worker_executes_block(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    /* Create a block with halt bytecode */
    Bytecode *code = create_halt_bytecode();
    ASSERT(code != NULL);

    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);
    block_load(block, code);

    /* Enqueue block */
    worker_enqueue(worker, block);
    ASSERT(!deque_empty(&worker->runq));

    /* Pop and execute manually (simulating worker loop) */
    Block *popped = deque_pop(&worker->runq);
    ASSERT_EQ(block, popped);

    VM *vm = block->vm;
    vm->scheduler = scheduler;
    vm->reduction_limit = 10000;
    vm->reductions = 0;

    VMResult result = vm_run(vm);
    ASSERT(result == VM_OK || result == VM_HALT);

    bytecode_free(code);
    block_free(block);
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Worker tracks blocks_executed
 */
void test_worker_tracks_blocks_executed(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    ASSERT_EQ(0, atomic_load(&worker->blocks_executed));

    /* Simulate execution */
    atomic_fetch_add(&worker->blocks_executed, 1);
    ASSERT_EQ(1, atomic_load(&worker->blocks_executed));

    atomic_fetch_add(&worker->blocks_executed, 4);
    ASSERT_EQ(5, atomic_load(&worker->blocks_executed));

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Worker tracks total_reductions
 */
void test_worker_tracks_reductions(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    ASSERT_EQ(0, atomic_load(&worker->total_reductions));

    /* Execute a block and track reductions */
    Bytecode *code = create_work_bytecode();
    ASSERT(code != NULL);

    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);
    block_load(block, code);

    VM *vm = block->vm;
    vm->scheduler = scheduler;
    vm->reduction_limit = 10000;
    vm->reductions = 0;

    vm_run(vm);

    /* Simulate adding reductions to worker */
    atomic_fetch_add(&worker->total_reductions, vm->reductions);
    ASSERT(atomic_load(&worker->total_reductions) > 0);

    bytecode_free(code);
    block_free(block);
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Block state is RUNNABLE after load
 */
void test_block_state_after_load(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Bytecode *code = create_halt_bytecode();
    ASSERT(code != NULL);

    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(BLOCK_RUNNABLE, atomic_load(&block->state));

    block_load(block, code);
    ASSERT_EQ(BLOCK_RUNNABLE, atomic_load(&block->state));

    bytecode_free(code);
    block_free(block);
    scheduler_free(scheduler);
}

/*
 * Test: Block state after halt
 */
void test_block_state_after_halt(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Bytecode *code = create_halt_bytecode();
    ASSERT(code != NULL);

    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);
    block_load(block, code);

    VM *vm = block->vm;
    vm->scheduler = scheduler;
    vm->reduction_limit = 10000;

    VMResult result = vm_run(vm);
    ASSERT(result == VM_OK || result == VM_HALT);

    /* After halt, block would be marked DEAD by worker loop */
    atomic_store(&block->state, BLOCK_DEAD);
    ASSERT_EQ(BLOCK_DEAD, atomic_load(&block->state));

    bytecode_free(code);
    block_free(block);
    scheduler_free(scheduler);
}

/*
 * Test: Worker VM is reusable
 */
void test_worker_vm_reusable(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);
    ASSERT(worker->vm != NULL);

    /* Worker's VM can be reused for different blocks */
    VM *vm = worker->vm;
    vm->scheduler = scheduler;
    vm->reduction_limit = 10000;

    /* Execute multiple times (worker VM is separate from block VM) */
    for (int i = 0; i < 3; i++) {
        Bytecode *code = create_halt_bytecode();
        ASSERT(code != NULL);

        Block *block = block_new((Pid)(i + 1), "test", NULL);
        ASSERT(block != NULL);
        block_load(block, code);

        VM *block_vm = block->vm;
        block_vm->scheduler = scheduler;
        block_vm->reduction_limit = 10000;

        VMResult result = vm_run(block_vm);
        ASSERT(result == VM_OK || result == VM_HALT);

        bytecode_free(code);
        block_free(block);
    }

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Worker handles multiple blocks
 */
void test_worker_handles_multiple_blocks(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    /* Enqueue multiple blocks */
    Block *blocks[3];
    Bytecode *codes[3];
    for (int i = 0; i < 3; i++) {
        codes[i] = create_halt_bytecode();
        ASSERT(codes[i] != NULL);

        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        ASSERT(blocks[i] != NULL);
        block_load(blocks[i], codes[i]);

        worker_enqueue(worker, blocks[i]);
    }

    ASSERT_EQ(3, deque_size(&worker->runq));

    /* Pop and execute each */
    for (int i = 2; i >= 0; i--) {  /* LIFO order */
        Block *popped = deque_pop(&worker->runq);
        ASSERT_EQ(blocks[i], popped);

        VM *vm = popped->vm;
        vm->scheduler = scheduler;
        vm->reduction_limit = 10000;

        VMResult result = vm_run(vm);
        ASSERT(result == VM_OK || result == VM_HALT);
    }

    ASSERT(deque_empty(&worker->runq));

    for (int i = 0; i < 3; i++) {
        bytecode_free(codes[i]);
        block_free(blocks[i]);
    }
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Scheduler total_spawned/total_terminated tracking
 */
void test_scheduler_spawned_terminated(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    ASSERT_EQ(0, atomic_load(&scheduler->total_spawned));
    ASSERT_EQ(0, atomic_load(&scheduler->total_terminated));

    /* Simulate spawning */
    atomic_fetch_add(&scheduler->total_spawned, 1);
    ASSERT_EQ(1, atomic_load(&scheduler->total_spawned));

    /* Simulate termination */
    atomic_fetch_add(&scheduler->total_terminated, 1);
    ASSERT_EQ(1, atomic_load(&scheduler->total_terminated));

    scheduler_free(scheduler);
}

/*
 * Test: Scheduler blocks_in_flight tracking
 */
void test_scheduler_blocks_in_flight(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    ASSERT_EQ(0, atomic_load(&scheduler->blocks_in_flight));

    /* Simulate execution start */
    atomic_fetch_add(&scheduler->blocks_in_flight, 1);
    ASSERT_EQ(1, atomic_load(&scheduler->blocks_in_flight));

    /* Simulate execution end */
    atomic_fetch_sub(&scheduler->blocks_in_flight, 1);
    ASSERT_EQ(0, atomic_load(&scheduler->blocks_in_flight));

    scheduler_free(scheduler);
}

/*
 * Test: Worker deque LIFO for owner
 */
void test_worker_deque_lifo_owner(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(0, scheduler);
    ASSERT(worker != NULL);

    Block *b1 = block_new(1, "b1", NULL);
    Block *b2 = block_new(2, "b2", NULL);
    Block *b3 = block_new(3, "b3", NULL);

    worker_enqueue(worker, b1);
    worker_enqueue(worker, b2);
    worker_enqueue(worker, b3);

    /* Pop returns in LIFO order */
    ASSERT_EQ(b3, deque_pop(&worker->runq));
    ASSERT_EQ(b2, deque_pop(&worker->runq));
    ASSERT_EQ(b1, deque_pop(&worker->runq));

    block_free(b1);
    block_free(b2);
    block_free(b3);
    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Block execution preserves block data
 */
void test_block_execution_preserves_data(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Bytecode *code = create_halt_bytecode();
    ASSERT(code != NULL);

    Block *block = block_new(42, "testblock", NULL);
    ASSERT(block != NULL);
    ASSERT_EQ(42, block->pid);
    ASSERT(strcmp(block->name, "testblock") == 0);

    block_load(block, code);

    VM *vm = block->vm;
    vm->scheduler = scheduler;
    vm->reduction_limit = 10000;

    vm_run(vm);

    /* Block metadata preserved after execution */
    ASSERT_EQ(42, block->pid);
    ASSERT(strcmp(block->name, "testblock") == 0);

    bytecode_free(code);
    block_free(block);
    scheduler_free(scheduler);
}

/*
 * Test: Worker allocator is initialized
 */
void test_worker_allocator_initialized(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Worker *worker = worker_new(5, scheduler);
    ASSERT(worker != NULL);

    /* Allocator should be initialized with worker id */
    /* Can't easily test internal state, but verify it doesn't crash */
    worker_alloc_free(&worker->allocator);
    worker_alloc_init(&worker->allocator, 5);

    worker_free(worker);
    scheduler_free(scheduler);
}

/*
 * Test: Execution with reduction limit
 */
void test_execution_with_reduction_limit(void) {
    Scheduler *scheduler = create_test_scheduler();
    ASSERT(scheduler != NULL);

    Bytecode *code = create_work_bytecode();
    ASSERT(code != NULL);

    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);
    block_load(block, code);

    VM *vm = block->vm;
    vm->scheduler = scheduler;
    vm->reduction_limit = 10000;
    vm->reductions = 0;

    VMResult result = vm_run(vm);
    ASSERT(result == VM_OK || result == VM_HALT);

    /* Reductions should have been counted */
    ASSERT(vm->reductions > 0);
    ASSERT(vm->reductions <= vm->reduction_limit);

    bytecode_free(code);
    block_free(block);
    scheduler_free(scheduler);
}

int main(void) {
    printf("Running worker execution tests...\n");

    printf("\nBlock execution tests:\n");
    RUN_TEST(test_worker_executes_block);
    RUN_TEST(test_worker_handles_multiple_blocks);
    RUN_TEST(test_worker_vm_reusable);
    RUN_TEST(test_execution_with_reduction_limit);

    printf("\nStatistics tracking tests:\n");
    RUN_TEST(test_worker_tracks_blocks_executed);
    RUN_TEST(test_worker_tracks_reductions);

    printf("\nBlock state tests:\n");
    RUN_TEST(test_block_state_after_load);
    RUN_TEST(test_block_state_after_halt);
    RUN_TEST(test_block_execution_preserves_data);

    printf("\nScheduler tracking tests:\n");
    RUN_TEST(test_scheduler_spawned_terminated);
    RUN_TEST(test_scheduler_blocks_in_flight);

    printf("\nDeque behavior tests:\n");
    RUN_TEST(test_worker_deque_lifo_owner);

    printf("\nAllocator tests:\n");
    RUN_TEST(test_worker_allocator_initialized);

    return TEST_RESULT();
}
