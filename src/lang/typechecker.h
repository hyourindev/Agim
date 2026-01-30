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

/*============================================================================
 * Type Representation
 *============================================================================*/

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
    TYPE_ANY,       /* For untyped/dynamic values */
    TYPE_UNKNOWN,   /* For type inference */
    TYPE_PID,
} TypeKind;

typedef struct Type Type;

struct Type {
    TypeKind kind;
    union {
        /* Array: element type */
        struct {
            Type *elem_type;
        } array;

        /* Map: key and value types */
        struct {
            Type *key_type;
            Type *value_type;
        } map;

        /* Option<T> */
        struct {
            Type *inner_type;
        } option;

        /* Result<T, E> */
        struct {
            Type *ok_type;
            Type *err_type;
        } result;

        /* Struct: name and field types */
        struct {
            char *name;
            char **field_names;
            Type **field_types;
            size_t field_count;
        } struct_type;

        /* Enum: name and variant info */
        struct {
            char *name;
            char **variant_names;
            Type **variant_payloads;  /* NULL for unit variants */
            size_t variant_count;
        } enum_type;

        /* Function: param and return types */
        struct {
            Type **param_types;
            size_t param_count;
            Type *return_type;
        } func;
    } as;
};

/*============================================================================
 * Type Environment
 *============================================================================*/

typedef struct TypeEnv TypeEnv;

/**
 * Create a new type environment.
 */
TypeEnv *type_env_new(void);

/**
 * Free a type environment.
 */
void type_env_free(TypeEnv *env);

/**
 * Enter a new scope.
 */
void type_env_push_scope(TypeEnv *env);

/**
 * Exit current scope.
 */
void type_env_pop_scope(TypeEnv *env);

/**
 * Define a variable with its type.
 */
void type_env_define(TypeEnv *env, const char *name, Type *type, bool is_mutable);

/**
 * Look up a variable's type.
 */
Type *type_env_lookup(TypeEnv *env, const char *name);

/**
 * Check if a variable is mutable.
 */
bool type_env_is_mutable(TypeEnv *env, const char *name);

/**
 * Define a struct type.
 */
void type_env_define_struct(TypeEnv *env, const char *name, Type *type);

/**
 * Look up a struct type.
 */
Type *type_env_lookup_struct(TypeEnv *env, const char *name);

/**
 * Define an enum type.
 */
void type_env_define_enum(TypeEnv *env, const char *name, Type *type);

/**
 * Look up an enum type.
 */
Type *type_env_lookup_enum(TypeEnv *env, const char *name);

/**
 * Define a function type.
 */
void type_env_define_func(TypeEnv *env, const char *name, Type *type);

/**
 * Look up a function type.
 */
Type *type_env_lookup_func(TypeEnv *env, const char *name);

/*============================================================================
 * Type Construction
 *============================================================================*/

/**
 * Create primitive types.
 */
Type *type_int(void);
Type *type_float(void);
Type *type_string(void);
Type *type_bool(void);
Type *type_void(void);
Type *type_bytes(void);
Type *type_nil(void);
Type *type_any(void);
Type *type_pid(void);

/**
 * Create composite types.
 */
Type *type_array(Type *elem_type);
Type *type_map(Type *key_type, Type *value_type);
Type *type_option(Type *inner_type);
Type *type_result(Type *ok_type, Type *err_type);
Type *type_function(Type **param_types, size_t param_count, Type *return_type);

/**
 * Free a type.
 */
void type_free(Type *type);

/**
 * Clone a type.
 */
Type *type_clone(Type *type);

/**
 * Check if two types are equal.
 */
bool type_equals(Type *a, Type *b);

/**
 * Check if type 'from' can be assigned to type 'to'.
 */
bool type_assignable(Type *to, Type *from);

/**
 * Get string representation of a type (for error messages).
 */
const char *type_to_string(Type *type);

/*============================================================================
 * Type Checker
 *============================================================================*/

typedef struct TypeChecker TypeChecker;

/**
 * Create a new type checker.
 */
TypeChecker *typechecker_new(void);

/**
 * Free a type checker.
 */
void typechecker_free(TypeChecker *tc);

/**
 * Type check a program.
 * Returns true if type checking succeeds, false otherwise.
 */
bool typechecker_check(TypeChecker *tc, AstNode *program);

/**
 * Get the error message if type checking failed.
 */
const char *typechecker_error(TypeChecker *tc);

/**
 * Get the line number of the error.
 */
int typechecker_error_line(TypeChecker *tc);

/*============================================================================
 * AST Type Conversion
 *============================================================================*/

/**
 * Convert an AST type node to a Type.
 */
Type *type_from_ast(TypeEnv *env, AstNode *type_node);

#endif /* AGIM_LANG_TYPECHECKER_H */
