/*
 * Agim - Type Checker Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/typechecker.h"
#include "util/alloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*============================================================================
 * Type Construction
 *============================================================================*/

static Type *type_new(TypeKind kind) {
    Type *t = agim_alloc(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = kind;
    return t;
}

Type *type_int(void) { return type_new(TYPE_INT); }
Type *type_float(void) { return type_new(TYPE_FLOAT); }
Type *type_string(void) { return type_new(TYPE_STRING); }
Type *type_bool(void) { return type_new(TYPE_BOOL); }
Type *type_void(void) { return type_new(TYPE_VOID); }
Type *type_bytes(void) { return type_new(TYPE_BYTES); }
Type *type_nil(void) { return type_new(TYPE_NIL); }
Type *type_any(void) { return type_new(TYPE_ANY); }
Type *type_pid(void) { return type_new(TYPE_PID); }

Type *type_array(Type *elem_type) {
    Type *t = type_new(TYPE_ARRAY);
    t->as.array.elem_type = elem_type;
    return t;
}

Type *type_map(Type *key_type, Type *value_type) {
    Type *t = type_new(TYPE_MAP);
    t->as.map.key_type = key_type;
    t->as.map.value_type = value_type;
    return t;
}

Type *type_option(Type *inner_type) {
    Type *t = type_new(TYPE_OPTION);
    t->as.option.inner_type = inner_type;
    return t;
}

Type *type_result(Type *ok_type, Type *err_type) {
    Type *t = type_new(TYPE_RESULT);
    t->as.result.ok_type = ok_type;
    t->as.result.err_type = err_type;
    return t;
}

Type *type_function(Type **param_types, size_t param_count, Type *return_type) {
    Type *t = type_new(TYPE_FUNCTION);
    t->as.func.param_types = param_types;
    t->as.func.param_count = param_count;
    t->as.func.return_type = return_type;
    return t;
}

void type_free(Type *type) {
    if (!type) return;

    switch (type->kind) {
    case TYPE_ARRAY:
        type_free(type->as.array.elem_type);
        break;
    case TYPE_MAP:
        type_free(type->as.map.key_type);
        type_free(type->as.map.value_type);
        break;
    case TYPE_OPTION:
        type_free(type->as.option.inner_type);
        break;
    case TYPE_RESULT:
        type_free(type->as.result.ok_type);
        type_free(type->as.result.err_type);
        break;
    case TYPE_STRUCT:
        agim_free(type->as.struct_type.name);
        for (size_t i = 0; i < type->as.struct_type.field_count; i++) {
            agim_free(type->as.struct_type.field_names[i]);
            type_free(type->as.struct_type.field_types[i]);
        }
        agim_free(type->as.struct_type.field_names);
        agim_free(type->as.struct_type.field_types);
        break;
    case TYPE_ENUM:
        agim_free(type->as.enum_type.name);
        for (size_t i = 0; i < type->as.enum_type.variant_count; i++) {
            agim_free(type->as.enum_type.variant_names[i]);
            type_free(type->as.enum_type.variant_payloads[i]);
        }
        agim_free(type->as.enum_type.variant_names);
        agim_free(type->as.enum_type.variant_payloads);
        break;
    case TYPE_FUNCTION:
        for (size_t i = 0; i < type->as.func.param_count; i++) {
            type_free(type->as.func.param_types[i]);
        }
        agim_free(type->as.func.param_types);
        type_free(type->as.func.return_type);
        break;
    default:
        break;
    }

    agim_free(type);
}

Type *type_clone(Type *type) {
    if (!type) return NULL;

    Type *clone = type_new(type->kind);

    switch (type->kind) {
    case TYPE_ARRAY:
        clone->as.array.elem_type = type_clone(type->as.array.elem_type);
        break;
    case TYPE_MAP:
        clone->as.map.key_type = type_clone(type->as.map.key_type);
        clone->as.map.value_type = type_clone(type->as.map.value_type);
        break;
    case TYPE_OPTION:
        clone->as.option.inner_type = type_clone(type->as.option.inner_type);
        break;
    case TYPE_RESULT:
        clone->as.result.ok_type = type_clone(type->as.result.ok_type);
        clone->as.result.err_type = type_clone(type->as.result.err_type);
        break;
    case TYPE_STRUCT:
        clone->as.struct_type.name = agim_alloc(strlen(type->as.struct_type.name) + 1);
        strcpy(clone->as.struct_type.name, type->as.struct_type.name);
        clone->as.struct_type.field_count = type->as.struct_type.field_count;
        clone->as.struct_type.field_names = agim_alloc(sizeof(char *) * clone->as.struct_type.field_count);
        clone->as.struct_type.field_types = agim_alloc(sizeof(Type *) * clone->as.struct_type.field_count);
        for (size_t i = 0; i < clone->as.struct_type.field_count; i++) {
            clone->as.struct_type.field_names[i] = agim_alloc(strlen(type->as.struct_type.field_names[i]) + 1);
            strcpy(clone->as.struct_type.field_names[i], type->as.struct_type.field_names[i]);
            clone->as.struct_type.field_types[i] = type_clone(type->as.struct_type.field_types[i]);
        }
        break;
    case TYPE_ENUM:
        clone->as.enum_type.name = agim_alloc(strlen(type->as.enum_type.name) + 1);
        strcpy(clone->as.enum_type.name, type->as.enum_type.name);
        clone->as.enum_type.variant_count = type->as.enum_type.variant_count;
        clone->as.enum_type.variant_names = agim_alloc(sizeof(char *) * clone->as.enum_type.variant_count);
        clone->as.enum_type.variant_payloads = agim_alloc(sizeof(Type *) * clone->as.enum_type.variant_count);
        for (size_t i = 0; i < clone->as.enum_type.variant_count; i++) {
            clone->as.enum_type.variant_names[i] = agim_alloc(strlen(type->as.enum_type.variant_names[i]) + 1);
            strcpy(clone->as.enum_type.variant_names[i], type->as.enum_type.variant_names[i]);
            clone->as.enum_type.variant_payloads[i] = type_clone(type->as.enum_type.variant_payloads[i]);
        }
        break;
    case TYPE_FUNCTION:
        clone->as.func.param_count = type->as.func.param_count;
        clone->as.func.param_types = agim_alloc(sizeof(Type *) * clone->as.func.param_count);
        for (size_t i = 0; i < clone->as.func.param_count; i++) {
            clone->as.func.param_types[i] = type_clone(type->as.func.param_types[i]);
        }
        clone->as.func.return_type = type_clone(type->as.func.return_type);
        break;
    default:
        break;
    }

    return clone;
}

bool type_equals(Type *a, Type *b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return false;

    /* ANY matches anything */
    if (a->kind == TYPE_ANY || b->kind == TYPE_ANY) return true;

    switch (a->kind) {
    case TYPE_ARRAY:
        return type_equals(a->as.array.elem_type, b->as.array.elem_type);
    case TYPE_MAP:
        return type_equals(a->as.map.key_type, b->as.map.key_type) &&
               type_equals(a->as.map.value_type, b->as.map.value_type);
    case TYPE_OPTION:
        return type_equals(a->as.option.inner_type, b->as.option.inner_type);
    case TYPE_RESULT:
        return type_equals(a->as.result.ok_type, b->as.result.ok_type) &&
               type_equals(a->as.result.err_type, b->as.result.err_type);
    case TYPE_STRUCT:
        return strcmp(a->as.struct_type.name, b->as.struct_type.name) == 0;
    case TYPE_ENUM:
        return strcmp(a->as.enum_type.name, b->as.enum_type.name) == 0;
    case TYPE_FUNCTION:
        if (a->as.func.param_count != b->as.func.param_count) return false;
        for (size_t i = 0; i < a->as.func.param_count; i++) {
            if (!type_equals(a->as.func.param_types[i], b->as.func.param_types[i])) {
                return false;
            }
        }
        return type_equals(a->as.func.return_type, b->as.func.return_type);
    default:
        return true;
    }
}

bool type_assignable(Type *to, Type *from) {
    if (!to || !from) return false;

    /* ANY can be assigned to/from anything */
    if (to->kind == TYPE_ANY || from->kind == TYPE_ANY) return true;

    /* nil can be assigned to Option types */
    if (from->kind == TYPE_NIL && to->kind == TYPE_OPTION) return true;

    /* Exact type match */
    return type_equals(to, from);
}

const char *type_to_string(Type *type) {
    static char buffer[256];

    if (!type) {
        return "unknown";
    }

    switch (type->kind) {
    case TYPE_INT: return "int";
    case TYPE_FLOAT: return "float";
    case TYPE_STRING: return "string";
    case TYPE_BOOL: return "bool";
    case TYPE_VOID: return "void";
    case TYPE_BYTES: return "bytes";
    case TYPE_NIL: return "nil";
    case TYPE_ANY: return "any";
    case TYPE_PID: return "Pid";
    case TYPE_UNKNOWN: return "?";
    case TYPE_ARRAY:
        snprintf(buffer, sizeof(buffer), "[%s]", type_to_string(type->as.array.elem_type));
        return buffer;
    case TYPE_MAP:
        snprintf(buffer, sizeof(buffer), "map<%s, %s>",
                 type_to_string(type->as.map.key_type),
                 type_to_string(type->as.map.value_type));
        return buffer;
    case TYPE_OPTION:
        snprintf(buffer, sizeof(buffer), "Option<%s>",
                 type_to_string(type->as.option.inner_type));
        return buffer;
    case TYPE_RESULT:
        snprintf(buffer, sizeof(buffer), "Result<%s, %s>",
                 type_to_string(type->as.result.ok_type),
                 type_to_string(type->as.result.err_type));
        return buffer;
    case TYPE_STRUCT:
        return type->as.struct_type.name;
    case TYPE_ENUM:
        return type->as.enum_type.name;
    case TYPE_FUNCTION:
        snprintf(buffer, sizeof(buffer), "fn(...) -> %s",
                 type->as.func.return_type ? type_to_string(type->as.func.return_type) : "void");
        return buffer;
    }
    return "unknown";
}

/*============================================================================
 * Type Environment
 *============================================================================*/

typedef struct VarEntry {
    char *name;
    Type *type;
    bool is_mutable;
    struct VarEntry *next;
} VarEntry;

typedef struct Scope {
    VarEntry *vars;
    struct Scope *parent;
} Scope;

typedef struct TypeEntry {
    char *name;
    Type *type;
    struct TypeEntry *next;
} TypeEntry;

struct TypeEnv {
    Scope *current;
    TypeEntry *structs;
    TypeEntry *enums;
    TypeEntry *funcs;
};

TypeEnv *type_env_new(void) {
    TypeEnv *env = agim_alloc(sizeof(TypeEnv));
    env->current = agim_alloc(sizeof(Scope));
    env->current->vars = NULL;
    env->current->parent = NULL;
    env->structs = NULL;
    env->enums = NULL;
    env->funcs = NULL;
    return env;
}

static void free_var_list(VarEntry *vars) {
    while (vars) {
        VarEntry *next = vars->next;
        agim_free(vars->name);
        type_free(vars->type);
        agim_free(vars);
        vars = next;
    }
}

static void free_type_list(TypeEntry *entries) {
    while (entries) {
        TypeEntry *next = entries->next;
        agim_free(entries->name);
        type_free(entries->type);
        agim_free(entries);
        entries = next;
    }
}

void type_env_free(TypeEnv *env) {
    if (!env) return;

    while (env->current) {
        Scope *parent = env->current->parent;
        free_var_list(env->current->vars);
        agim_free(env->current);
        env->current = parent;
    }

    free_type_list(env->structs);
    free_type_list(env->enums);
    free_type_list(env->funcs);
    agim_free(env);
}

void type_env_push_scope(TypeEnv *env) {
    Scope *scope = agim_alloc(sizeof(Scope));
    scope->vars = NULL;
    scope->parent = env->current;
    env->current = scope;
}

void type_env_pop_scope(TypeEnv *env) {
    if (!env->current || !env->current->parent) return;

    Scope *old = env->current;
    env->current = old->parent;
    free_var_list(old->vars);
    agim_free(old);
}

void type_env_define(TypeEnv *env, const char *name, Type *type, bool is_mutable) {
    VarEntry *entry = agim_alloc(sizeof(VarEntry));
    entry->name = agim_alloc(strlen(name) + 1);
    strcpy(entry->name, name);
    entry->type = type;
    entry->is_mutable = is_mutable;
    entry->next = env->current->vars;
    env->current->vars = entry;
}

Type *type_env_lookup(TypeEnv *env, const char *name) {
    for (Scope *scope = env->current; scope; scope = scope->parent) {
        for (VarEntry *v = scope->vars; v; v = v->next) {
            if (strcmp(v->name, name) == 0) {
                return v->type;
            }
        }
    }
    return NULL;
}

bool type_env_is_mutable(TypeEnv *env, const char *name) {
    for (Scope *scope = env->current; scope; scope = scope->parent) {
        for (VarEntry *v = scope->vars; v; v = v->next) {
            if (strcmp(v->name, name) == 0) {
                return v->is_mutable;
            }
        }
    }
    return false;
}

void type_env_define_struct(TypeEnv *env, const char *name, Type *type) {
    TypeEntry *entry = agim_alloc(sizeof(TypeEntry));
    entry->name = agim_alloc(strlen(name) + 1);
    strcpy(entry->name, name);
    entry->type = type;
    entry->next = env->structs;
    env->structs = entry;
}

Type *type_env_lookup_struct(TypeEnv *env, const char *name) {
    for (TypeEntry *e = env->structs; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e->type;
        }
    }
    return NULL;
}

