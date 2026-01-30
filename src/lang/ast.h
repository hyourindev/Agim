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

/*============================================================================
 * AST Node Types
 *============================================================================*/

typedef enum NodeType {
    /* Declarations */
    NODE_PROGRAM,
    NODE_TOOL_DECL,
    NODE_FN_DECL,
    NODE_PARAM,
    NODE_IMPORT,        /* import "file.ag" */
    NODE_IMPORT_FROM,   /* import { names } from "file.ag" */
    NODE_EXPORT,        /* export let/fn/tool */
    NODE_STRUCT_DECL,   /* struct Foo { field: Type } */
    NODE_STRUCT_FIELD,  /* field: Type in struct */
    NODE_ENUM_DECL,     /* enum Foo { A, B(Type) } */
    NODE_ENUM_VARIANT,  /* variant in enum */
    NODE_TYPE_ALIAS,    /* type Alias = Type */

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
    NODE_TYPE_NAME,     /* int, string, bool, etc. */
    NODE_TYPE_GENERIC,  /* Option<T>, Result<T, E> */
    NODE_TYPE_ARRAY,    /* [T] */
    NODE_TYPE_MAP,      /* map<K, V> */
    NODE_TYPE_FUNC,     /* fn(A, B) -> C */

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
    NODE_MATCH,         /* match expr { arms } */
    NODE_MATCH_ARM,     /* ok(x) => expr or err(e) => expr */
    NODE_RESULT_OK,     /* ok(value) */
    NODE_RESULT_ERR,    /* err(message) */
    NODE_TRY,           /* try expr */
    NODE_SOME,          /* some(value) */
    NODE_NONE,          /* none */
    NODE_STRUCT_INIT,   /* Foo { field: value } */
    NODE_SPREAD,        /* ...expr */
} NodeType;

/*============================================================================
 * AST Node Structure
 *============================================================================*/

typedef struct AstNode AstNode;

struct AstNode {
    NodeType type;
    int line;

    union {
        /* Program: list of declarations */
        struct {
            AstNode **decls;
            size_t count;
            size_t capacity;
        } program;

        /* Tool/Function declaration */
        struct {
            char *name;
            AstNode **params;
            size_t param_count;
            AstNode *return_type;   /* Type node (NODE_TYPE_*), NULL if not specified */
            AstNode *body;
            char *description;      /* Tool description from @tool decorator */
        } fn_decl;

        /* Parameter */
        struct {
            char *name;
            AstNode *type_ann;  /* Type annotation (NODE_TYPE_*), NULL if not specified */
        } param;

        /* Block: list of statements */
        struct {
            AstNode **stmts;
            size_t count;
            size_t capacity;
        } block;

        /* Let/Const with optional type and mutability */
        struct {
            char *name;
            AstNode *type_ann;   /* Type annotation (NODE_TYPE_*), NULL if not specified */
            AstNode *value;
            bool is_mutable;     /* true for `let mut`, false for `let` */
        } var_decl;

        /* If */
        struct {
            AstNode *cond;
            AstNode *then_block;
            AstNode *else_block;  /* NULL if no else */
        } if_stmt;

        /* For */
        struct {
            char *var;
            char *index_var;  /* NULL if not specified */
            AstNode *iterable;
            AstNode *body;
        } for_stmt;

        /* While */
        struct {
            AstNode *cond;
            AstNode *body;
        } while_stmt;

        /* Return */
        struct {
            AstNode *value;  /* NULL if bare return */
        } return_stmt;

        /* Binary expression */
        struct {
            TokenType op;
            AstNode *left;
            AstNode *right;
        } binary;

        /* Unary expression */
        struct {
            TokenType op;
            AstNode *operand;
        } unary;

        /* Function call */
        struct {
            AstNode *callee;
            AstNode **args;
            size_t arg_count;
        } call;

        /* Member access (a.b) */
        struct {
            AstNode *object;
            char *field;
        } member;

