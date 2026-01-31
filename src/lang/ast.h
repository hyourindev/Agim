/*
 * Agim - Abstract Syntax Tree
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_AST_H
#define AGIM_LANG_AST_H

#include "lang/token.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* AST Node Types */

typedef enum NodeType {
    /* Declarations */
    NODE_PROGRAM,
    NODE_TOOL_DECL,
    NODE_FN_DECL,
    NODE_PARAM,
    NODE_IMPORT,
    NODE_IMPORT_FROM,
    NODE_EXPORT,
    NODE_STRUCT_DECL,
    NODE_STRUCT_FIELD,
    NODE_ENUM_DECL,
    NODE_ENUM_VARIANT,
    NODE_TYPE_ALIAS,

    /* Statements */
    NODE_BLOCK,
    NODE_LET,
    NODE_CONST,
    NODE_IF,
    NODE_FOR,
    NODE_WHILE,
    NODE_RETURN,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_EXPR_STMT,

    /* Type Expressions */
    NODE_TYPE_NAME,
    NODE_TYPE_GENERIC,
    NODE_TYPE_ARRAY,
    NODE_TYPE_MAP,
    NODE_TYPE_FUNC,

    /* Expressions */
    NODE_BINARY,
    NODE_UNARY,
    NODE_CALL,
    NODE_MEMBER,
    NODE_INDEX,
    NODE_TERNARY,
    NODE_ASSIGN,
    NODE_IDENT,
    NODE_INT,
    NODE_FLOAT,
    NODE_STRING,
    NODE_BOOL,
    NODE_NIL,
    NODE_ARRAY,
    NODE_MAP,
    NODE_MATCH,
    NODE_MATCH_ARM,
    NODE_RESULT_OK,
    NODE_RESULT_ERR,
    NODE_TRY,
    NODE_SOME,
    NODE_NONE,
    NODE_STRUCT_INIT,
    NODE_SPREAD,
    NODE_ENUM_EXPR,
    NODE_RANGE,
} NodeType;

/* AST Node Structure */

typedef struct AstNode AstNode;

struct AstNode {
    NodeType type;
    int line;

    union {
        struct {
            AstNode **decls;
            size_t count;
            size_t capacity;
        } program;

        struct {
            char *name;
            AstNode **params;
            size_t param_count;
            AstNode *return_type;
            AstNode *body;
            char *description;
            AstNode *params_map;  /* Parameter descriptions from @tool decorator */
        } fn_decl;

        struct {
            char *name;
            AstNode *type_ann;
        } param;

        struct {
            AstNode **stmts;
            size_t count;
            size_t capacity;
        } block;

        struct {
            char *name;
            AstNode *type_ann;
            AstNode *value;
            bool is_mutable;
        } var_decl;

        struct {
            AstNode *cond;
            AstNode *then_block;
            AstNode *else_block;
        } if_stmt;

        struct {
            char *var;
            char *index_var;
            AstNode *iterable;
            AstNode *body;
        } for_stmt;

        struct {
            AstNode *cond;
            AstNode *body;
        } while_stmt;

        struct {
            AstNode *value;
        } return_stmt;

        struct {
            TokenType op;
            AstNode *left;
            AstNode *right;
        } binary;

        struct {
            TokenType op;
            AstNode *operand;
        } unary;

        struct {
            AstNode *callee;
            AstNode **args;
            size_t arg_count;
        } call;

        struct {
            AstNode *object;
            char *field;
        } member;

        struct {
            AstNode *object;
            AstNode *index;
        } index_expr;

        struct {
            AstNode *cond;
            AstNode *then_expr;
            AstNode *else_expr;
        } ternary;

        struct {
            AstNode *target;
            TokenType op;
            AstNode *value;
        } assign;

        struct {
            char *name;
        } ident;

        int64_t int_val;
        double float_val;
        char *string_val;
        bool bool_val;

        struct {
            AstNode **elements;
            size_t count;
        } array;

        struct {
            char **keys;
            AstNode **values;
            size_t count;
        } map;

        struct {
            char *path;
        } import_stmt;

        struct {
            char **names;
            size_t name_count;
            char *path;
        } import_from;

        struct {
            AstNode *decl;
        } export_stmt;

        struct {
            AstNode *expr;
            AstNode **arms;
            size_t arm_count;
        } match_expr;