void type_env_define_enum(TypeEnv *env, const char *name, Type *type) {
    TypeEntry *entry = agim_alloc(sizeof(TypeEntry));
    entry->name = agim_alloc(strlen(name) + 1);
    strcpy(entry->name, name);
    entry->type = type;
    entry->next = env->enums;
    env->enums = entry;
}

Type *type_env_lookup_enum(TypeEnv *env, const char *name) {
    for (TypeEntry *e = env->enums; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e->type;
        }
    }
    return NULL;
}

void type_env_define_func(TypeEnv *env, const char *name, Type *type) {
    TypeEntry *entry = agim_alloc(sizeof(TypeEntry));
    entry->name = agim_alloc(strlen(name) + 1);
    strcpy(entry->name, name);
    entry->type = type;
    entry->next = env->funcs;
    env->funcs = entry;
}

Type *type_env_lookup_func(TypeEnv *env, const char *name) {
    for (TypeEntry *e = env->funcs; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e->type;
        }
    }
    return NULL;
}

/*============================================================================
 * AST Type Conversion
 *============================================================================*/

Type *type_from_ast(TypeEnv *env, AstNode *type_node) {
    if (!type_node) return type_any();

    switch (type_node->type) {
    case NODE_TYPE_NAME: {
        const char *name = type_node->as.type_name.name;
        if (strcmp(name, "int") == 0) return type_int();
        if (strcmp(name, "float") == 0) return type_float();
        if (strcmp(name, "string") == 0) return type_string();
        if (strcmp(name, "bool") == 0) return type_bool();
        if (strcmp(name, "void") == 0) return type_void();
        if (strcmp(name, "bytes") == 0) return type_bytes();
        if (strcmp(name, "Pid") == 0) return type_pid();

        /* Check for user-defined struct */
        Type *struct_type = type_env_lookup_struct(env, name);
        if (struct_type) return type_clone(struct_type);

        /* Check for user-defined enum */
        Type *enum_type = type_env_lookup_enum(env, name);
        if (enum_type) return type_clone(enum_type);

        /* Unknown type - treat as any for now */
        return type_any();
    }

    case NODE_TYPE_ARRAY:
        return type_array(type_from_ast(env, type_node->as.type_array.elem_type));

    case NODE_TYPE_MAP:
        return type_map(
            type_from_ast(env, type_node->as.type_map.key_type),
            type_from_ast(env, type_node->as.type_map.value_type)
        );

    case NODE_TYPE_GENERIC: {
        const char *name = type_node->as.type_generic.name;
        if (strcmp(name, "Option") == 0 && type_node->as.type_generic.arg_count == 1) {
            return type_option(type_from_ast(env, type_node->as.type_generic.type_args[0]));
        }
        if (strcmp(name, "Result") == 0 && type_node->as.type_generic.arg_count == 2) {
            return type_result(
                type_from_ast(env, type_node->as.type_generic.type_args[0]),
                type_from_ast(env, type_node->as.type_generic.type_args[1])
            );
        }
        /* Unknown generic - treat as any */
        return type_any();
    }

    case NODE_TYPE_FUNC: {
        size_t count = type_node->as.type_func.param_count;
        Type **params = NULL;
        if (count > 0) {
            params = agim_alloc(sizeof(Type *) * count);
            for (size_t i = 0; i < count; i++) {
                params[i] = type_from_ast(env, type_node->as.type_func.param_types[i]);
            }
        }
        Type *ret = type_from_ast(env, type_node->as.type_func.return_type);
        return type_function(params, count, ret);
    }

    default:
        return type_any();
    }
}

