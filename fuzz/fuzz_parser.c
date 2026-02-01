/*
 * Agim - Parser Fuzzer
 *
 * Fuzz target for the parser using libFuzzer.
 * Tests parser resilience against malformed/random input.
 *
 * Build: cmake -DAGIM_ENABLE_FUZZING=ON -DCMAKE_C_COMPILER=clang ..
 * Run:   ./fuzz_parser corpus/parser/ -max_len=4096
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/ast.h"

/*
 * LLVMFuzzerTestOneInput - Entry point for libFuzzer
 *
 * This function is called by libFuzzer with random input data.
 * We parse the input and free the resulting AST (if any).
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

    /* Create lexer and parser */
    Lexer *lexer = lexer_new(source);
    if (lexer) {
        Parser *parser = parser_new(lexer);
        if (parser) {
            /* Parse the program */
            AstNode *ast = parser_parse(parser);

            /* Free the AST if parsing succeeded */
            if (ast) {
                ast_free(ast);
            }

            parser_free(parser);
        }
        lexer_free(lexer);
    }

    free(source);
    return 0;
}
