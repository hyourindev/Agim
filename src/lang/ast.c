/*
 * Agim - AST Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/ast.h"
#include "util/alloc.h"

#include <stdio.h>
#include <string.h>

/*============================================================================
 * Node Type Names
 *============================================================================*/

const char *ast_node_type_name(NodeType type) {
    static const char *names[] = {
        [NODE_PROGRAM] = "PROGRAM",
        [NODE_TOOL_DECL] = "TOOL_DECL",
        [NODE_FN_DECL] = "FN_DECL",
        [NODE_PARAM] = "PARAM",
        [NODE_IMPORT] = "IMPORT",
        [NODE_IMPORT_FROM] = "IMPORT_FROM",
        [NODE_EXPORT] = "EXPORT",
        [NODE_STRUCT_DECL] = "STRUCT_DECL",
        [NODE_STRUCT_FIELD] = "STRUCT_FIELD",
        [NODE_ENUM_DECL] = "ENUM_DECL",
        [NODE_ENUM_VARIANT] = "ENUM_VARIANT",
        [NODE_TYPE_ALIAS] = "TYPE_ALIAS",
        [NODE_BLOCK] = "BLOCK",
        [NODE_LET] = "LET",
        [NODE_CONST] = "CONST",
        [NODE_IF] = "IF",
        [NODE_FOR] = "FOR",
        [NODE_WHILE] = "WHILE",
        [NODE_RETURN] = "RETURN",
        [NODE_BREAK] = "BREAK",
        [NODE_CONTINUE] = "CONTINUE",
        [NODE_EXPR_STMT] = "EXPR_STMT",
        [NODE_TYPE_NAME] = "TYPE_NAME",
        [NODE_TYPE_GENERIC] = "TYPE_GENERIC",
        [NODE_TYPE_ARRAY] = "TYPE_ARRAY",
        [NODE_TYPE_MAP] = "TYPE_MAP",
        [NODE_TYPE_FUNC] = "TYPE_FUNC",
        [NODE_BINARY] = "BINARY",
        [NODE_UNARY] = "UNARY",
        [NODE_CALL] = "CALL",
        [NODE_MEMBER] = "MEMBER",
        [NODE_INDEX] = "INDEX",
        [NODE_TERNARY] = "TERNARY",
        [NODE_ASSIGN] = "ASSIGN",
        [NODE_IDENT] = "IDENT",
        [NODE_INT] = "INT",
        [NODE_FLOAT] = "FLOAT",
        [NODE_STRING] = "STRING",
        [NODE_BOOL] = "BOOL",
        [NODE_NIL] = "NIL",
        [NODE_ARRAY] = "ARRAY",
        [NODE_MAP] = "MAP",
        [NODE_MATCH] = "MATCH",
        [NODE_MATCH_ARM] = "MATCH_ARM",
        [NODE_RESULT_OK] = "RESULT_OK",
        [NODE_RESULT_ERR] = "RESULT_ERR",
        [NODE_TRY] = "TRY",
        [NODE_SOME] = "SOME",
        [NODE_NONE] = "NONE",
        [NODE_STRUCT_INIT] = "STRUCT_INIT",
        [NODE_SPREAD] = "SPREAD",
        [NODE_ENUM_EXPR] = "ENUM_EXPR",
    };
    return names[type];
}

/*============================================================================
 * Node Creation
 *============================================================================*/

AstNode *ast_new(NodeType type, int line) {
    AstNode *node = agim_alloc(sizeof(AstNode));
    memset(node, 0, sizeof(AstNode));
    node->type = type;
    node->line = line;
    return node;
}

/*============================================================================
 * Node Destruction
 *============================================================================*/

