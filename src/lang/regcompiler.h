/*
 * Agim - Register Bytecode Compiler
 *
 * Compiles AST to register-based bytecode for the register VM.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_REGCOMPILER_H
#define AGIM_LANG_REGCOMPILER_H

#include "lang/ast.h"
#include "vm/regvm.h"

/*============================================================================
 * Register Compiler API
 *============================================================================*/

/**
 * Compile an AST to register bytecode.
 *
 * @param ast  The parsed AST
 * @return     RegChunk containing the compiled bytecode, or NULL on error
 */
RegChunk *regcompile(AstNode *ast);

/**
 * Compile a single expression to register bytecode.
 * Useful for REPL and testing.
 *
 * @param ast  The expression AST
 * @return     RegChunk containing the compiled bytecode, or NULL on error
 */
RegChunk *regcompile_expr(AstNode *ast);

/**
 * Get the last compilation error message.
 */
const char *regcompile_error(void);

/**
 * Get the line number of the last error.
 */
int regcompile_error_line(void);

#endif /* AGIM_LANG_REGCOMPILER_H */
