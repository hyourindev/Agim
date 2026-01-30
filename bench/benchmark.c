/*
 * Agim VM Benchmark
 *
 * Exercises all major VM components:
 * - Arithmetic operations
 * - Control flow (loops)
 * - Function calls
 * - Data structures
 * - Message passing
 * - Primitives
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vm/value.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "vm/gc.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "vm/primitives.h"

/*============================================================================
 * Timing Utilities
 *============================================================================*/

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

#define BENCH_START() double _bench_start = get_time_ms()
#define BENCH_END(name, iterations) do { \
    double _bench_end = get_time_ms(); \
    double _bench_time = _bench_end - _bench_start; \
    double _ops_per_sec = (iterations) / (_bench_time / 1000.0); \
    printf("  %-30s %8.2f ms  %12.0f ops/sec\n", name, _bench_time, _ops_per_sec); \
} while(0)

/*============================================================================
 * Benchmark: Arithmetic Loop
 * Tests: OP_CONST, OP_ADD, OP_SUB, OP_LT, OP_JUMP_IF, OP_LOOP
 *============================================================================*/

static Bytecode *make_arithmetic_loop(int iterations) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Constants */
    size_t c_iter = chunk_add_constant(chunk, value_int(iterations));
    size_t c_one = chunk_add_constant(chunk, value_int(1));
    size_t c_zero = chunk_add_constant(chunk, value_int(0));

    /*
     * Simple counter loop:
     * i = iterations
     * while (i > 0) { i = i - 1 }
     * push i (should be 0)
     *
     * Stack just holds [i]
     */

    /* i = iterations */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, (c_iter >> 8) & 0xFF, 1);
    chunk_write_byte(chunk, c_iter & 0xFF, 1);

    /* loop_start: */
    size_t loop_start = chunk->code_size;

    /* if i <= 0 goto end */
    chunk_write_opcode(chunk, OP_DUP, 2);  /* [i, i] */
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, (c_zero >> 8) & 0xFF, 2);
    chunk_write_byte(chunk, c_zero & 0xFF, 2);  /* [i, i, 0] */
    chunk_write_opcode(chunk, OP_LE, 2);  /* [i, i<=0] */
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);  /* peeks condition */
    chunk_write_opcode(chunk, OP_POP, 2);  /* pop condition (false, since we didn't jump) */

    /* i = i - 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, (c_one >> 8) & 0xFF, 3);
    chunk_write_byte(chunk, c_one & 0xFF, 3);  /* [i, 1] */
    chunk_write_opcode(chunk, OP_SUB, 3);  /* [i-1] */

    /* goto loop_start */
    chunk_write_opcode(chunk, OP_LOOP, 4);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, offset & 0xFF, 4);

    /* end: stack is [i, true] after jump */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);  /* pop condition */
    /* i is on stack (should be 0) */
    chunk_write_opcode(chunk, OP_HALT, 5);

    return code;
}

static void bench_arithmetic(int iterations) {
    Bytecode *code = make_arithmetic_loop(iterations);
    VM *vm = vm_new();
    vm->reduction_limit = iterations * 20;  /* Enough for loop iterations */
    vm_load(vm, code);

    BENCH_START();
    VMResult result = vm_run(vm);
    BENCH_END("Arithmetic loop", iterations);

    if (result != VM_HALT && result != VM_OK) {
        printf("    ERROR: VM returned %d\n", result);
    } else {
        Value *sum = vm_peek(vm, 0);
        if (sum && value_is_int(sum)) {
            printf("    Result: sum = %ld\n", sum->as.integer);
        }
    }

    vm_free(vm);
    bytecode_free(code);
}

/*============================================================================
 * Benchmark: Function Calls
 * Tests: OP_CALL, OP_RETURN, call stack management
 *============================================================================*/

/* Note: Function call benchmarks are complex without a compiler.
 * The recursive fib implementation is omitted for now - would need
 * proper compiler support to set up call frames correctly. */

