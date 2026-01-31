/*
 * Agim - Token Types
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_TOKEN_H
#define AGIM_LANG_TOKEN_H

#include <stddef.h>

/* Token Types */

typedef enum TokenType {
    /* Literals */
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NIL,

    /* Identifiers & Keywords */
    TOK_IDENT,
    TOK_TOOL,
    TOK_FN,
    TOK_LET,
    TOK_MUT,
    TOK_CONST,
    TOK_IF,
    TOK_ELSE,
    TOK_FOR,
    TOK_WHILE,
    TOK_IN,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_IMPORT,
    TOK_FROM,
    TOK_EXPORT,
    TOK_MATCH,
    TOK_OK,
    TOK_ERR,
    TOK_TRY,
    TOK_STRUCT,
    TOK_ENUM,
    TOK_ALIAS,
    TOK_SOME,
    TOK_NONE,

    /* Built-in Type Names */
    TOK_TYPE_INT,
    TOK_TYPE_FLOAT,
    TOK_TYPE_STRING,
    TOK_TYPE_BOOL,
    TOK_TYPE_VOID,
    TOK_TYPE_BYTES,
    TOK_TYPE_OPTION,
    TOK_TYPE_RESULT,
    TOK_TYPE_MAP,
    TOK_TYPE_PID,

    /* Operators */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_ASSIGN,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN,
    TOK_SLASH_ASSIGN,

    /* Delimiters */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_DOT,
    TOK_COLON,
    TOK_COLON_COLON,
    TOK_ARROW,
    TOK_QUESTION,
    TOK_RANGE,
    TOK_RANGE_INCL,
    TOK_SPREAD,
    TOK_AT,
    TOK_FAT_ARROW,
    TOK_SEMICOLON,

    /* Special */
    TOK_NEWLINE,
    TOK_EOF,
    TOK_ERROR,
} TokenType;

/* Token Structure */

typedef struct Token {
    TokenType type;
    const char *start;
    size_t length;
    int line;
    int column;
} Token;

const char *token_type_name(TokenType type);

#endif /* AGIM_LANG_TOKEN_H */