void ast_free(AstNode *node) {
    if (!node) return;

    switch (node->type) {
    case NODE_PROGRAM:
        for (size_t i = 0; i < node->as.program.count; i++) {
            ast_free(node->as.program.decls[i]);
        }
        agim_free(node->as.program.decls);
        break;

    case NODE_TOOL_DECL:
    case NODE_FN_DECL:
        agim_free(node->as.fn_decl.name);
        for (size_t i = 0; i < node->as.fn_decl.param_count; i++) {
            ast_free(node->as.fn_decl.params[i]);
        }
        agim_free(node->as.fn_decl.params);
        ast_free(node->as.fn_decl.return_type);
        agim_free(node->as.fn_decl.description);
        ast_free(node->as.fn_decl.body);
        break;

    case NODE_PARAM:
        agim_free(node->as.param.name);
        ast_free(node->as.param.type_ann);
        break;

    case NODE_BLOCK:
        for (size_t i = 0; i < node->as.block.count; i++) {
            ast_free(node->as.block.stmts[i]);
        }
        agim_free(node->as.block.stmts);
        break;

    case NODE_LET:
    case NODE_CONST:
        agim_free(node->as.var_decl.name);
        ast_free(node->as.var_decl.type_ann);
        ast_free(node->as.var_decl.value);
        break;

    case NODE_IF:
        ast_free(node->as.if_stmt.cond);
        ast_free(node->as.if_stmt.then_block);
        ast_free(node->as.if_stmt.else_block);
        break;

    case NODE_FOR:
        agim_free(node->as.for_stmt.var);
        agim_free(node->as.for_stmt.index_var);
        ast_free(node->as.for_stmt.iterable);
        ast_free(node->as.for_stmt.body);
        break;

    case NODE_WHILE:
        ast_free(node->as.while_stmt.cond);
        ast_free(node->as.while_stmt.body);
        break;

    case NODE_RETURN:
        ast_free(node->as.return_stmt.value);
        break;

    case NODE_EXPR_STMT:
        ast_free(node->as.return_stmt.value); /* Same layout */
        break;

    case NODE_BINARY:
        ast_free(node->as.binary.left);
        ast_free(node->as.binary.right);
        break;

    case NODE_UNARY:
        ast_free(node->as.unary.operand);
        break;

    case NODE_CALL:
        ast_free(node->as.call.callee);
        for (size_t i = 0; i < node->as.call.arg_count; i++) {
            ast_free(node->as.call.args[i]);
        }
        agim_free(node->as.call.args);
        break;

    case NODE_MEMBER:
        ast_free(node->as.member.object);
        agim_free(node->as.member.field);
        break;

    case NODE_INDEX:
        ast_free(node->as.index_expr.object);
        ast_free(node->as.index_expr.index);
        break;

    case NODE_TERNARY:
        ast_free(node->as.ternary.cond);
        ast_free(node->as.ternary.then_expr);
        ast_free(node->as.ternary.else_expr);
        break;

    case NODE_ASSIGN:
        ast_free(node->as.assign.target);
        ast_free(node->as.assign.value);
        break;

    case NODE_IDENT:
        agim_free(node->as.ident.name);
        break;

    case NODE_STRING:
        agim_free(node->as.string_val);
        break;

    case NODE_ARRAY:
        for (size_t i = 0; i < node->as.array.count; i++) {
            ast_free(node->as.array.elements[i]);
        }
        agim_free(node->as.array.elements);
        break;

    case NODE_MAP:
        for (size_t i = 0; i < node->as.map.count; i++) {
            agim_free(node->as.map.keys[i]);
            ast_free(node->as.map.values[i]);
        }
        agim_free(node->as.map.keys);
        agim_free(node->as.map.values);
        break;

    case NODE_INT:
    case NODE_FLOAT:
    case NODE_BOOL:
    case NODE_NIL:
    case NODE_BREAK:
    case NODE_CONTINUE:
        /* No additional allocations */
        break;

    case NODE_IMPORT:
        agim_free(node->as.import_stmt.path);
        break;

    case NODE_IMPORT_FROM:
        for (size_t i = 0; i < node->as.import_from.name_count; i++) {
            agim_free(node->as.import_from.names[i]);
        }
        agim_free(node->as.import_from.names);
        agim_free(node->as.import_from.path);
        break;

    case NODE_EXPORT:
        ast_free(node->as.export_stmt.decl);
        break;

    case NODE_MATCH:
        ast_free(node->as.match_expr.expr);
        for (size_t i = 0; i < node->as.match_expr.arm_count; i++) {
            ast_free(node->as.match_expr.arms[i]);
        }
        agim_free(node->as.match_expr.arms);
        break;

    case NODE_MATCH_ARM:
        agim_free(node->as.match_arm.binding_name);
        agim_free(node->as.match_arm.variant_name);
        ast_free(node->as.match_arm.body);
        break;

    case NODE_RESULT_OK:
    case NODE_RESULT_ERR:
        ast_free(node->as.result_expr.value);
        break;

    case NODE_TRY:
        ast_free(node->as.try_expr.expr);
        break;

    case NODE_STRUCT_DECL:
        agim_free(node->as.struct_decl.name);
        for (size_t i = 0; i < node->as.struct_decl.field_count; i++) {
            ast_free(node->as.struct_decl.fields[i]);
        }
        agim_free(node->as.struct_decl.fields);
        break;

    case NODE_STRUCT_FIELD:
        agim_free(node->as.struct_field.name);
        ast_free(node->as.struct_field.type_ann);
        break;

    case NODE_ENUM_DECL:
        agim_free(node->as.enum_decl.name);
        for (size_t i = 0; i < node->as.enum_decl.variant_count; i++) {
            ast_free(node->as.enum_decl.variants[i]);
        }
        agim_free(node->as.enum_decl.variants);
        break;

    case NODE_ENUM_VARIANT:
        agim_free(node->as.enum_variant.name);
        ast_free(node->as.enum_variant.payload_type);
        break;

    case NODE_TYPE_ALIAS:
        agim_free(node->as.type_alias.name);
        ast_free(node->as.type_alias.aliased);
        break;

    case NODE_TYPE_NAME:
        agim_free(node->as.type_name.name);
        break;

    case NODE_TYPE_GENERIC:
        agim_free(node->as.type_generic.name);
        for (size_t i = 0; i < node->as.type_generic.arg_count; i++) {
            ast_free(node->as.type_generic.type_args[i]);
        }
        agim_free(node->as.type_generic.type_args);
        break;

    case NODE_TYPE_ARRAY:
        ast_free(node->as.type_array.elem_type);
        break;

    case NODE_TYPE_MAP:
        ast_free(node->as.type_map.key_type);
        ast_free(node->as.type_map.value_type);
        break;

    case NODE_TYPE_FUNC:
        for (size_t i = 0; i < node->as.type_func.param_count; i++) {
            ast_free(node->as.type_func.param_types[i]);
        }
        agim_free(node->as.type_func.param_types);
        ast_free(node->as.type_func.return_type);
        break;

    case NODE_SOME:
        ast_free(node->as.some_expr.value);
        break;

    case NODE_NONE:
        /* No allocations */
        break;

    case NODE_STRUCT_INIT:
        agim_free(node->as.struct_init.type_name);
        for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
            agim_free(node->as.struct_init.field_names[i]);
            ast_free(node->as.struct_init.field_values[i]);
        }
        agim_free(node->as.struct_init.field_names);
        agim_free(node->as.struct_init.field_values);
        ast_free(node->as.struct_init.spread);
        break;

    case NODE_SPREAD:
        ast_free(node->as.spread_expr.expr);
        break;

    case NODE_ENUM_EXPR:
        agim_free(node->as.enum_expr.enum_type);
        agim_free(node->as.enum_expr.variant_name);
        ast_free(node->as.enum_expr.payload);
        break;
    }

    agim_free(node);
}