        /* Index access (a[b]) */
        struct {
            AstNode *object;
            AstNode *index;
        } index_expr;

        /* Ternary (cond ? a : b) */
        struct {
            AstNode *cond;
            AstNode *then_expr;
            AstNode *else_expr;
        } ternary;

        /* Assignment */
        struct {
            AstNode *target;
            TokenType op;  /* =, +=, -=, etc. */
            AstNode *value;
        } assign;

        /* Identifier */
        struct {
            char *name;
        } ident;

        /* Literals */
        int64_t int_val;
        double float_val;
        char *string_val;
        bool bool_val;

        /* Array literal */
        struct {
            AstNode **elements;
            size_t count;
        } array;

        /* Map literal */
        struct {
            char **keys;
            AstNode **values;
            size_t count;
        } map;

        /* Import: import "path.ag" */
        struct {
            char *path;
        } import_stmt;

        /* Import from: import { name1, name2 } from "path.ag" */
        struct {
            char **names;
            size_t name_count;
            char *path;
        } import_from;

        /* Export: export let/fn/tool */
        struct {
            AstNode *decl;
        } export_stmt;

        /* Match expression */
        struct {
            AstNode *expr;
            AstNode **arms;
            size_t arm_count;
        } match_expr;

        /* Match arm: ok(name) => expr or err(name) => expr */
        struct {
            bool is_ok;          /* true for ok, false for err */
            char *binding_name;  /* variable name to bind the value */
            AstNode *body;
        } match_arm;

        /* Result ok/err: ok(value) or err(message) */
        struct {
            AstNode *value;
        } result_expr;

        /* Try expression: try expr */
        struct {
            AstNode *expr;
        } try_expr;

        /* Struct declaration: struct Foo { field: Type, ... } */
        struct {
            char *name;
            AstNode **fields;    /* Array of NODE_STRUCT_FIELD */
            size_t field_count;
        } struct_decl;

        /* Struct field: name: Type */
        struct {
            char *name;
            AstNode *type_ann;   /* Type annotation */
        } struct_field;

        /* Enum declaration: enum Foo { A, B(Type), ... } */
        struct {
            char *name;
            AstNode **variants;  /* Array of NODE_ENUM_VARIANT */
            size_t variant_count;
        } enum_decl;

        /* Enum variant: Name or Name(Type) */
        struct {
            char *name;
            AstNode *payload_type;  /* NULL for unit variant, type for tuple variant */
        } enum_variant;

        /* Type alias: type Alias = Type */
        struct {
            char *name;
            AstNode *aliased;   /* The aliased type */
        } type_alias;

        /* Type name: int, string, bool, CustomType */
        struct {
            char *name;
        } type_name;

        /* Generic type: Option<T>, Result<T, E> */
        struct {
            char *name;
            AstNode **type_args;  /* Array of type arguments */
            size_t arg_count;
        } type_generic;

        /* Array type: [T] */
        struct {
            AstNode *elem_type;
        } type_array;

        /* Map type: map<K, V> */
        struct {
            AstNode *key_type;
            AstNode *value_type;
        } type_map;

        /* Function type: fn(A, B) -> C */
        struct {
            AstNode **param_types;
            size_t param_count;
            AstNode *return_type;
        } type_func;

        /* Some: some(value) */
        struct {
            AstNode *value;
        } some_expr;

        /* Struct initialization: Foo { field: value, ... } */
        struct {
            char *type_name;
            char **field_names;
            AstNode **field_values;
            size_t field_count;
            AstNode *spread;     /* ...other_struct (NULL if not present) */
        } struct_init;

        /* Spread: ...expr */
        struct {
            AstNode *expr;
        } spread_expr;

        /* Tool decorator metadata */
        struct {
            char *name;
            char *description;
            AstNode *params_map;     /* Map of param schemas */
            AstNode *fn_decl;        /* The decorated function */
        } tool_decl_meta;
    } as;
};