/*============================================================================
 * Benchmark: Data Structures
 * Tests: OP_ARRAY_NEW, OP_ARRAY_PUSH, OP_MAP_NEW, OP_MAP_SET, OP_MAP_GET
 *============================================================================*/

static Bytecode *make_array_benchmark(int size) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Create array */
    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);

    /* Push values */
    for (int i = 0; i < size; i++) {
        size_t c = chunk_add_constant(chunk, value_int(i));
        chunk_write_opcode(chunk, OP_CONST, 2);
        chunk_write_byte(chunk, (c >> 8) & 0xFF, 2);
        chunk_write_byte(chunk, c & 0xFF, 2);
        chunk_write_opcode(chunk, OP_ARRAY_PUSH, 2);
    }

    chunk_write_opcode(chunk, OP_HALT, 3);

    return code;
}

static void bench_array(int size) {
    Bytecode *code = make_array_benchmark(size);
    VM *vm = vm_new();
    vm->reduction_limit = size * 10;  /* Enough for all operations */
    vm_load(vm, code);

    BENCH_START();
    VMResult result = vm_run(vm);
    BENCH_END("Array push", size);

    if (result != VM_HALT && result != VM_OK) {
        printf("    ERROR: VM returned %d\n", result);
    } else {
        Value *arr = vm_peek(vm, 0);
        if (arr && value_is_array(arr)) {
            printf("    Result: array length = %zu\n", array_length(arr));
        }
    }

    vm_free(vm);
    bytecode_free(code);
}

static Bytecode *make_map_benchmark(int size) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    /* Create map */
    chunk_write_opcode(chunk, OP_MAP_NEW, 1);

    /* Set key-value pairs */
    for (int i = 0; i < size; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%d", i);

        size_t c_key = chunk_add_constant(chunk, value_string(key));
        size_t c_val = chunk_add_constant(chunk, value_int(i));

        chunk_write_opcode(chunk, OP_CONST, 2);
        chunk_write_byte(chunk, (c_key >> 8) & 0xFF, 2);
        chunk_write_byte(chunk, c_key & 0xFF, 2);

        chunk_write_opcode(chunk, OP_CONST, 2);
        chunk_write_byte(chunk, (c_val >> 8) & 0xFF, 2);
        chunk_write_byte(chunk, c_val & 0xFF, 2);

        chunk_write_opcode(chunk, OP_MAP_SET, 2);
    }

    chunk_write_opcode(chunk, OP_HALT, 3);

    return code;
}

static void bench_map(int size) {
    Bytecode *code = make_map_benchmark(size);
    VM *vm = vm_new();
    vm->reduction_limit = size * 10;  /* Enough for all operations */
    vm_load(vm, code);

    BENCH_START();
    VMResult result = vm_run(vm);
    BENCH_END("Map set", size);

    if (result != VM_HALT && result != VM_OK) {
        printf("    ERROR: VM returned %d\n", result);
    } else {
        Value *map = vm_peek(vm, 0);
        if (map && value_is_map(map)) {
            printf("    Result: map size = %zu\n", map_size(map));
        }
    }

    vm_free(vm);
    bytecode_free(code);
}

/*============================================================================
 * Benchmark: Scheduler / Multiple Blocks
 *============================================================================*/

static Bytecode *make_simple_block(int64_t value) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c = chunk_add_constant(chunk, value_int(value));
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, (c >> 8) & 0xFF, 1);
    chunk_write_byte(chunk, c & 0xFF, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    return code;
}

static void bench_scheduler(int num_blocks) {
    Scheduler *sched = scheduler_new(NULL);
    Bytecode **codes = malloc(sizeof(Bytecode*) * num_blocks);

    for (int i = 0; i < num_blocks; i++) {
        codes[i] = make_simple_block(i);
        scheduler_spawn(sched, codes[i], NULL);
    }

    BENCH_START();
    scheduler_run(sched);
    BENCH_END("Block scheduling", num_blocks);

    SchedulerStats stats = scheduler_stats(sched);
    printf("    Blocks completed: %zu, Context switches: %zu\n",
           stats.blocks_dead, stats.context_switches);

    scheduler_free(sched);
    for (int i = 0; i < num_blocks; i++) {
        bytecode_free(codes[i]);
    }
    free(codes);
}

