/*
 * Agim - Lexer Fuzzer
 *
 * Fuzz target for the lexer using libFuzzer.
 * Tests lexer resilience against malformed/random input.
 *
 * Build: cmake -DAGIM_ENABLE_FUZZING=ON -DCMAKE_C_COMPILER=clang ..
 * Run:   ./fuzz_lexer corpus/lexer/ -max_len=4096
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lang/lexer.h"
#include "lang/token.h"

/*
 * LLVMFuzzerTestOneInput - Entry point for libFuzzer
 *
 * This function is called by libFuzzer with random input data.
 * We create a null-terminated copy of the input and feed it to the lexer.
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
    char *source = (char *)malloc(size + 1);
    if (!source) {
        return 0;
    }
    memcpy(source, data, size);
    source[size] = '\0';

    /* Create lexer and tokenize entire input */
    Lexer *lexer = lexer_new(source);
    if (lexer) {
        /* Consume all tokens until EOF or error */
        Token token;
        int max_tokens = 100000;  /* Prevent infinite loops */
        int token_count = 0;

        do {
            token = lexer_next(lexer);
            token_count++;
        } while (token.type != TOK_EOF &&
                 token.type != TOK_ERROR &&
                 token_count < max_tokens);

        lexer_free(lexer);
    }

    free(source);
    return 0;
}