/*============================================================================
 * Node Helpers
 *============================================================================*/

AstNode *ast_program(int line) {
    AstNode *node = ast_new(NODE_PROGRAM, line);
    node->as.program.decls = NULL;
    node->as.program.count = 0;
    node->as.program.capacity = 0;
    return node;
}

void ast_program_add(AstNode *program, AstNode *decl) {
    if (program->as.program.count >= program->as.program.capacity) {
        size_t new_cap = program->as.program.capacity == 0 ? 8 : program->as.program.capacity * 2;
        program->as.program.decls = agim_realloc(
            program->as.program.decls,
            sizeof(AstNode *) * new_cap
        );
        program->as.program.capacity = new_cap;
    }
    program->as.program.decls[program->as.program.count++] = decl;
}

AstNode *ast_block(int line) {
    AstNode *node = ast_new(NODE_BLOCK, line);
    node->as.block.stmts = NULL;
    node->as.block.count = 0;
    node->as.block.capacity = 0;
    return node;
}

void ast_block_add(AstNode *block, AstNode *stmt) {
    if (block->as.block.count >= block->as.block.capacity) {
        size_t new_cap = block->as.block.capacity == 0 ? 8 : block->as.block.capacity * 2;
        block->as.block.stmts = agim_realloc(
            block->as.block.stmts,
            sizeof(AstNode *) * new_cap
        );
        block->as.block.capacity = new_cap;
    }
    block->as.block.stmts[block->as.block.count++] = stmt;
}