/*============================================================================
 * Type Checker
 *============================================================================*/

struct TypeChecker {
    TypeEnv *env;
    char *error;
    int error_line;
    bool had_error;
};

TypeChecker *typechecker_new(void) {
    TypeChecker *tc = agim_alloc(sizeof(TypeChecker));
    tc->env = type_env_new();
    tc->error = NULL;
    tc->error_line = 0;
    tc->had_error = false;
    return tc;
}

void typechecker_free(TypeChecker *tc) {
    if (!tc) return;
    type_env_free(tc->env);
    if (tc->error) agim_free(tc->error);
    agim_free(tc);
}

static void tc_error(TypeChecker *tc, int line, const char *fmt, ...) {
    if (tc->had_error) return;
    tc->had_error = true;
    tc->error_line = line;

    va_list args;
    va_start(args, fmt);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    tc->error = agim_alloc(strlen(buffer) + 1);
    strcpy(tc->error, buffer);
}

/* Forward declarations */
static Type *check_expr(TypeChecker *tc, AstNode *node);
static void check_stmt(TypeChecker *tc, AstNode *node);
static void check_decl(TypeChecker *tc, AstNode *node);

static Type *check_expr(TypeChecker *tc, AstNode *node) {
    if (!node || tc->had_error) return type_any();

    switch (node->type) {
    case NODE_INT:
        return type_int();

    case NODE_FLOAT:
        return type_float();

    case NODE_STRING:
        return type_string();

    case NODE_BOOL:
        return type_bool();

    case NODE_NIL:
        return type_nil();

    case NODE_NONE:
        return type_option(type_any());

    case NODE_SOME: {
        Type *inner = check_expr(tc, node->as.some_expr.value);
        return type_option(inner);
    }

    case NODE_IDENT: {
        Type *t = type_env_lookup(tc->env, node->as.ident.name);
        if (!t) {
            /* Check if it's a function */
            t = type_env_lookup_func(tc->env, node->as.ident.name);
        }
        if (!t) {
            /* Unknown variable - use any for gradual typing */
            return type_any();
        }
        return type_clone(t);
    }

    case NODE_ARRAY: {
        if (node->as.array.count == 0) {
            return type_array(type_any());
        }
        Type *elem_type = check_expr(tc, node->as.array.elements[0]);
        for (size_t i = 1; i < node->as.array.count; i++) {
            Type *t = check_expr(tc, node->as.array.elements[i]);
            if (!type_equals(elem_type, t)) {
                /* Mixed array - use any */
                type_free(elem_type);
                type_free(t);
                return type_array(type_any());
            }
            type_free(t);
        }
        return type_array(elem_type);
    }

    case NODE_MAP: {
        if (node->as.map.count == 0) {
            return type_map(type_string(), type_any());
        }
        /* Assume string keys for now */
        Type *val_type = check_expr(tc, node->as.map.values[0]);
        for (size_t i = 1; i < node->as.map.count; i++) {
            Type *t = check_expr(tc, node->as.map.values[i]);
            if (!type_equals(val_type, t)) {
                type_free(val_type);
                type_free(t);
                return type_map(type_string(), type_any());
            }
            type_free(t);
        }
        return type_map(type_string(), val_type);
    }

    case NODE_BINARY: {
        Type *left = check_expr(tc, node->as.binary.left);
        Type *right = check_expr(tc, node->as.binary.right);

        TokenType op = node->as.binary.op;

        /* Comparison operators return bool */
        if (op == TOK_EQ || op == TOK_NE || op == TOK_LT ||
            op == TOK_LE || op == TOK_GT || op == TOK_GE) {
            type_free(left);
            type_free(right);
            return type_bool();
        }

        /* Logical operators */
        if (op == TOK_AND || op == TOK_OR) {
            type_free(left);
            type_free(right);
            return type_bool();
        }

        /* Arithmetic - check numeric types */
        if (op == TOK_PLUS || op == TOK_MINUS || op == TOK_STAR ||
            op == TOK_SLASH || op == TOK_PERCENT) {
            /* String concatenation */
            if (op == TOK_PLUS && left->kind == TYPE_STRING) {
                type_free(left);
                type_free(right);
                return type_string();
            }

            /* Numeric operations */
            if (left->kind == TYPE_FLOAT || right->kind == TYPE_FLOAT) {
                type_free(left);
                type_free(right);
                return type_float();
            }
            type_free(left);
            type_free(right);
            return type_int();
        }

        type_free(left);
        type_free(right);
        return type_any();
    }

    case NODE_UNARY: {
        Type *operand = check_expr(tc, node->as.unary.operand);
        if (node->as.unary.op == TOK_NOT) {
            type_free(operand);
            return type_bool();
        }
        if (node->as.unary.op == TOK_MINUS) {
            /* Keep numeric type */
            return operand;
        }
        return operand;
    }

    case NODE_CALL: {
        Type *callee_type = check_expr(tc, node->as.call.callee);

        /* Check arguments */
        for (size_t i = 0; i < node->as.call.arg_count; i++) {
            Type *arg_type = check_expr(tc, node->as.call.args[i]);
            type_free(arg_type);
        }

        if (callee_type->kind == TYPE_FUNCTION) {
            Type *ret = type_clone(callee_type->as.func.return_type);
            type_free(callee_type);
            return ret ? ret : type_any();
        }

        type_free(callee_type);
        return type_any();
    }

    case NODE_MEMBER: {
        Type *obj_type = check_expr(tc, node->as.member.object);

        if (obj_type->kind == TYPE_STRUCT) {
            /* Find field type */
            for (size_t i = 0; i < obj_type->as.struct_type.field_count; i++) {
                if (strcmp(obj_type->as.struct_type.field_names[i], node->as.member.field) == 0) {
                    Type *field_type = type_clone(obj_type->as.struct_type.field_types[i]);
                    type_free(obj_type);
                    return field_type;
                }
            }
            tc_error(tc, node->line, "struct '%s' has no field '%s'",
                     obj_type->as.struct_type.name, node->as.member.field);
        }

        type_free(obj_type);
        return type_any();
    }

    case NODE_INDEX: {
        Type *obj_type = check_expr(tc, node->as.index_expr.object);
        Type *idx_type = check_expr(tc, node->as.index_expr.index);

        if (obj_type->kind == TYPE_ARRAY) {
            Type *elem = type_clone(obj_type->as.array.elem_type);
            type_free(obj_type);
            type_free(idx_type);
            return elem;
        }
        if (obj_type->kind == TYPE_MAP) {
            Type *val = type_clone(obj_type->as.map.value_type);
            type_free(obj_type);
            type_free(idx_type);
            return val;
        }

        type_free(obj_type);
        type_free(idx_type);
        return type_any();
    }

    case NODE_TERNARY: {
        Type *cond = check_expr(tc, node->as.ternary.cond);
        Type *then_type = check_expr(tc, node->as.ternary.then_expr);
        Type *else_type = check_expr(tc, node->as.ternary.else_expr);

        type_free(cond);
        if (type_equals(then_type, else_type)) {
            type_free(else_type);
            return then_type;
        }
        type_free(then_type);
        type_free(else_type);
        return type_any();
    }

    case NODE_RESULT_OK: {
        Type *val_type = check_expr(tc, node->as.result_expr.value);
        return type_result(val_type, type_any());
    }

    case NODE_RESULT_ERR: {
        Type *err_type = check_expr(tc, node->as.result_expr.value);
        return type_result(type_any(), err_type);
    }

    case NODE_TRY: {
        Type *expr_type = check_expr(tc, node->as.try_expr.expr);
        if (expr_type->kind == TYPE_RESULT) {
            Type *ok = type_clone(expr_type->as.result.ok_type);
            type_free(expr_type);
            return ok;
        }
        type_free(expr_type);
        return type_any();
    }

    case NODE_MATCH: {
        check_expr(tc, node->as.match_expr.expr);
        if (node->as.match_expr.arm_count > 0) {
            return check_expr(tc, node->as.match_expr.arms[0]->as.match_arm.body);
        }
        return type_any();
    }

    case NODE_ASSIGN: {
        Type *target = check_expr(tc, node->as.assign.target);
        Type *value = check_expr(tc, node->as.assign.value);

        /* Check mutability */
        if (node->as.assign.target->type == NODE_IDENT) {
            const char *name = node->as.assign.target->as.ident.name;
            if (!type_env_is_mutable(tc->env, name)) {
                tc_error(tc, node->line, "cannot assign to immutable variable '%s'", name);
            }
        }

        /* Check type compatibility */
        if (!type_assignable(target, value)) {
            tc_error(tc, node->line, "cannot assign '%s' to '%s'",
                     type_to_string(value), type_to_string(target));
        }

        type_free(target);
        return value;
    }

    default:
        return type_any();
    }
}

