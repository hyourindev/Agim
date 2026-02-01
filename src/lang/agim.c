/*
 * Agim - Public API Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/agim.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/typechecker.h"
#include "lang/compiler.h"
#include "vm/vm.h"
#include "util/alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global flag for strict type checking */
static bool g_strict_types = false;

void agim_set_strict_types(bool strict) {
    g_strict_types = strict;
}

Bytecode *agim_compile(const char *source, const char **error) {
    if (error) *error = NULL;

    /* Lex */
    Lexer *lexer = lexer_new(source);

    /* Parse */
    Parser *parser = parser_new(lexer);
    AstNode *ast = parser_parse(parser);

    if (!ast) {
        if (error && parser_error(parser)) {
            size_t len = strlen(parser_error(parser));
            *error = agim_alloc(len + 1);
            memcpy((char *)*error, parser_error(parser), len + 1);
        }
        parser_free(parser);
        lexer_free(lexer);
        return NULL;
    }

    /* Type check (if strict mode enabled) */
    if (g_strict_types) {
        TypeChecker *tc = typechecker_new();
        if (!typechecker_check(tc, ast)) {
            if (error && typechecker_error(tc)) {
                char buffer[512];
                snprintf(buffer, sizeof(buffer), "line %d: type error: %s",
                         typechecker_error_line(tc), typechecker_error(tc));
                size_t len = strlen(buffer);
                *error = agim_alloc(len + 1);
                memcpy((char *)*error, buffer, len + 1);
            }
            typechecker_free(tc);
            ast_free(ast);
            parser_free(parser);
            lexer_free(lexer);
            return NULL;
        }
        typechecker_free(tc);
    }

    /* Compile */
    Compiler *compiler = compiler_new();
    Bytecode *code = compiler_compile(compiler, ast);

    if (!code) {
        if (error && compiler_error(compiler)) {
            size_t len = strlen(compiler_error(compiler));
            *error = agim_alloc(len + 1);
            memcpy((char *)*error, compiler_error(compiler), len + 1);
        }
    }

    compiler_free(compiler);
    ast_free(ast);
    parser_free(parser);
    lexer_free(lexer);

    return code;
}

Bytecode *agim_compile_file(const char *path, const char **error) {
    if (error) *error = NULL;

    FILE *file = fopen(path, "rb");
    if (!file) {
        if (error) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "could not open file: %s", path);
            size_t len = strlen(buffer);
            *error = agim_alloc(len + 1);
            memcpy((char *)*error, buffer, len + 1);
        }
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *source = agim_alloc(size + 1);
    if (!source) {
        fclose(file);
        if (error) *error = "out of memory";
        return NULL;
    }

    size_t read = fread(source, 1, size, file);
    fclose(file);

    if (read != (size_t)size) {
        agim_free(source);
        if (error) *error = "failed to read file";
        return NULL;
    }
    source[read] = '\0';

    /* Lex */
    Lexer *lexer = lexer_new(source);

    /* Parse */
    Parser *parser = parser_new(lexer);
    AstNode *ast = parser_parse(parser);

    if (!ast) {
        if (error && parser_error(parser)) {
            size_t len = strlen(parser_error(parser));
            *error = agim_alloc(len + 1);
            memcpy((char *)*error, parser_error(parser), len + 1);
        }
        parser_free(parser);
        lexer_free(lexer);
        agim_free(source);
        return NULL;
    }

    /* Type check (if strict mode enabled) */
    if (g_strict_types) {
        TypeChecker *tc = typechecker_new();
        if (!typechecker_check(tc, ast)) {
            if (error && typechecker_error(tc)) {
                char buffer[512];
                snprintf(buffer, sizeof(buffer), "line %d: type error: %s",
                         typechecker_error_line(tc), typechecker_error(tc));
                size_t len = strlen(buffer);
                *error = agim_alloc(len + 1);
                memcpy((char *)*error, buffer, len + 1);
            }
            typechecker_free(tc);
            ast_free(ast);
            parser_free(parser);
            lexer_free(lexer);
            agim_free(source);
            return NULL;
        }
        typechecker_free(tc);
    }

    /* Compile with source path for import resolution */
    Compiler *compiler = compiler_new();
    compiler_set_source_path(compiler, path);
    Bytecode *code = compiler_compile(compiler, ast);

    if (!code) {
        if (error && compiler_error(compiler)) {
            size_t len = strlen(compiler_error(compiler));
            *error = agim_alloc(len + 1);
            memcpy((char *)*error, compiler_error(compiler), len + 1);
        }
    }

    compiler_free(compiler);
    ast_free(ast);
    parser_free(parser);
    lexer_free(lexer);
    agim_free(source);

    return code;
}

void agim_error_free(const char *error) {
    if (error) {
        agim_free((char *)error);
    }
}

AgimResult agim_run(const char *source) {
    return agim_run_with_result(source, NULL, NULL);
}

AgimResult agim_run_with_result(const char *source, Value **result, const char **error) {
    if (result) *result = NULL;
    if (error) *error = NULL;

    /* Compile */
    const char *compile_error = NULL;
    Bytecode *code = agim_compile(source, &compile_error);
    if (!code) {
        if (error && compile_error) {
            *error = compile_error;
        } else if (compile_error) {
            agim_error_free(compile_error);
        }
        return AGIM_ERROR_COMPILE;
    }

    /* Run */
    VM *vm = vm_new();
    vm_load(vm, code);

    VMResult vm_result = vm_run(vm);

    AgimResult agim_result = AGIM_OK;

    if (vm_result != VM_OK && vm_result != VM_HALT) {
        if (error && vm_error(vm)) {
            size_t len = strlen(vm_error(vm));
            *error = agim_alloc(len + 1);
            if (*error) {
                memcpy((char *)*error, vm_error(vm), len + 1);
            }
        }
        agim_result = AGIM_ERROR_RUNTIME;
    } else if (result) {
        /* Get result from top of stack if available */
        *result = vm_peek(vm, 0);
    }

    vm_free(vm);
    bytecode_free(code);

    return agim_result;
}
