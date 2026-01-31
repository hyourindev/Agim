/*
 * Agim - Lexer Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/lexer.h"
#include "util/alloc.h"

#include <ctype.h>
#include <string.h>
#include <stdbool.h>

/* Lexer Structure */

struct Lexer {
    const char *source;
    const char *start;
    const char *current;
    int line;
    int column;
    int start_column;
    Token peeked;
    bool has_peeked;
};

/* Keywords Table */

static struct {
    const char *name;
    TokenType type;
} keywords[] = {
    {"tool", TOK_TOOL},
    {"fn", TOK_FN},
    {"let", TOK_LET},
    {"mut", TOK_MUT},
    {"const", TOK_CONST},
    {"if", TOK_IF},
    {"else", TOK_ELSE},
    {"for", TOK_FOR},
    {"while", TOK_WHILE},
    {"in", TOK_IN},
    {"return", TOK_RETURN},
    {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE},
    {"true", TOK_TRUE},
    {"false", TOK_FALSE},
    {"nil", TOK_NIL},
    {"and", TOK_AND},
    {"or", TOK_OR},
    {"not", TOK_NOT},
    {"import", TOK_IMPORT},
    {"from", TOK_FROM},
    {"export", TOK_EXPORT},
    {"match", TOK_MATCH},
    {"ok", TOK_OK},
    {"err", TOK_ERR},
    {"try", TOK_TRY},
    {"struct", TOK_STRUCT},
    {"enum", TOK_ENUM},
    {"alias", TOK_ALIAS},
    {"some", TOK_SOME},
    {"none", TOK_NONE},
    {"int", TOK_TYPE_INT},
    {"float", TOK_TYPE_FLOAT},
    {"string", TOK_TYPE_STRING},
    {"bool", TOK_TYPE_BOOL},
    {"void", TOK_TYPE_VOID},
    {"bytes", TOK_TYPE_BYTES},
    {"Option", TOK_TYPE_OPTION},
    {"Result", TOK_TYPE_RESULT},
    {"map", TOK_TYPE_MAP},
    {"Pid", TOK_TYPE_PID},
    {NULL, TOK_ERROR},
};

/* Token Type Names */

const char *token_type_name(TokenType type) {
    static const char *names[] = {
        [TOK_INT] = "INT",
        [TOK_FLOAT] = "FLOAT",
        [TOK_STRING] = "STRING",
        [TOK_TRUE] = "TRUE",
        [TOK_FALSE] = "FALSE",
        [TOK_NIL] = "NIL",
        [TOK_IDENT] = "IDENT",
        [TOK_TOOL] = "TOOL",
        [TOK_FN] = "FN",
        [TOK_LET] = "LET",
        [TOK_MUT] = "MUT",
        [TOK_CONST] = "CONST",
        [TOK_IF] = "IF",
        [TOK_ELSE] = "ELSE",
        [TOK_FOR] = "FOR",
        [TOK_WHILE] = "WHILE",
        [TOK_IN] = "IN",
        [TOK_RETURN] = "RETURN",
        [TOK_BREAK] = "BREAK",
        [TOK_CONTINUE] = "CONTINUE",
        [TOK_AND] = "AND",
        [TOK_OR] = "OR",
        [TOK_NOT] = "NOT",
        [TOK_IMPORT] = "IMPORT",
        [TOK_FROM] = "FROM",
        [TOK_EXPORT] = "EXPORT",
        [TOK_MATCH] = "MATCH",
        [TOK_OK] = "OK",
        [TOK_ERR] = "ERR",
        [TOK_TRY] = "TRY",
        [TOK_STRUCT] = "STRUCT",
        [TOK_ENUM] = "ENUM",
        [TOK_ALIAS] = "ALIAS",
        [TOK_SOME] = "SOME",
        [TOK_NONE] = "NONE",
        [TOK_TYPE_INT] = "TYPE_INT",
        [TOK_TYPE_FLOAT] = "TYPE_FLOAT",
        [TOK_TYPE_STRING] = "TYPE_STRING",
        [TOK_TYPE_BOOL] = "TYPE_BOOL",
        [TOK_TYPE_VOID] = "TYPE_VOID",
        [TOK_TYPE_BYTES] = "TYPE_BYTES",
        [TOK_TYPE_OPTION] = "TYPE_OPTION",
        [TOK_TYPE_RESULT] = "TYPE_RESULT",
        [TOK_TYPE_MAP] = "TYPE_MAP",
        [TOK_TYPE_PID] = "TYPE_PID",
        [TOK_PLUS] = "PLUS",
        [TOK_MINUS] = "MINUS",
        [TOK_STAR] = "STAR",
        [TOK_SLASH] = "SLASH",
        [TOK_PERCENT] = "PERCENT",
        [TOK_EQ] = "EQ",
        [TOK_NE] = "NE",
        [TOK_LT] = "LT",
        [TOK_LE] = "LE",
        [TOK_GT] = "GT",
        [TOK_GE] = "GE",
        [TOK_ASSIGN] = "ASSIGN",
        [TOK_PLUS_ASSIGN] = "PLUS_ASSIGN",
        [TOK_MINUS_ASSIGN] = "MINUS_ASSIGN",
        [TOK_STAR_ASSIGN] = "STAR_ASSIGN",
        [TOK_SLASH_ASSIGN] = "SLASH_ASSIGN",
        [TOK_LPAREN] = "LPAREN",
        [TOK_RPAREN] = "RPAREN",
        [TOK_LBRACE] = "LBRACE",
        [TOK_RBRACE] = "RBRACE",
        [TOK_LBRACKET] = "LBRACKET",
        [TOK_RBRACKET] = "RBRACKET",
        [TOK_COMMA] = "COMMA",
        [TOK_DOT] = "DOT",
        [TOK_COLON] = "COLON",
        [TOK_COLON_COLON] = "COLON_COLON",
        [TOK_ARROW] = "ARROW",
        [TOK_QUESTION] = "QUESTION",
        [TOK_RANGE] = "RANGE",
        [TOK_RANGE_INCL] = "RANGE_INCL",
        [TOK_SPREAD] = "SPREAD",
        [TOK_AT] = "AT",
        [TOK_FAT_ARROW] = "FAT_ARROW",
        [TOK_SEMICOLON] = "SEMICOLON",
        [TOK_NEWLINE] = "NEWLINE",
        [TOK_EOF] = "EOF",
        [TOK_ERROR] = "ERROR",
    };
    return names[type];
}