/*============================================================================
 * Benchmark: Message Passing
 *============================================================================*/

static Bytecode *make_sender(Pid target, int count) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_target = chunk_add_constant(chunk, value_pid(target));
    size_t c_count = chunk_add_constant(chunk, value_int(count));
    size_t c_one = chunk_add_constant(chunk, value_int(1));
    size_t c_zero = chunk_add_constant(chunk, value_int(0));

    /* i = count */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, (c_count >> 8) & 0xFF, 1);
    chunk_write_byte(chunk, c_count & 0xFF, 1);

    /* loop: */
    size_t loop_start = chunk->code_size;

    /* if i <= 0 goto end */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, (c_zero >> 8) & 0xFF, 2);
    chunk_write_byte(chunk, c_zero & 0xFF, 2);
    chunk_write_opcode(chunk, OP_LE, 2);
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* send(target, i) */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, (c_target >> 8) & 0xFF, 3);
    chunk_write_byte(chunk, c_target & 0xFF, 3);
    chunk_write_opcode(chunk, OP_DUP, 3);  /* dup i for send */
    /* Stack: [i, target, i] - wrong order, need [i, target, msg] */
    /* Let's simplify: just send a constant */
    chunk_write_opcode(chunk, OP_POP, 3);  /* pop i copy */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, (c_one >> 8) & 0xFF, 3);
    chunk_write_byte(chunk, c_one & 0xFF, 3);
    chunk_write_opcode(chunk, OP_SEND, 3);
    chunk_write_opcode(chunk, OP_POP, 3);  /* pop send result */

    /* i = i - 1 */
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, (c_one >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, c_one & 0xFF, 4);
    chunk_write_opcode(chunk, OP_SUB, 4);

    /* goto loop */
    chunk_write_opcode(chunk, OP_LOOP, 5);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 5);
    chunk_write_byte(chunk, offset & 0xFF, 5);

    /* end: */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 6);  /* pop condition */
    chunk_write_opcode(chunk, OP_POP, 6);  /* pop i */
    chunk_write_opcode(chunk, OP_HALT, 6);

    return code;
}

static Bytecode *make_receiver(int count) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_count = chunk_add_constant(chunk, value_int(count));
    size_t c_one = chunk_add_constant(chunk, value_int(1));
    size_t c_zero = chunk_add_constant(chunk, value_int(0));

    /* i = count */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, (c_count >> 8) & 0xFF, 1);
    chunk_write_byte(chunk, c_count & 0xFF, 1);

    /* loop: */
    size_t loop_start = chunk->code_size;

    /* if i <= 0 goto end */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, (c_zero >> 8) & 0xFF, 2);
    chunk_write_byte(chunk, c_zero & 0xFF, 2);
    chunk_write_opcode(chunk, OP_LE, 2);
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* receive() */
    chunk_write_opcode(chunk, OP_RECEIVE, 3);
    chunk_write_opcode(chunk, OP_POP, 3);  /* discard message */

    /* i = i - 1 */
    chunk_write_opcode(chunk, OP_CONST, 4);
    chunk_write_byte(chunk, (c_one >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, c_one & 0xFF, 4);
    chunk_write_opcode(chunk, OP_SUB, 4);

    /* goto loop */
    chunk_write_opcode(chunk, OP_LOOP, 5);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 5);
    chunk_write_byte(chunk, offset & 0xFF, 5);

    /* end: */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 6);
    chunk_write_opcode(chunk, OP_POP, 6);
    chunk_write_opcode(chunk, OP_HALT, 6);

    return code;
}