AstNode *ast_binary(TokenType op, AstNode *left, AstNode *right, int line) {
    AstNode *node = ast_new(NODE_BINARY, line);
    node->as.binary.op = op;
    node->as.binary.left = left;
    node->as.binary.right = right;
    return node;
}

AstNode *ast_unary(TokenType op, AstNode *operand, int line) {
    AstNode *node = ast_new(NODE_UNARY, line);
    node->as.unary.op = op;
    node->as.unary.operand = operand;
    return node;
}

AstNode *ast_int(int64_t value, int line) {
    AstNode *node = ast_new(NODE_INT, line);
    node->as.int_val = value;
    return node;
}

AstNode *ast_float(double value, int line) {
    AstNode *node = ast_new(NODE_FLOAT, line);
    node->as.float_val = value;
    return node;
}

AstNode *ast_string(const char *value, size_t length, int line) {
    AstNode *node = ast_new(NODE_STRING, line);
    /* Skip quotes and handle escapes */
    char *str = agim_alloc(length + 1);
    size_t j = 0;
    for (size_t i = 0; i < length; i++) {
        if (value[i] == '\\' && i + 1 < length) {
            i++;
            switch (value[i]) {
            case 'n': str[j++] = '\n'; break;
            case 't': str[j++] = '\t'; break;
            case 'r': str[j++] = '\r'; break;
            case '\\': str[j++] = '\\'; break;
            case '"': str[j++] = '"'; break;
            default: str[j++] = value[i]; break;
            }
        } else {
            str[j++] = value[i];
        }
    }
    str[j] = '\0';
    node->as.string_val = str;
    return node;
}

AstNode *ast_bool(bool value, int line) {
    AstNode *node = ast_new(NODE_BOOL, line);
    node->as.bool_val = value;
    return node;
}

AstNode *ast_nil(int line) {
    return ast_new(NODE_NIL, line);
}

AstNode *ast_ident(const char *name, size_t length, int line) {
    AstNode *node = ast_new(NODE_IDENT, line);
    node->as.ident.name = agim_alloc(length + 1);
    memcpy(node->as.ident.name, name, length);
    node->as.ident.name[length] = '\0';
    return node;
}

/*============================================================================
 * Type Node Helpers
 *============================================================================*/

AstNode *ast_type_name(const char *name, size_t length, int line) {
    AstNode *node = ast_new(NODE_TYPE_NAME, line);
    node->as.type_name.name = agim_alloc(length + 1);
    memcpy(node->as.type_name.name, name, length);
    node->as.type_name.name[length] = '\0';
    return node;
}

AstNode *ast_type_generic(const char *name, AstNode **type_args, size_t arg_count, int line) {
    AstNode *node = ast_new(NODE_TYPE_GENERIC, line);
    size_t name_len = strlen(name);
    node->as.type_generic.name = agim_alloc(name_len + 1);
    memcpy(node->as.type_generic.name, name, name_len + 1);
    node->as.type_generic.type_args = type_args;
    node->as.type_generic.arg_count = arg_count;
    return node;
}