/* Lexer Lifecycle */

Lexer *lexer_new(const char *source) {
    Lexer *lexer = agim_alloc(sizeof(Lexer));
    lexer->source = source;
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->column = 1;
    lexer->start_column = 1;
    lexer->has_peeked = false;
    return lexer;
}

void lexer_free(Lexer *lexer) {
    if (lexer) {
        agim_free(lexer);
    }
}

int lexer_line(Lexer *lexer) {
    return lexer->line;
}

int lexer_column(Lexer *lexer) {
    return lexer->column;
}

/* Character Helpers */

static bool is_at_end(Lexer *lexer) {
    return *lexer->current == '\0';
}

static char peek_char(Lexer *lexer) {
    return *lexer->current;
}

static char peek_next(Lexer *lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static char advance(Lexer *lexer) {
    char c = *lexer->current++;
    lexer->column++;
    return c;
}

static bool match(Lexer *lexer, char expected) {
    if (is_at_end(lexer)) return false;
    if (*lexer->current != expected) return false;
    lexer->current++;
    lexer->column++;
    return true;
}

/* Token Creation */

static Token make_token(Lexer *lexer, TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer->start;
    token.length = (size_t)(lexer->current - lexer->start);
    token.line = lexer->line;
    token.column = lexer->start_column;
    return token;
}

static Token error_token(Lexer *lexer, const char *message) {
    Token token;
    token.type = TOK_ERROR;
    token.start = message;
    token.length = strlen(message);
    token.line = lexer->line;
    token.column = lexer->start_column;
    return token;
}

/* Whitespace and Comments */

static void skip_whitespace(Lexer *lexer) {
    for (;;) {
        char c = peek_char(lexer);
        switch (c) {
        case ' ':
        case '\t':
        case '\r':
            advance(lexer);
            break;
        case '/':
            if (peek_next(lexer) == '/') {
                while (peek_char(lexer) != '\n' && !is_at_end(lexer)) {
                    advance(lexer);
                }
            } else if (peek_next(lexer) == '*') {
                advance(lexer);
                advance(lexer);
                while (!is_at_end(lexer)) {
                    if (peek_char(lexer) == '*' && peek_next(lexer) == '/') {
                        advance(lexer);
                        advance(lexer);
                        break;
                    }
                    if (peek_char(lexer) == '\n') {
                        lexer->line++;
                        lexer->column = 0;
                    }
                    advance(lexer);
                }
            } else {
                return;
            }
            break;
        default:
            return;
        }
    }
}

/* Number Scanning */

static Token scan_number(Lexer *lexer) {
    bool is_float = false;

    while (isdigit(peek_char(lexer)) || peek_char(lexer) == '_') {
        advance(lexer);
    }

    if (peek_char(lexer) == '.' && isdigit(peek_next(lexer))) {
        is_float = true;
        advance(lexer);
        while (isdigit(peek_char(lexer)) || peek_char(lexer) == '_') {
            advance(lexer);
        }
    }

    if (peek_char(lexer) == 'e' || peek_char(lexer) == 'E') {
        is_float = true;
        advance(lexer);
        if (peek_char(lexer) == '+' || peek_char(lexer) == '-') {
            advance(lexer);
        }
        while (isdigit(peek_char(lexer))) {
            advance(lexer);
        }
    }

    return make_token(lexer, is_float ? TOK_FLOAT : TOK_INT);
}

/* String Scanning */

static Token scan_string(Lexer *lexer) {
    while (peek_char(lexer) != '"' && !is_at_end(lexer)) {
        if (peek_char(lexer) == '\n') {
            lexer->line++;
            lexer->column = 0;
        }
        if (peek_char(lexer) == '\\' && peek_next(lexer) != '\0') {
            advance(lexer);
        }
        advance(lexer);
    }

    if (is_at_end(lexer)) {
        return error_token(lexer, "unterminated string");
    }

    advance(lexer);
    return make_token(lexer, TOK_STRING);
}

/* Identifier and Keyword Scanning */

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool is_alnum(char c) {
    return is_alpha(c) || isdigit(c);
}

static TokenType check_keyword(Lexer *lexer) {
    size_t length = (size_t)(lexer->current - lexer->start);

    for (int i = 0; keywords[i].name != NULL; i++) {
        if (strlen(keywords[i].name) == length &&
            memcmp(lexer->start, keywords[i].name, length) == 0) {
            return keywords[i].type;
        }
    }

    return TOK_IDENT;
}

static Token scan_identifier(Lexer *lexer) {
    while (is_alnum(peek_char(lexer))) {
        advance(lexer);
    }
    return make_token(lexer, check_keyword(lexer));
}

/* Main Scanning */

static Token scan_token(Lexer *lexer) {
    skip_whitespace(lexer);

    lexer->start = lexer->current;
    lexer->start_column = lexer->column;

    if (is_at_end(lexer)) {
        return make_token(lexer, TOK_EOF);
    }

    char c = advance(lexer);

    if (is_alpha(c)) {
        return scan_identifier(lexer);
    }

    if (isdigit(c)) {
        return scan_number(lexer);
    }

    switch (c) {
    case '\n':
        lexer->line++;
        lexer->column = 1;
        return make_token(lexer, TOK_NEWLINE);

    case '(': return make_token(lexer, TOK_LPAREN);
    case ')': return make_token(lexer, TOK_RPAREN);
    case '{': return make_token(lexer, TOK_LBRACE);
    case '}': return make_token(lexer, TOK_RBRACE);
    case '[': return make_token(lexer, TOK_LBRACKET);
    case ']': return make_token(lexer, TOK_RBRACKET);
    case ',': return make_token(lexer, TOK_COMMA);
    case ':':
        if (match(lexer, ':')) return make_token(lexer, TOK_COLON_COLON);
        return make_token(lexer, TOK_COLON);
    case '?': return make_token(lexer, TOK_QUESTION);
    case '%': return make_token(lexer, TOK_PERCENT);

    case '+':
        return make_token(lexer, match(lexer, '=') ? TOK_PLUS_ASSIGN : TOK_PLUS);
    case '-':
        if (match(lexer, '>')) return make_token(lexer, TOK_ARROW);
        if (match(lexer, '=')) return make_token(lexer, TOK_MINUS_ASSIGN);
        return make_token(lexer, TOK_MINUS);
    case '*':
        return make_token(lexer, match(lexer, '=') ? TOK_STAR_ASSIGN : TOK_STAR);
    case '/':
        return make_token(lexer, match(lexer, '=') ? TOK_SLASH_ASSIGN : TOK_SLASH);
    case '=':
        if (match(lexer, '=')) return make_token(lexer, TOK_EQ);
        if (match(lexer, '>')) return make_token(lexer, TOK_FAT_ARROW);
        return make_token(lexer, TOK_ASSIGN);
    case '!':
        return make_token(lexer, match(lexer, '=') ? TOK_NE : TOK_NOT);
    case '<':
        return make_token(lexer, match(lexer, '=') ? TOK_LE : TOK_LT);
    case '>':
        return make_token(lexer, match(lexer, '=') ? TOK_GE : TOK_GT);

    case '.':
        if (match(lexer, '.')) {
            if (match(lexer, '.')) return make_token(lexer, TOK_SPREAD);
            if (match(lexer, '=')) return make_token(lexer, TOK_RANGE_INCL);
            return make_token(lexer, TOK_RANGE);
        }
        return make_token(lexer, TOK_DOT);

    case ';':
        return make_token(lexer, TOK_SEMICOLON);

    case '"':
        return scan_string(lexer);

    case '@':
        return make_token(lexer, TOK_AT);
    }

    return error_token(lexer, "unexpected character");
}

/* Public API */

Token lexer_next(Lexer *lexer) {
    if (lexer->has_peeked) {
        lexer->has_peeked = false;
        return lexer->peeked;
    }
    return scan_token(lexer);
}

Token lexer_peek(Lexer *lexer) {
    if (!lexer->has_peeked) {
        lexer->peeked = scan_token(lexer);
        lexer->has_peeked = true;
    }
    return lexer->peeked;
}
