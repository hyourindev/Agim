/*
 * Agim Register VM Benchmark
 *
 * Compares stack-based VM vs register-based VM performance.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "vm/value.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "vm/regvm.h"

/*============================================================================
 * Timing Utilities
 *============================================================================*/

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

#define BENCH_ITERATIONS 1000000

/*============================================================================
 * Stack VM Benchmark: Arithmetic Loop
 *============================================================================*/

static Bytecode *make_stack_arithmetic_loop(int iterations) {
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_iter = chunk_add_constant(chunk, value_int(iterations));
    size_t c_one = chunk_add_constant(chunk, value_int(1));
    size_t c_zero = chunk_add_constant(chunk, value_int(0));

    /* i = iterations */
    chunk_write_opcode(chunk, OP_CONST, 1);
    chunk_write_byte(chunk, (c_iter >> 8) & 0xFF, 1);
    chunk_write_byte(chunk, c_iter & 0xFF, 1);

    size_t loop_start = chunk->code_size;

    /* if i <= 0 goto end */
    chunk_write_opcode(chunk, OP_DUP, 2);
    chunk_write_opcode(chunk, OP_CONST, 2);
    chunk_write_byte(chunk, (c_zero >> 8) & 0xFF, 2);
    chunk_write_byte(chunk, c_zero & 0xFF, 2);
    chunk_write_opcode(chunk, OP_LE, 2);
    size_t exit_jump = chunk_write_jump(chunk, OP_JUMP_IF, 2);
    chunk_write_opcode(chunk, OP_POP, 2);

    /* i = i - 1 */
    chunk_write_opcode(chunk, OP_CONST, 3);
    chunk_write_byte(chunk, (c_one >> 8) & 0xFF, 3);
    chunk_write_byte(chunk, c_one & 0xFF, 3);
    chunk_write_opcode(chunk, OP_SUB, 3);

    /* goto loop_start */
    chunk_write_opcode(chunk, OP_LOOP, 4);
    size_t offset = chunk->code_size - loop_start + 2;
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, 4);
    chunk_write_byte(chunk, offset & 0xFF, 4);

    /* end: */
    chunk_patch_jump(chunk, exit_jump);
    chunk_write_opcode(chunk, OP_POP, 5);
    chunk_write_opcode(chunk, OP_HALT, 6);

    return code;
}

static double bench_stack_vm(int iterations) {
    Bytecode *code = make_stack_arithmetic_loop(iterations);
    VM *vm = vm_new();
    vm->reduction_limit = iterations * 20;
    vm_load(vm, code);

    double start = get_time_ms();
    vm_run(vm);
    double end = get_time_ms();

    vm_free(vm);
    bytecode_free(code);

    return end - start;
}

/*============================================================================
 * Register VM Benchmark: Count-up loop (simpler)
 *============================================================================*/

static RegChunk *make_reg_countup_loop(int iterations) {
    RegChunk *chunk = regchunk_new();

    /* r0 = 0 (counter)
     * r1 = 1 (increment)
     * r2 = iterations (limit)
     * r3 = condition
     *
     * loop:
     *   r0 = r0 + r1
     *   r3 = r0 < r2
     *   if r3 goto loop
     * halt
     */

    /* r0 = 0 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 0), 1);

    /* r1 = 1 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 1), 2);

    /* r2 = iterations */
    size_t idx = regchunk_add_constant(chunk, value_int(iterations));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 2, (uint16_t)idx), 3);

    /* loop_start at offset 3: */
    /* r0 = r0 + r1 */
    regchunk_write(chunk, reg_instr(ROP_ADD, 0, 0, 1), 4);

    /* r3 = r0 < r2 */
    regchunk_write(chunk, reg_instr(ROP_LT, 3, 0, 2), 5);

    /* if r3 then goto loop_start (offset 3) */
    /* Current offset is 5, after this instruction IP will be 6 */
    /* We want to jump back to offset 3, so relative = 3 - 6 = -3 */
    regchunk_write(chunk, reg_instr_cond_jump(ROP_JMP_IF, 3, -3), 6);

    /* HALT */
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 7);

    chunk->num_regs = 4;
    return chunk;
}

static double bench_reg_vm(int iterations) {
    RegChunk *chunk = make_reg_countup_loop(iterations);
    RegVM *vm = regvm_new();

    double start = get_time_ms();
    regvm_run(vm, chunk);
    double end = get_time_ms();

    /* Verify result */
    int64_t result = nanbox_as_int(vm->frames[0].regs[0]);
    if (result != iterations) {
        printf("    ERROR: Expected %d, got %ld\n", iterations, result);
    }

    regvm_free(vm);
    regchunk_free(chunk);

    return end - start;
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

    int iterations = BENCH_ITERATIONS * scale;

    printf("=================================================\n");
    printf("Agim VM Comparison Benchmark (scale: %dx)\n", scale);
    printf("Iterations: %d\n", iterations);
    printf("=================================================\n\n");

    /* Quick test with small count */
    printf("Testing with 10 iterations...\n");
    RegChunk *test_chunk = make_reg_countup_loop(10);
    RegVM *test_vm = regvm_new();
    regvm_run(test_vm, test_chunk);
    int64_t test_result = nanbox_as_int(test_vm->frames[0].regs[0]);
    printf("  Result: %ld (expected 10)\n", test_result);
    regvm_free(test_vm);
    regchunk_free(test_chunk);

    if (test_result != 10) {
        printf("  ERROR: Test failed!\n");
        return 1;
    }
    printf("  OK\n\n");

    /* Stack VM */
    printf("Stack-Based VM:\n");
    double stack_time = bench_stack_vm(iterations);
    double stack_ops = iterations / (stack_time / 1000.0);
    printf("  Countdown loop:     %8.2f ms  %12.0f ops/sec\n", stack_time, stack_ops);

    printf("\n");

    /* Register VM */
    printf("Register-Based VM:\n");
    double reg_time = bench_reg_vm(iterations);
    double reg_ops = iterations / (reg_time / 1000.0);
    printf("  Count-up loop:      %8.2f ms  %12.0f ops/sec\n", reg_time, reg_ops);

    printf("\n");

    /* Comparison */
    printf("=================================================\n");
    printf("Performance Comparison:\n");
    printf("=================================================\n");
    double speedup = stack_time / reg_time;
    printf("  Register VM vs Stack VM: %.2fx %s\n",
           speedup > 1 ? speedup : 1.0/speedup,
           speedup > 1 ? "faster" : "slower");
    printf("\n");

    printf("=================================================\n");
    printf("Benchmark complete.\n");
    printf("=================================================\n");

    return 0;
}