AstNode *ast_type_array(AstNode *elem_type, int line) {
    AstNode *node = ast_new(NODE_TYPE_ARRAY, line);
    node->as.type_array.elem_type = elem_type;
    return node;
}

AstNode *ast_type_map(AstNode *key_type, AstNode *value_type, int line) {
    AstNode *node = ast_new(NODE_TYPE_MAP, line);
    node->as.type_map.key_type = key_type;
    node->as.type_map.value_type = value_type;
    return node;
}

AstNode *ast_type_func(AstNode **param_types, size_t param_count, AstNode *return_type, int line) {
    AstNode *node = ast_new(NODE_TYPE_FUNC, line);
    node->as.type_func.param_types = param_types;
    node->as.type_func.param_count = param_count;
    node->as.type_func.return_type = return_type;
    return node;
}

/*============================================================================
 * Struct/Enum Helpers
 *============================================================================*/

AstNode *ast_struct_decl(const char *name, int line) {
    AstNode *node = ast_new(NODE_STRUCT_DECL, line);
    size_t len = strlen(name);
    node->as.struct_decl.name = agim_alloc(len + 1);
    memcpy(node->as.struct_decl.name, name, len + 1);
    node->as.struct_decl.fields = NULL;
    node->as.struct_decl.field_count = 0;
    return node;
}

void ast_struct_add_field(AstNode *struct_decl, const char *name, AstNode *type_ann, int line) {
    AstNode *field = ast_new(NODE_STRUCT_FIELD, line);
    size_t name_len = strlen(name);
    field->as.struct_field.name = agim_alloc(name_len + 1);
    memcpy(field->as.struct_field.name, name, name_len + 1);
    field->as.struct_field.type_ann = type_ann;

    size_t count = struct_decl->as.struct_decl.field_count;
    struct_decl->as.struct_decl.fields = agim_realloc(
        struct_decl->as.struct_decl.fields,
        sizeof(AstNode *) * (count + 1)
    );
    struct_decl->as.struct_decl.fields[count] = field;
    struct_decl->as.struct_decl.field_count = count + 1;
}

AstNode *ast_enum_decl(const char *name, int line) {
    AstNode *node = ast_new(NODE_ENUM_DECL, line);
    size_t len = strlen(name);
    node->as.enum_decl.name = agim_alloc(len + 1);
    memcpy(node->as.enum_decl.name, name, len + 1);
    node->as.enum_decl.variants = NULL;
    node->as.enum_decl.variant_count = 0;
    return node;
}

void ast_enum_add_variant(AstNode *enum_decl, const char *name, AstNode *payload_type, int line) {
    AstNode *variant = ast_new(NODE_ENUM_VARIANT, line);
    size_t name_len = strlen(name);
    variant->as.enum_variant.name = agim_alloc(name_len + 1);
    memcpy(variant->as.enum_variant.name, name, name_len + 1);
    variant->as.enum_variant.payload_type = payload_type;

    size_t count = enum_decl->as.enum_decl.variant_count;
    enum_decl->as.enum_decl.variants = agim_realloc(
        enum_decl->as.enum_decl.variants,
        sizeof(AstNode *) * (count + 1)
    );
    enum_decl->as.enum_decl.variants[count] = variant;
    enum_decl->as.enum_decl.variant_count = count + 1;
}

AstNode *ast_type_alias(const char *name, AstNode *aliased, int line) {
    AstNode *node = ast_new(NODE_TYPE_ALIAS, line);
    size_t len = strlen(name);
    node->as.type_alias.name = agim_alloc(len + 1);
    memcpy(node->as.type_alias.name, name, len + 1);
    node->as.type_alias.aliased = aliased;
    return node;
}