static void check_stmt(TypeChecker *tc, AstNode *node) {
    if (!node || tc->had_error) return;

    switch (node->type) {
    case NODE_LET:
    case NODE_CONST: {
        Type *decl_type = NULL;
        if (node->as.var_decl.type_ann) {
            decl_type = type_from_ast(tc->env, node->as.var_decl.type_ann);
        }

        Type *init_type = check_expr(tc, node->as.var_decl.value);

        if (decl_type) {
            if (!type_assignable(decl_type, init_type)) {
                tc_error(tc, node->line, "cannot assign '%s' to '%s'",
                         type_to_string(init_type), type_to_string(decl_type));
            }
            type_free(init_type);
            type_env_define(tc->env, node->as.var_decl.name, decl_type,
                            node->type == NODE_LET && node->as.var_decl.is_mutable);
        } else {
            type_env_define(tc->env, node->as.var_decl.name, init_type,
                            node->type == NODE_LET && node->as.var_decl.is_mutable);
        }
        break;
    }

    case NODE_IF:
        check_expr(tc, node->as.if_stmt.cond);
        type_env_push_scope(tc->env);
        check_stmt(tc, node->as.if_stmt.then_block);
        type_env_pop_scope(tc->env);
        if (node->as.if_stmt.else_block) {
            type_env_push_scope(tc->env);
            check_stmt(tc, node->as.if_stmt.else_block);
            type_env_pop_scope(tc->env);
        }
        break;

    case NODE_WHILE:
        check_expr(tc, node->as.while_stmt.cond);
        type_env_push_scope(tc->env);
        check_stmt(tc, node->as.while_stmt.body);
        type_env_pop_scope(tc->env);
        break;

    case NODE_FOR:
        type_env_push_scope(tc->env);
        {
            Type *iter_type = check_expr(tc, node->as.for_stmt.iterable);
            Type *elem_type = type_any();
            if (iter_type->kind == TYPE_ARRAY) {
                elem_type = type_clone(iter_type->as.array.elem_type);
            }
            type_env_define(tc->env, node->as.for_stmt.var, elem_type, false);
            if (node->as.for_stmt.index_var) {
                type_env_define(tc->env, node->as.for_stmt.index_var, type_int(), false);
            }
            type_free(iter_type);
        }
        check_stmt(tc, node->as.for_stmt.body);
        type_env_pop_scope(tc->env);
        break;

    case NODE_RETURN:
        if (node->as.return_stmt.value) {
            check_expr(tc, node->as.return_stmt.value);
        }
        break;

    case NODE_BLOCK:
        for (size_t i = 0; i < node->as.block.count; i++) {
            check_stmt(tc, node->as.block.stmts[i]);
        }
        break;

    case NODE_EXPR_STMT:
        check_expr(tc, node->as.return_stmt.value);
        break;

    case NODE_BREAK:
    case NODE_CONTINUE:
        break;

    default:
        break;
    }
}

