/*
 * Agim - Parser
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_PARSER_H
#define AGIM_LANG_PARSER_H

#include "lang/ast.h"
#include "lang/lexer.h"

typedef struct Parser Parser;

Parser *parser_new(Lexer *lexer);
void parser_free(Parser *parser);
AstNode *parser_parse(Parser *parser);
const char *parser_error(Parser *parser);
int parser_error_line(Parser *parser);

#endif /* AGIM_LANG_PARSER_H */
