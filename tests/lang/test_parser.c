/*
 * Agim - Parser Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/ast.h"

static AstNode *parse(const char *source) {
    Lexer *lexer = lexer_new(source);
    Parser *parser = parser_new(lexer);
    AstNode *ast = parser_parse(parser);

    if (!ast && parser_error(parser)) {
        printf("    Parse error: %s\n", parser_error(parser));
    }

    parser_free(parser);
    lexer_free(lexer);
    return ast;
}

/* Basic Expression Tests */

void test_parse_literals(void) {
    printf("  Testing literal parsing...\n");

    AstNode *ast = parse("42");
    ASSERT(ast != NULL);
    ASSERT_EQ(NODE_PROGRAM, ast->type);
    ASSERT_EQ(1, ast->as.program.count);
    ASSERT_EQ(NODE_EXPR_STMT, ast->as.program.decls[0]->type);
    ast_free(ast);

    ast = parse("3.14");
    ASSERT(ast != NULL);
    ast_free(ast);

    ast = parse("\"hello\"");
    ASSERT(ast != NULL);
    ast_free(ast);

    ast = parse("true");
    ASSERT(ast != NULL);
    ast_free(ast);

    ast = parse("nil");
    ASSERT(ast != NULL);
    ast_free(ast);
}

void test_parse_binary(void) {
    printf("  Testing binary expression parsing...\n");

    AstNode *ast = parse("1 + 2");
    ASSERT(ast != NULL);
    ASSERT_EQ(1, ast->as.program.count);
    AstNode *expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_BINARY, expr->type);
    ASSERT_EQ(TOK_PLUS, expr->as.binary.op);
    ast_free(ast);

    ast = parse("1 + 2 * 3");
    ASSERT(ast != NULL);
    expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_BINARY, expr->type);
    ASSERT_EQ(TOK_PLUS, expr->as.binary.op);
    ASSERT_EQ(NODE_BINARY, expr->as.binary.right->type);
    ASSERT_EQ(TOK_STAR, expr->as.binary.right->as.binary.op);
    ast_free(ast);
}

void test_parse_unary(void) {
    printf("  Testing unary expression parsing...\n");

    AstNode *ast = parse("-42");
    ASSERT(ast != NULL);
    AstNode *expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_UNARY, expr->type);
    ASSERT_EQ(TOK_MINUS, expr->as.unary.op);
    ast_free(ast);

    ast = parse("not true");
    ASSERT(ast != NULL);
    expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_UNARY, expr->type);
    ASSERT_EQ(TOK_NOT, expr->as.unary.op);
    ast_free(ast);
}

void test_parse_ternary(void) {
    printf("  Testing ternary expression parsing...\n");

    AstNode *ast = parse("true ? 1 : 0");
    ASSERT(ast != NULL);
    AstNode *expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_TERNARY, expr->type);
    ast_free(ast);
}

/* Statement Tests */

void test_parse_let(void) {
    printf("  Testing let statement parsing...\n");

    AstNode *ast = parse("let x = 42");
    ASSERT(ast != NULL);
    ASSERT_EQ(1, ast->as.program.count);
    ASSERT_EQ(NODE_LET, ast->as.program.decls[0]->type);
    ASSERT_STR_EQ("x", ast->as.program.decls[0]->as.var_decl.name);
    ast_free(ast);
}

void test_parse_const(void) {
    printf("  Testing const statement parsing...\n");

    AstNode *ast = parse("const PI = 3.14");
    ASSERT(ast != NULL);
    ASSERT_EQ(NODE_CONST, ast->as.program.decls[0]->type);
    ASSERT_STR_EQ("PI", ast->as.program.decls[0]->as.var_decl.name);
    ast_free(ast);
}

void test_parse_if(void) {
    printf("  Testing if statement parsing...\n");

    AstNode *ast = parse("if true { 1 }");
    ASSERT(ast != NULL);
    ASSERT_EQ(NODE_IF, ast->as.program.decls[0]->type);
    ASSERT(ast->as.program.decls[0]->as.if_stmt.else_block == NULL);
    ast_free(ast);

    ast = parse("if true { 1 } else { 0 }");
    ASSERT(ast != NULL);
    ASSERT_EQ(NODE_IF, ast->as.program.decls[0]->type);
    ASSERT(ast->as.program.decls[0]->as.if_stmt.else_block != NULL);
    ast_free(ast);
}

void test_parse_while(void) {
    printf("  Testing while statement parsing...\n");

    AstNode *ast = parse("while true { break }");
    ASSERT(ast != NULL);
    ASSERT_EQ(NODE_WHILE, ast->as.program.decls[0]->type);
    ast_free(ast);
}