static void check_decl(TypeChecker *tc, AstNode *node) {
    if (!node || tc->had_error) return;

    switch (node->type) {
    case NODE_STRUCT_DECL: {
        Type *struct_type = type_new(TYPE_STRUCT);
        struct_type->as.struct_type.name = agim_alloc(strlen(node->as.struct_decl.name) + 1);
        strcpy(struct_type->as.struct_type.name, node->as.struct_decl.name);
        struct_type->as.struct_type.field_count = node->as.struct_decl.field_count;
        struct_type->as.struct_type.field_names = agim_alloc(sizeof(char *) * node->as.struct_decl.field_count);
        struct_type->as.struct_type.field_types = agim_alloc(sizeof(Type *) * node->as.struct_decl.field_count);

        for (size_t i = 0; i < node->as.struct_decl.field_count; i++) {
            AstNode *field = node->as.struct_decl.fields[i];
            struct_type->as.struct_type.field_names[i] = agim_alloc(strlen(field->as.struct_field.name) + 1);
            strcpy(struct_type->as.struct_type.field_names[i], field->as.struct_field.name);
            struct_type->as.struct_type.field_types[i] = type_from_ast(tc->env, field->as.struct_field.type_ann);
        }

        type_env_define_struct(tc->env, node->as.struct_decl.name, struct_type);
        break;
    }

    case NODE_ENUM_DECL: {
        Type *enum_type = type_new(TYPE_ENUM);
        enum_type->as.enum_type.name = agim_alloc(strlen(node->as.enum_decl.name) + 1);
        strcpy(enum_type->as.enum_type.name, node->as.enum_decl.name);
        enum_type->as.enum_type.variant_count = node->as.enum_decl.variant_count;
        enum_type->as.enum_type.variant_names = agim_alloc(sizeof(char *) * node->as.enum_decl.variant_count);
        enum_type->as.enum_type.variant_payloads = agim_alloc(sizeof(Type *) * node->as.enum_decl.variant_count);

        for (size_t i = 0; i < node->as.enum_decl.variant_count; i++) {
            AstNode *variant = node->as.enum_decl.variants[i];
            enum_type->as.enum_type.variant_names[i] = agim_alloc(strlen(variant->as.enum_variant.name) + 1);
            strcpy(enum_type->as.enum_type.variant_names[i], variant->as.enum_variant.name);
            enum_type->as.enum_type.variant_payloads[i] = type_from_ast(tc->env, variant->as.enum_variant.payload_type);
        }

        type_env_define_enum(tc->env, node->as.enum_decl.name, enum_type);
        break;
    }

    case NODE_TYPE_ALIAS:
        /* Type aliases just define an alternative name */
        /* For now, we don't track them separately */
        break;

    case NODE_FN_DECL:
    case NODE_TOOL_DECL: {
        /* Build function type */
        size_t param_count = node->as.fn_decl.param_count;
        Type **param_types = NULL;
        if (param_count > 0) {
            param_types = agim_alloc(sizeof(Type *) * param_count);
            for (size_t i = 0; i < param_count; i++) {
                AstNode *param = node->as.fn_decl.params[i];
                param_types[i] = type_from_ast(tc->env, param->as.param.type_ann);
            }
        }
        Type *ret_type = type_from_ast(tc->env, node->as.fn_decl.return_type);
        Type *func_type = type_function(param_types, param_count, ret_type);

        type_env_define_func(tc->env, node->as.fn_decl.name, func_type);

        /* Check function body */
        type_env_push_scope(tc->env);
        for (size_t i = 0; i < param_count; i++) {
            AstNode *param = node->as.fn_decl.params[i];
            Type *pt = type_from_ast(tc->env, param->as.param.type_ann);
            type_env_define(tc->env, param->as.param.name, pt, false);
        }
        check_stmt(tc, node->as.fn_decl.body);
        type_env_pop_scope(tc->env);
        break;
    }

    case NODE_EXPORT:
        check_decl(tc, node->as.export_stmt.decl);
        break;

    case NODE_IMPORT:
    case NODE_IMPORT_FROM:
        /* Imports don't need type checking */
        break;

    default:
        /* Treat as statement */
        check_stmt(tc, node);
        break;
    }
}

