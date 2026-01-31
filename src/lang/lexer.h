/*
 * Agim - Lexer
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_LEXER_H
#define AGIM_LANG_LEXER_H

#include "lang/token.h"

typedef struct Lexer Lexer;

Lexer *lexer_new(const char *source);
void lexer_free(Lexer *lexer);
Token lexer_next(Lexer *lexer);
Token lexer_peek(Lexer *lexer);
int lexer_line(Lexer *lexer);
int lexer_column(Lexer *lexer);

#endif /* AGIM_LANG_LEXER_H */
