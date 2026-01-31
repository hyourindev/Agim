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

RegChunk *regcompile(AstNode *ast);
RegChunk *regcompile_expr(AstNode *ast);
const char *regcompile_error(void);
int regcompile_error_line(void);

#endif /* AGIM_LANG_REGCOMPILER_H */