bool typechecker_check(TypeChecker *tc, AstNode *program) {
    if (!program || program->type != NODE_PROGRAM) {
        tc_error(tc, 0, "invalid program");
        return false;
    }

    /* First pass: collect struct/enum/function declarations */
    for (size_t i = 0; i < program->as.program.count; i++) {
        AstNode *decl = program->as.program.decls[i];
        if (decl->type == NODE_STRUCT_DECL ||
            decl->type == NODE_ENUM_DECL ||
            decl->type == NODE_FN_DECL ||
            decl->type == NODE_TOOL_DECL) {
            check_decl(tc, decl);
        } else if (decl->type == NODE_EXPORT) {
            AstNode *inner = decl->as.export_stmt.decl;
            if (inner->type == NODE_STRUCT_DECL ||
                inner->type == NODE_ENUM_DECL ||
                inner->type == NODE_FN_DECL ||
                inner->type == NODE_TOOL_DECL) {
                check_decl(tc, decl);
            }
        }
    }

    /* Second pass: check all statements */
    for (size_t i = 0; i < program->as.program.count && !tc->had_error; i++) {
        AstNode *decl = program->as.program.decls[i];
        if (decl->type != NODE_STRUCT_DECL &&
            decl->type != NODE_ENUM_DECL &&
            decl->type != NODE_FN_DECL &&
            decl->type != NODE_TOOL_DECL) {
            check_decl(tc, decl);
        }
    }

    return !tc->had_error;
}

const char *typechecker_error(TypeChecker *tc) {
    return tc->error;
}

int typechecker_error_line(TypeChecker *tc) {
    return tc->error_line;
}
