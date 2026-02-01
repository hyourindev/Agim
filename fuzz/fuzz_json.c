/*
 * Agim - JSON Parser Fuzzer
 *
 * Fuzz target for the JSON parser using libFuzzer.
 * Tests JSON parsing resilience against malformed/random input.
 *
 * Build: cmake -DAGIM_ENABLE_FUZZING=ON -DCMAKE_C_COMPILER=clang ..
 * Run:   ./fuzz_json corpus/json/ -max_len=4096
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vm/vm.h"
#include "vm/value.h"
#include "vm/bytecode.h"

/*
 * LLVMFuzzerTestOneInput - Entry point for libFuzzer
 *
 * This function is called by libFuzzer with random input data.
 * We create a JSON string value and parse it using the VM's OP_JSON_PARSE.
 *
 * The fuzzer will detect:
 * - Crashes (segfaults, aborts)
 * - Memory errors (with ASan enabled)
 * - Undefined behavior (with UBSan enabled)
 * - Hangs/infinite loops (with -timeout=N)
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Skip empty inputs */
    if (size == 0) {
        return 0;
    }

    /* Limit input size to prevent timeouts */
    if (size > 65536) {
        return 0;
    }

    /* Create null-terminated copy of input */
    char *json_str = (char *)malloc(size + 1);
    if (!json_str) {
        return 0;
    }
    memcpy(json_str, data, size);
    json_str[size] = '\0';

    /* Create a minimal VM and bytecode to run JSON_PARSE */
    Bytecode *code = bytecode_new();
    if (!code) {
        free(json_str);
        return 0;
    }

    /* Add the JSON string as a constant */
    int str_idx = bytecode_add_constant(code, value_string(json_str));

    Chunk *chunk = code->main;
    if (!chunk) {
        bytecode_free(code);
        free(json_str);
        return 0;
    }

    /* Bytecode: LOAD_CONST str_idx, JSON_PARSE, POP, HALT */
    chunk_write_opcode(chunk, OP_LOAD_CONST, 1);
    chunk_write_operand(chunk, str_idx, 1);
    chunk_write_opcode(chunk, OP_JSON_PARSE, 1);
    chunk_write_opcode(chunk, OP_POP, 1);
    chunk_write_opcode(chunk, OP_HALT, 1);

    /* Create VM and run */
    VM *vm = vm_new();
    if (vm) {
        vm_load_bytecode(vm, code);

        /* Run with reduction limit to prevent infinite loops */
        vm->reduction_limit = 10000;
        VMResult result = vm_run(vm);

        /* Clean up any result on the stack */
        while (vm->stack_top > vm->stack) {
            Value *v = vm_pop(vm);
            if (v) value_free(v);
        }

        vm_free(vm);
    }

    bytecode_free(code);
    free(json_str);
    return 0;
}
