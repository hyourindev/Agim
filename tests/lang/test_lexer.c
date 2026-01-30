/*
 * Agim - Lexer Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "lang/lexer.h"

void test_numbers(void) {
    Lexer *lexer = lexer_new("42 3.14 1_000_000 0.5e10");

    Token tok = lexer_next(lexer);
    ASSERT_EQ(TOK_INT, tok.type);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_FLOAT, tok.type);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_INT, tok.type);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_FLOAT, tok.type);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_EOF, tok.type);

    lexer_free(lexer);
}

void test_strings(void) {
    Lexer *lexer = lexer_new("\"hello\" \"world\\n\" \"with \\\"quotes\\\"\"");

    Token tok = lexer_next(lexer);
    ASSERT_EQ(TOK_STRING, tok.type);
    ASSERT_EQ(7, tok.length); /* "hello" */

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_STRING, tok.type);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_STRING, tok.type);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_EOF, tok.type);

    lexer_free(lexer);
}

void test_keywords(void) {
    Lexer *lexer = lexer_new("fn tool let const if else while for in return true false nil and or not");

    ASSERT_EQ(TOK_FN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_TOOL, lexer_next(lexer).type);
    ASSERT_EQ(TOK_LET, lexer_next(lexer).type);
    ASSERT_EQ(TOK_CONST, lexer_next(lexer).type);
    ASSERT_EQ(TOK_IF, lexer_next(lexer).type);
    ASSERT_EQ(TOK_ELSE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_WHILE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_FOR, lexer_next(lexer).type);
    ASSERT_EQ(TOK_IN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_RETURN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_TRUE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_FALSE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_NIL, lexer_next(lexer).type);
    ASSERT_EQ(TOK_AND, lexer_next(lexer).type);
    ASSERT_EQ(TOK_OR, lexer_next(lexer).type);
    ASSERT_EQ(TOK_NOT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_EOF, lexer_next(lexer).type);

    lexer_free(lexer);
}

void test_operators(void) {
    Lexer *lexer = lexer_new("+ - * / % == != < <= > >= = += -= -> .. ..=");

    ASSERT_EQ(TOK_PLUS, lexer_next(lexer).type);
    ASSERT_EQ(TOK_MINUS, lexer_next(lexer).type);
    ASSERT_EQ(TOK_STAR, lexer_next(lexer).type);
    ASSERT_EQ(TOK_SLASH, lexer_next(lexer).type);
    ASSERT_EQ(TOK_PERCENT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_EQ, lexer_next(lexer).type);
    ASSERT_EQ(TOK_NE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_LT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_LE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_GT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_GE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_ASSIGN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_PLUS_ASSIGN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_MINUS_ASSIGN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_ARROW, lexer_next(lexer).type);
    ASSERT_EQ(TOK_RANGE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_RANGE_INCL, lexer_next(lexer).type);
    ASSERT_EQ(TOK_EOF, lexer_next(lexer).type);

    lexer_free(lexer);
}

void test_delimiters(void) {
    Lexer *lexer = lexer_new("( ) { } [ ] , . : ?");

    ASSERT_EQ(TOK_LPAREN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_RPAREN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_LBRACE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_RBRACE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_LBRACKET, lexer_next(lexer).type);
    ASSERT_EQ(TOK_RBRACKET, lexer_next(lexer).type);
    ASSERT_EQ(TOK_COMMA, lexer_next(lexer).type);
    ASSERT_EQ(TOK_DOT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_COLON, lexer_next(lexer).type);
    ASSERT_EQ(TOK_QUESTION, lexer_next(lexer).type);
    ASSERT_EQ(TOK_EOF, lexer_next(lexer).type);

    lexer_free(lexer);
}

void test_comments(void) {
    Lexer *lexer = lexer_new("a // comment\nb /* multi\nline */ c");

    ASSERT_EQ(TOK_IDENT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_NEWLINE, lexer_next(lexer).type);
    ASSERT_EQ(TOK_IDENT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_IDENT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_EOF, lexer_next(lexer).type);

    lexer_free(lexer);
}

void test_identifiers(void) {
    Lexer *lexer = lexer_new("foo bar_baz _private CamelCase foo123");

    Token tok = lexer_next(lexer);
    ASSERT_EQ(TOK_IDENT, tok.type);
    ASSERT_EQ(3, tok.length);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_IDENT, tok.type);
    ASSERT_EQ(7, tok.length);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_IDENT, tok.type);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_IDENT, tok.type);

    tok = lexer_next(lexer);
    ASSERT_EQ(TOK_IDENT, tok.type);

    ASSERT_EQ(TOK_EOF, lexer_next(lexer).type);

    lexer_free(lexer);
}

void test_line_tracking(void) {
    Lexer *lexer = lexer_new("a\nb\nc");

    Token tok = lexer_next(lexer);
    ASSERT_EQ(1, tok.line);

    tok = lexer_next(lexer); /* newline */
    tok = lexer_next(lexer);
    ASSERT_EQ(2, tok.line);

    tok = lexer_next(lexer); /* newline */
    tok = lexer_next(lexer);
    ASSERT_EQ(3, tok.line);

    lexer_free(lexer);
}

int main(void) {
    RUN_TEST(test_numbers);
    RUN_TEST(test_strings);
    RUN_TEST(test_keywords);
    RUN_TEST(test_operators);
    RUN_TEST(test_delimiters);
    RUN_TEST(test_comments);
    RUN_TEST(test_identifiers);
    RUN_TEST(test_line_tracking);

    return TEST_RESULT();
}
