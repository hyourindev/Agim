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

/*============================================================================
 * Parser
 *============================================================================*/

typedef struct Parser Parser;

/**
 * Create a new parser.
 */
Parser *parser_new(Lexer *lexer);

/**
 * Free parser resources.
 */
void parser_free(Parser *parser);

/**
 * Parse entire program.
 * Returns NULL on error.
 */
AstNode *parser_parse(Parser *parser);

/**
 * Get error message (if any).
 */
const char *parser_error(Parser *parser);

/**
 * Get line number of error.
 */
int parser_error_line(Parser *parser);

#endif /* AGIM_LANG_PARSER_H */