        struct {
            enum {
                MATCH_PATTERN_OK,
                MATCH_PATTERN_ERR,
                MATCH_PATTERN_SOME,
                MATCH_PATTERN_NONE,
                MATCH_PATTERN_ENUM
            } pattern_kind;
            char *binding_name;
            char *variant_name;
            AstNode *body;
        } match_arm;

        struct {
            AstNode *value;
        } result_expr;

        struct {
            AstNode *expr;
        } try_expr;

        struct {
            char *name;
            AstNode **fields;
            size_t field_count;
        } struct_decl;

        struct {
            char *name;
            AstNode *type_ann;
        } struct_field;

        struct {
            char *name;
            AstNode **variants;
            size_t variant_count;
        } enum_decl;

        struct {
            char *name;
            AstNode *payload_type;
        } enum_variant;

        struct {
            char *name;
            AstNode *aliased;
        } type_alias;

        struct {
            char *name;
        } type_name;

        struct {
            char *name;
            AstNode **type_args;
            size_t arg_count;
        } type_generic;

        struct {
            AstNode *elem_type;
        } type_array;

        struct {
            AstNode *key_type;
            AstNode *value_type;
        } type_map;

        struct {
            AstNode **param_types;
            size_t param_count;
            AstNode *return_type;
        } type_func;

        struct {
            AstNode *value;
        } some_expr;

        struct {
            char *type_name;
            char **field_names;
            AstNode **field_values;
            size_t field_count;
            AstNode *spread;
        } struct_init;

        struct {
            AstNode *expr;
        } spread_expr;

        struct {
            char *enum_type;
            char *variant_name;
            AstNode *payload;
        } enum_expr;

        struct {
            AstNode *start;
            AstNode *end;
            bool inclusive;
        } range;

        struct {
            char *name;
            char *description;
            AstNode *params_map;
            AstNode *fn_decl;
        } tool_decl_meta;
    } as;
};

/* AST Node Construction */

AstNode *ast_new(NodeType type, int line);
void ast_free(AstNode *node);

/* AST Node Helpers */

AstNode *ast_program(int line);
void ast_program_add(AstNode *program, AstNode *decl);
AstNode *ast_block(int line);
void ast_block_add(AstNode *block, AstNode *stmt);
AstNode *ast_binary(TokenType op, AstNode *left, AstNode *right, int line);
AstNode *ast_unary(TokenType op, AstNode *operand, int line);
AstNode *ast_int(int64_t value, int line);
AstNode *ast_float(double value, int line);
AstNode *ast_string(const char *value, size_t length, int line);
AstNode *ast_bool(bool value, int line);
AstNode *ast_nil(int line);
AstNode *ast_ident(const char *name, size_t length, int line);

/* Type Node Helpers */

AstNode *ast_type_name(const char *name, size_t length, int line);
AstNode *ast_type_generic(const char *name, AstNode **type_args, size_t arg_count, int line);
AstNode *ast_type_array(AstNode *elem_type, int line);
AstNode *ast_type_map(AstNode *key_type, AstNode *value_type, int line);
AstNode *ast_type_func(AstNode **param_types, size_t param_count, AstNode *return_type, int line);

/* Struct/Enum Helpers */

AstNode *ast_struct_decl(const char *name, int line);
void ast_struct_add_field(AstNode *struct_decl, const char *name, AstNode *type_ann, int line);
AstNode *ast_enum_decl(const char *name, int line);
void ast_enum_add_variant(AstNode *enum_decl, const char *name, AstNode *payload_type, int line);
AstNode *ast_type_alias(const char *name, AstNode *aliased, int line);
AstNode *ast_struct_init(const char *type_name, int line);
void ast_struct_init_add_field(AstNode *init, const char *name, AstNode *value);
void ast_struct_init_set_spread(AstNode *init, AstNode *spread);
AstNode *ast_some(AstNode *value, int line);
AstNode *ast_none(int line);
AstNode *ast_spread(AstNode *expr, int line);
AstNode *ast_enum_variant(const char *enum_type, const char *variant_name, AstNode *payload, int line);
AstNode *ast_range(AstNode *start, AstNode *end, bool inclusive, int line);

/* Debug */

void ast_print(AstNode *node, int indent);
const char *ast_node_type_name(NodeType type);

#endif /* AGIM_LANG_AST_H */
