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

typedef struct Compiler Compiler;

Compiler *compiler_new(void);
void compiler_free(Compiler *compiler);
void compiler_set_source_path(Compiler *compiler, const char *path);
Bytecode *compiler_compile(Compiler *compiler, AstNode *ast);
const char *compiler_error(Compiler *compiler);
int compiler_error_line(Compiler *compiler);

#endif /* AGIM_LANG_COMPILER_H */
