/*
 * Agim - Lexer
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_LEXER_H
#define AGIM_LANG_LEXER_H

#include "lang/token.h"

/*============================================================================
 * Lexer
 *============================================================================*/

typedef struct Lexer Lexer;

/**
 * Create a new lexer for the given source code.
 */
Lexer *lexer_new(const char *source);

/**
 * Free lexer resources.
 */
void lexer_free(Lexer *lexer);

/**
 * Get the next token and advance.
 */
Token lexer_next(Lexer *lexer);

/**
 * Peek at the next token without advancing.
 */
Token lexer_peek(Lexer *lexer);

/**
 * Get the current line number.
 */
int lexer_line(Lexer *lexer);

/**
 * Get the current column number.
 */
int lexer_column(Lexer *lexer);

#endif /* AGIM_LANG_LEXER_H */