static void bench_message_passing(int num_messages) {
    Scheduler *sched = scheduler_new(NULL);

    /* Create limits with larger mailbox for the benchmark */
    BlockLimits limits = block_limits_default();
    limits.max_mailbox_size = num_messages + 100;  /* Ensure mailbox can hold all messages */

    /* Create receiver first to get its PID */
    Bytecode *recv_code = make_receiver(num_messages);
    Pid recv_pid = scheduler_spawn_ex(sched, recv_code, "receiver",
                                      CAP_SEND | CAP_RECEIVE, &limits);

    /* Create sender with receiver's PID */
    Bytecode *send_code = make_sender(recv_pid, num_messages);
    scheduler_spawn_ex(sched, send_code, "sender",
                       CAP_SEND | CAP_RECEIVE, &limits);

    BENCH_START();
    scheduler_run(sched);
    BENCH_END("Message passing", num_messages);

    SchedulerStats stats = scheduler_stats(sched);
    printf("    Blocks: %zu, Context switches: %zu\n",
           stats.blocks_dead, stats.context_switches);

    scheduler_free(sched);
    bytecode_free(recv_code);
    bytecode_free(send_code);
}

/*============================================================================
 * Benchmark: Primitives
 *============================================================================*/

static void bench_primitives_set(int iterations) {
    PrimitivesRuntime *rt = primitives_new();

    BENCH_START();
    for (int i = 0; i < iterations; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%d", i % 100);
        Value *v = value_int(i);
        primitives_memory_set(rt, key, v);
        value_free(v);
    }
    BENCH_END("Memory set", iterations);

    primitives_free(rt);
}

static void bench_primitives_get(int iterations) {
    PrimitivesRuntime *rt = primitives_new();

    /* Pre-populate */
    for (int i = 0; i < 100; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%d", i);
        Value *v = value_int(i);
        primitives_memory_set(rt, key, v);
        value_free(v);
    }

    BENCH_START();
    for (int i = 0; i < iterations; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%d", i % 100);
        Value *v = primitives_memory_get(rt, key);
        if (v) value_free(v);
    }
    BENCH_END("Memory get", iterations);

    primitives_free(rt);
}

/*============================================================================
 * Benchmark: GC
 *============================================================================*/

static void bench_gc(int allocations) {
    GCConfig config = gc_config_default();
    config.initial_heap_size = 1024;  /* Small heap to trigger GC */
    config.max_heap_size = 1024 * 1024;

    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    BENCH_START();
    for (int i = 0; i < allocations; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        if (v) {
            v->as.integer = i;
        }
        /* Trigger GC periodically */
        if (i % 1000 == 0) {
            gc_collect(heap, vm);
        }
    }
    BENCH_END("Allocation + GC", allocations);

    HeapStats stats = heap_stats(heap);
    printf("    GC runs: %zu, Bytes allocated: %zu\n",
           stats.gc_runs, stats.bytes_allocated);

    heap_free(heap);
    vm_free(vm);
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char **argv) {
    int scale = 1;
    if (argc > 1) {
        scale = atoi(argv[1]);
        if (scale < 1) scale = 1;
        if (scale > 100) scale = 100;
    }

    printf("=================================================\n");
    printf("Agim VM Benchmark (scale: %dx)\n", scale);
    printf("=================================================\n\n");

    printf("Arithmetic Operations:\n");
    bench_arithmetic(100000 * scale);
    printf("\n");

    printf("Data Structures:\n");
    bench_array(10000 * scale);
    bench_map(1000 * scale);
    printf("\n");

    printf("Scheduler:\n");
    bench_scheduler(100 * scale);
    printf("\n");

    printf("Message Passing:\n");
    bench_message_passing(1000 * scale);
    printf("\n");

    printf("Primitives:\n");
    bench_primitives_set(10000 * scale);
    bench_primitives_get(10000 * scale);
    printf("\n");

    printf("Garbage Collection:\n");
    bench_gc(10000 * scale);
    printf("\n");

    printf("=================================================\n");
    printf("Benchmark complete.\n");
    printf("=================================================\n");

    return 0;
}
