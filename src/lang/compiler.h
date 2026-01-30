/*
 * Agim - Compiler
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_COMPILER_H
#define AGIM_LANG_COMPILER_H

#include "lang/ast.h"
#include "vm/bytecode.h"

/*============================================================================
 * Compiler
 *============================================================================*/

typedef struct Compiler Compiler;

/**
 * Create a new compiler.
 */
Compiler *compiler_new(void);

/**
 * Free compiler resources.
 */
void compiler_free(Compiler *compiler);

/**
 * Set the source file path for import resolution.
 * This should be called before compile if imports are used.
 */
void compiler_set_source_path(Compiler *compiler, const char *path);

/**
 * Compile AST to bytecode.
 * Returns NULL on error.
 */
Bytecode *compiler_compile(Compiler *compiler, AstNode *ast);

/**
 * Get error message (if any).
 */
const char *compiler_error(Compiler *compiler);

/**
 * Get line number of error.
 */
int compiler_error_line(Compiler *compiler);

#endif /* AGIM_LANG_COMPILER_H */