AstNode *ast_struct_init(const char *type_name, int line) {
    AstNode *node = ast_new(NODE_STRUCT_INIT, line);
    size_t len = strlen(type_name);
    node->as.struct_init.type_name = agim_alloc(len + 1);
    memcpy(node->as.struct_init.type_name, type_name, len + 1);
    node->as.struct_init.field_names = NULL;
    node->as.struct_init.field_values = NULL;
    node->as.struct_init.field_count = 0;
    node->as.struct_init.spread = NULL;
    return node;
}

void ast_struct_init_add_field(AstNode *init, const char *name, AstNode *value) {
    size_t count = init->as.struct_init.field_count;
    init->as.struct_init.field_names = agim_realloc(
        init->as.struct_init.field_names,
        sizeof(char *) * (count + 1)
    );
    init->as.struct_init.field_values = agim_realloc(
        init->as.struct_init.field_values,
        sizeof(AstNode *) * (count + 1)
    );

    size_t name_len = strlen(name);
    init->as.struct_init.field_names[count] = agim_alloc(name_len + 1);
    memcpy(init->as.struct_init.field_names[count], name, name_len + 1);
    init->as.struct_init.field_values[count] = value;
    init->as.struct_init.field_count = count + 1;
}

void ast_struct_init_set_spread(AstNode *init, AstNode *spread) {
    init->as.struct_init.spread = spread;
}

AstNode *ast_some(AstNode *value, int line) {
    AstNode *node = ast_new(NODE_SOME, line);
    node->as.some_expr.value = value;
    return node;
}

AstNode *ast_none(int line) {
    return ast_new(NODE_NONE, line);
}

AstNode *ast_spread(AstNode *expr, int line) {
    AstNode *node = ast_new(NODE_SPREAD, line);
    node->as.spread_expr.expr = expr;
    return node;
}

AstNode *ast_enum_variant(const char *enum_type, const char *variant_name, AstNode *payload, int line) {
    AstNode *node = ast_new(NODE_ENUM_EXPR, line);
    size_t type_len = strlen(enum_type);
    size_t var_len = strlen(variant_name);
    node->as.enum_expr.enum_type = agim_alloc(type_len + 1);
    memcpy(node->as.enum_expr.enum_type, enum_type, type_len + 1);
    node->as.enum_expr.variant_name = agim_alloc(var_len + 1);
    memcpy(node->as.enum_expr.variant_name, variant_name, var_len + 1);
    node->as.enum_expr.payload = payload;
    return node;
}

AstNode *ast_range(AstNode *start, AstNode *end, bool inclusive, int line) {
    AstNode *node = ast_new(NODE_RANGE, line);
    node->as.range.start = start;
    node->as.range.end = end;
    node->as.range.inclusive = inclusive;
    return node;
}

/*============================================================================
 * Debug Printing
 *============================================================================*/

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

