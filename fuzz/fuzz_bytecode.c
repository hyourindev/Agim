/*
 * Agim - Bytecode Fuzzer
 *
 * Fuzz target for bytecode loading/deserialization using libFuzzer.
 * Tests bytecode parser resilience against malformed input.
 *
 * Build: cmake -DAGIM_ENABLE_FUZZING=ON -DCMAKE_C_COMPILER=clang ..
 * Run:   ./fuzz_bytecode corpus/bytecode/ -max_len=4096
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vm/bytecode.h"

/*
 * LLVMFuzzerTestOneInput - Entry point for libFuzzer
 *
 * This function is called by libFuzzer with random input data.
 * We try to deserialize the input as bytecode and verify it handles
 * malformed input gracefully.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Skip empty inputs */
    if (size == 0) {
        return 0;
    }

    /* Limit input size to prevent timeouts */
    if (size > 1024 * 1024) {  /* 1MB max */
        return 0;
    }

    /* Try to deserialize the bytecode */
    Bytecode *code = bytecode_deserialize(data, size);

    /* If deserialization succeeded, free the bytecode */
    if (code) {
        bytecode_free(code);
    }

    return 0;
}