/*============================================================================
 * AST Node Construction
 *============================================================================*/

/**
 * Create a new AST node.
 */
AstNode *ast_new(NodeType type, int line);

/**
 * Free an AST node and all children.
 */
void ast_free(AstNode *node);

/*============================================================================
 * AST Node Helpers
 *============================================================================*/

/**
 * Create program node.
 */
AstNode *ast_program(int line);

/**
 * Add declaration to program.
 */
void ast_program_add(AstNode *program, AstNode *decl);

/**
 * Create block node.
 */
AstNode *ast_block(int line);

/**
 * Add statement to block.
 */
void ast_block_add(AstNode *block, AstNode *stmt);

/**
 * Create binary expression.
 */
AstNode *ast_binary(TokenType op, AstNode *left, AstNode *right, int line);

/**
 * Create unary expression.
 */
AstNode *ast_unary(TokenType op, AstNode *operand, int line);

/**
 * Create integer literal.
 */
AstNode *ast_int(int64_t value, int line);

/**
 * Create float literal.
 */
AstNode *ast_float(double value, int line);

/**
 * Create string literal.
 */
AstNode *ast_string(const char *value, size_t length, int line);

/**
 * Create boolean literal.
 */
AstNode *ast_bool(bool value, int line);

/**
 * Create nil literal.
 */
AstNode *ast_nil(int line);

/**
 * Create identifier.
 */
AstNode *ast_ident(const char *name, size_t length, int line);

/*============================================================================
 * Type Node Helpers
 *============================================================================*/

/**
 * Create type name node (int, string, bool, CustomType).
 */
AstNode *ast_type_name(const char *name, size_t length, int line);

/**
 * Create generic type node (Option<T>, Result<T, E>).
 */
AstNode *ast_type_generic(const char *name, AstNode **type_args, size_t arg_count, int line);

/**
 * Create array type node ([T]).
 */
AstNode *ast_type_array(AstNode *elem_type, int line);

/**
 * Create map type node (map<K, V>).
 */
AstNode *ast_type_map(AstNode *key_type, AstNode *value_type, int line);

/**
 * Create function type node (fn(A, B) -> C).
 */
AstNode *ast_type_func(AstNode **param_types, size_t param_count, AstNode *return_type, int line);

/*============================================================================
 * Struct/Enum Helpers
 *============================================================================*/

/**
 * Create struct declaration.
 */
AstNode *ast_struct_decl(const char *name, int line);

/**
 * Add field to struct declaration.
 */
void ast_struct_add_field(AstNode *struct_decl, const char *name, AstNode *type_ann, int line);

/**
 * Create enum declaration.
 */
AstNode *ast_enum_decl(const char *name, int line);

/**
 * Add variant to enum declaration.
 */
void ast_enum_add_variant(AstNode *enum_decl, const char *name, AstNode *payload_type, int line);

/**
 * Create type alias.
 */
AstNode *ast_type_alias(const char *name, AstNode *aliased, int line);

/**
 * Create struct initialization.
 */
AstNode *ast_struct_init(const char *type_name, int line);

/**
 * Add field to struct initialization.
 */
void ast_struct_init_add_field(AstNode *init, const char *name, AstNode *value);

/**
 * Set spread on struct initialization.
 */
void ast_struct_init_set_spread(AstNode *init, AstNode *spread);

/**
 * Create some(value) expression.
 */
AstNode *ast_some(AstNode *value, int line);

/**
 * Create none expression.
 */
AstNode *ast_none(int line);

/**
 * Create spread expression (...expr).
 */
AstNode *ast_spread(AstNode *expr, int line);

/*============================================================================
 * Debug
 *============================================================================*/

/**
 * Print AST for debugging.
 */
void ast_print(AstNode *node, int indent);

/**
 * Get string name of node type.
 */
const char *ast_node_type_name(NodeType type);

#endif /* AGIM_LANG_AST_H */
