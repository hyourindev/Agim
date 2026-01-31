/*
 * Agim - Type Checker
 *
 * Static type checking pass for the typed functional language.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_TYPECHECKER_H
#define AGIM_LANG_TYPECHECKER_H

#include "lang/ast.h"
#include <stdbool.h>

/* Type Representation */

typedef enum TypeKind {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_BYTES,
    TYPE_NIL,
    TYPE_ARRAY,
    TYPE_MAP,
    TYPE_OPTION,
    TYPE_RESULT,
    TYPE_STRUCT,
    TYPE_ENUM,
    TYPE_FUNCTION,
    TYPE_ANY,
    TYPE_UNKNOWN,
    TYPE_PID,
} TypeKind;

typedef struct Type Type;

struct Type {
    TypeKind kind;
    union {
        struct {
            Type *elem_type;
        } array;

        struct {
            Type *key_type;
            Type *value_type;
        } map;

        struct {
            Type *inner_type;
        } option;

        struct {
            Type *ok_type;
            Type *err_type;
        } result;

        struct {
            char *name;
            char **field_names;
            Type **field_types;
            size_t field_count;
        } struct_type;

        struct {
            char *name;
            char **variant_names;
            Type **variant_payloads;
            size_t variant_count;
        } enum_type;

        struct {
            Type **param_types;
            size_t param_count;
            Type *return_type;
        } func;
    } as;
};

/* Type Environment */

typedef struct TypeEnv TypeEnv;

TypeEnv *type_env_new(void);
void type_env_free(TypeEnv *env);
void type_env_push_scope(TypeEnv *env);
void type_env_pop_scope(TypeEnv *env);
void type_env_define(TypeEnv *env, const char *name, Type *type, bool is_mutable);
Type *type_env_lookup(TypeEnv *env, const char *name);
bool type_env_is_mutable(TypeEnv *env, const char *name);
void type_env_define_struct(TypeEnv *env, const char *name, Type *type);
Type *type_env_lookup_struct(TypeEnv *env, const char *name);
void type_env_define_enum(TypeEnv *env, const char *name, Type *type);
Type *type_env_lookup_enum(TypeEnv *env, const char *name);
void type_env_define_func(TypeEnv *env, const char *name, Type *type);
Type *type_env_lookup_func(TypeEnv *env, const char *name);

/* Type Construction */

Type *type_int(void);
Type *type_float(void);
Type *type_string(void);
Type *type_bool(void);
Type *type_void(void);
Type *type_bytes(void);
Type *type_nil(void);
Type *type_any(void);
Type *type_pid(void);

Type *type_array(Type *elem_type);
Type *type_map(Type *key_type, Type *value_type);
Type *type_option(Type *inner_type);
Type *type_result(Type *ok_type, Type *err_type);
Type *type_function(Type **param_types, size_t param_count, Type *return_type);

void type_free(Type *type);
Type *type_clone(Type *type);
bool type_equals(Type *a, Type *b);
bool type_assignable(Type *to, Type *from);
const char *type_to_string(Type *type);

/* Type Checker */

typedef struct TypeChecker TypeChecker;

TypeChecker *typechecker_new(void);
void typechecker_free(TypeChecker *tc);
bool typechecker_check(TypeChecker *tc, AstNode *program);
const char *typechecker_error(TypeChecker *tc);
int typechecker_error_line(TypeChecker *tc);

/* AST Type Conversion */

Type *type_from_ast(TypeEnv *env, AstNode *type_node);

#endif /* AGIM_LANG_TYPECHECKER_H */