void test_parse_for(void) {
    printf("  Testing for statement parsing...\n");

    AstNode *ast = parse("for x in [1, 2, 3] { x }");
    ASSERT(ast != NULL);
    ASSERT_EQ(NODE_FOR, ast->as.program.decls[0]->type);
    ASSERT_STR_EQ("x", ast->as.program.decls[0]->as.for_stmt.var);
    ast_free(ast);
}

/* Function Tests */

void test_parse_fn(void) {
    printf("  Testing function declaration parsing...\n");

    AstNode *ast = parse("fn add(a, b) { return a + b }");
    ASSERT(ast != NULL);
    ASSERT_EQ(NODE_FN_DECL, ast->as.program.decls[0]->type);
    ASSERT_STR_EQ("add", ast->as.program.decls[0]->as.fn_decl.name);
    ASSERT_EQ(2, ast->as.program.decls[0]->as.fn_decl.param_count);
    ast_free(ast);
}

void test_parse_call(void) {
    printf("  Testing function call parsing...\n");

    AstNode *ast = parse("foo()");
    ASSERT(ast != NULL);
    AstNode *expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_CALL, expr->type);
    ASSERT_EQ(0, expr->as.call.arg_count);
    ast_free(ast);

    ast = parse("foo(1, 2, 3)");
    ASSERT(ast != NULL);
    expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_CALL, expr->type);
    ASSERT_EQ(3, expr->as.call.arg_count);
    ast_free(ast);
}

/* Collection Tests */

void test_parse_array(void) {
    printf("  Testing array literal parsing...\n");

    AstNode *ast = parse("[1, 2, 3]");
    ASSERT(ast != NULL);
    AstNode *expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_ARRAY, expr->type);
    ASSERT_EQ(3, expr->as.array.count);
    ast_free(ast);

    ast = parse("[]");
    ASSERT(ast != NULL);
    expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_ARRAY, expr->type);
    ASSERT_EQ(0, expr->as.array.count);
    ast_free(ast);
}

void test_parse_map(void) {
    printf("  Testing map literal parsing...\n");

    AstNode *ast = parse("{a: 1, b: 2}");
    ASSERT(ast != NULL);
    AstNode *expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_MAP, expr->type);
    ASSERT_EQ(2, expr->as.map.count);
    ast_free(ast);
}

void test_parse_index(void) {
    printf("  Testing index expression parsing...\n");

    AstNode *ast = parse("arr[0]");
    ASSERT(ast != NULL);
    AstNode *expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_INDEX, expr->type);
    ast_free(ast);
}

void test_parse_member(void) {
    printf("  Testing member expression parsing...\n");

    AstNode *ast = parse("obj.field");
    ASSERT(ast != NULL);
    AstNode *expr = ast->as.program.decls[0]->as.return_stmt.value;
    ASSERT_EQ(NODE_MEMBER, expr->type);
    ASSERT_STR_EQ("field", expr->as.member.field);
    ast_free(ast);
}

/* Error Handling Tests */

void test_parse_errors(void) {
    printf("  Testing parse error handling...\n");

    Lexer *lexer = lexer_new("let = 42");
    Parser *parser = parser_new(lexer);
    AstNode *ast = parser_parse(parser);

    ASSERT(ast == NULL);
    ASSERT(parser_error(parser) != NULL);

    parser_free(parser);
    lexer_free(lexer);
}

/* Main */

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Agim Parser Tests\n");
    printf("=================================================\n\n");

    printf("Expression tests:\n");
    RUN_TEST(test_parse_literals);
    RUN_TEST(test_parse_binary);
    RUN_TEST(test_parse_unary);
    RUN_TEST(test_parse_ternary);

    printf("\nStatement tests:\n");
    RUN_TEST(test_parse_let);
    RUN_TEST(test_parse_const);
    RUN_TEST(test_parse_if);
    RUN_TEST(test_parse_while);
    RUN_TEST(test_parse_for);

    printf("\nFunction tests:\n");
    RUN_TEST(test_parse_fn);
    RUN_TEST(test_parse_call);

    printf("\nCollection tests:\n");
    RUN_TEST(test_parse_array);
    RUN_TEST(test_parse_map);
    RUN_TEST(test_parse_index);
    RUN_TEST(test_parse_member);

    printf("\nError handling tests:\n");
    RUN_TEST(test_parse_errors);

    printf("\n=================================================\n");
    return TEST_RESULT();
}