void ast_print(AstNode *node, int indent) {
    if (!node) {
        print_indent(indent);
        printf("(nil)\n");
        return;
    }

    print_indent(indent);
    printf("%s", ast_node_type_name(node->type));

    switch (node->type) {
    case NODE_PROGRAM:
        printf(" (%zu decls)\n", node->as.program.count);
        for (size_t i = 0; i < node->as.program.count; i++) {
            ast_print(node->as.program.decls[i], indent + 1);
        }
        break;

    case NODE_TOOL_DECL:
    case NODE_FN_DECL:
        printf(" %s (", node->as.fn_decl.name);
        for (size_t i = 0; i < node->as.fn_decl.param_count; i++) {
            if (i > 0) printf(", ");
            printf("%s", node->as.fn_decl.params[i]->as.param.name);
            if (node->as.fn_decl.params[i]->as.param.type_ann) {
                printf(": <type>");
            }
        }
        printf(")");
        if (node->as.fn_decl.return_type) {
            printf(" -> <type>");
        }
        printf("\n");
        ast_print(node->as.fn_decl.body, indent + 1);
        break;

    case NODE_BLOCK:
        printf(" (%zu stmts)\n", node->as.block.count);
        for (size_t i = 0; i < node->as.block.count; i++) {
            ast_print(node->as.block.stmts[i], indent + 1);
        }
        break;

    case NODE_LET:
    case NODE_CONST:
        printf(" %s\n", node->as.var_decl.name);
        ast_print(node->as.var_decl.value, indent + 1);
        break;

    case NODE_IF:
        printf("\n");
        print_indent(indent + 1);
        printf("condition:\n");
        ast_print(node->as.if_stmt.cond, indent + 2);
        print_indent(indent + 1);
        printf("then:\n");
        ast_print(node->as.if_stmt.then_block, indent + 2);
        if (node->as.if_stmt.else_block) {
            print_indent(indent + 1);
            printf("else:\n");
            ast_print(node->as.if_stmt.else_block, indent + 2);
        }
        break;

    case NODE_WHILE:
        printf("\n");
        print_indent(indent + 1);
        printf("condition:\n");
        ast_print(node->as.while_stmt.cond, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        ast_print(node->as.while_stmt.body, indent + 2);
        break;

    case NODE_FOR:
        printf(" %s", node->as.for_stmt.var);
        if (node->as.for_stmt.index_var) {
            printf(", %s", node->as.for_stmt.index_var);
        }
        printf(" in\n");
        ast_print(node->as.for_stmt.iterable, indent + 1);
        ast_print(node->as.for_stmt.body, indent + 1);
        break;

    case NODE_RETURN:
        printf("\n");
        if (node->as.return_stmt.value) {
            ast_print(node->as.return_stmt.value, indent + 1);
        }
        break;

    case NODE_BINARY:
        printf(" %s\n", token_type_name(node->as.binary.op));
        ast_print(node->as.binary.left, indent + 1);
        ast_print(node->as.binary.right, indent + 1);
        break;

    case NODE_UNARY:
        printf(" %s\n", token_type_name(node->as.unary.op));
        ast_print(node->as.unary.operand, indent + 1);
        break;

    case NODE_CALL:
        printf("\n");
        print_indent(indent + 1);
        printf("callee:\n");
        ast_print(node->as.call.callee, indent + 2);
        print_indent(indent + 1);
        printf("args (%zu):\n", node->as.call.arg_count);
        for (size_t i = 0; i < node->as.call.arg_count; i++) {
            ast_print(node->as.call.args[i], indent + 2);
        }
        break;

    case NODE_MEMBER:
        printf(" .%s\n", node->as.member.field);
        ast_print(node->as.member.object, indent + 1);
        break;

    case NODE_INDEX:
        printf("\n");
        ast_print(node->as.index_expr.object, indent + 1);
        ast_print(node->as.index_expr.index, indent + 1);
        break;

    case NODE_ASSIGN:
        printf(" %s\n", token_type_name(node->as.assign.op));
        ast_print(node->as.assign.target, indent + 1);
        ast_print(node->as.assign.value, indent + 1);
        break;

    case NODE_IDENT:
        printf(" %s\n", node->as.ident.name);
        break;

    case NODE_INT:
        printf(" %ld\n", (long)node->as.int_val);
        break;

    case NODE_FLOAT:
        printf(" %g\n", node->as.float_val);
        break;

    case NODE_STRING:
        printf(" \"%s\"\n", node->as.string_val);
        break;

    case NODE_BOOL:
        printf(" %s\n", node->as.bool_val ? "true" : "false");
        break;

    case NODE_NIL:
        printf("\n");
        break;

    case NODE_ARRAY:
        printf(" (%zu elements)\n", node->as.array.count);
        for (size_t i = 0; i < node->as.array.count; i++) {
            ast_print(node->as.array.elements[i], indent + 1);
        }
        break;

    case NODE_MAP:
        printf(" (%zu entries)\n", node->as.map.count);
        for (size_t i = 0; i < node->as.map.count; i++) {
            print_indent(indent + 1);
            printf("%s:\n", node->as.map.keys[i]);
            ast_print(node->as.map.values[i], indent + 2);
        }
        break;

    case NODE_EXPR_STMT:
        printf("\n");
        ast_print(node->as.return_stmt.value, indent + 1);
        break;

    default:
        printf("\n");
        break;
    }
}
