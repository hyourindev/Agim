/*
 * Agim - Value Representation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_VALUE_H
#define AGIM_VM_VALUE_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Value Types */

typedef enum ValueType {
    VAL_NIL,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_ARRAY,
    VAL_MAP,
    VAL_PID,
    VAL_FUNCTION,
    VAL_BYTES,
    VAL_VECTOR,
    VAL_CLOSURE,
    VAL_RESULT,
    VAL_OPTION,
    VAL_STRUCT,
    VAL_ENUM,
} ValueType;

/* Type Structures */

#include "types/string.h"
#include "types/array.h"
#include "types/map.h"
#include "types/vector.h"
#include "types/closure.h"

/* Function Object */

typedef struct Function {
    const char *name;
    size_t arity;
    size_t code_offset;
    size_t locals_count;
    struct Function *parent;
} Function;

/* Byte Buffer */

typedef struct Bytes {
    size_t length;
    size_t capacity;
    uint8_t *data;
} Bytes;

/* Result Type */

typedef struct Result {
    bool is_ok;
    struct Value *value;
} Result;

/* Option Type */

typedef struct Option {
    bool is_some;
    struct Value *value;
} Option;

/* Struct Instance */

typedef struct StructInstance {
    char *type_name;
    char **field_names;
    struct Value **fields;
    size_t field_count;
} StructInstance;

/* Enum Variant Instance */

typedef struct EnumInstance {
    char *type_name;
    char *variant_name;
    struct Value *payload;
} EnumInstance;

/* Value Structure */

#define VALUE_COW_SHARED   0x01
#define VALUE_IMMUTABLE    0x02

#define REFCOUNT_FREEING   UINT32_MAX
#define REFCOUNT_SATURATED (UINT32_MAX - 1)

#define GC_MARKED       0x01
#define GC_OLD_GEN      0x02
#define GC_SURVIVAL_MASK 0x1C
#define GC_SURVIVAL_SHIFT 2
#define GC_REMEMBERED   0x20

typedef struct Value {
    ValueType type;
    _Atomic(uint32_t) refcount;
    uint8_t flags;
    uint8_t gc_state;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        String *string;
        Array *array;
        Map *map;
        uint64_t pid;
        Function *function;
        Bytes *bytes;
        void *vector;
        void *closure;
        Result *result;
        Option *option;
        StructInstance *struct_val;
        EnumInstance *enum_val;
    } as;
    struct Value *next;
} Value;

/* Safe Type Access Macros */

#define VALUE_AS_STRING(v) \
    (((v) && (v)->type == VAL_STRING) ? (v)->as.string : NULL)

#define VALUE_AS_ARRAY(v) \
    (((v) && (v)->type == VAL_ARRAY) ? (v)->as.array : NULL)

#define VALUE_AS_MAP(v) \
    (((v) && (v)->type == VAL_MAP) ? (v)->as.map : NULL)

#define VALUE_AS_FUNCTION(v) \
    (((v) && (v)->type == VAL_FUNCTION) ? (v)->as.function : NULL)

#define VALUE_AS_BYTES(v) \
    (((v) && (v)->type == VAL_BYTES) ? (v)->as.bytes : NULL)

#define VALUE_AS_BOOL(v) \
    (((v) && (v)->type == VAL_BOOL) ? (v)->as.boolean : false)

#define VALUE_AS_INT(v) \
    (((v) && (v)->type == VAL_INT) ? (v)->as.integer : 0)

#define VALUE_AS_FLOAT(v) \
    (((v) && (v)->type == VAL_FLOAT) ? (v)->as.floating : 0.0)

#define VALUE_AS_PID(v) \
    (((v) && (v)->type == VAL_PID) ? (v)->as.pid : 0)

#define VALUE_AS_VECTOR(v) \
    (((v) && (v)->type == VAL_VECTOR) ? (v)->as.vector : NULL)

#define VALUE_AS_CLOSURE(v) \
    (((v) && (v)->type == VAL_CLOSURE) ? (v)->as.closure : NULL)

#define VALUE_AS_RESULT(v) \
    (((v) && (v)->type == VAL_RESULT) ? (v)->as.result : NULL)

#define VALUE_AS_OPTION(v) \
    (((v) && (v)->type == VAL_OPTION) ? (v)->as.option : NULL)

#define VALUE_AS_STRUCT(v) \
    (((v) && (v)->type == VAL_STRUCT) ? (v)->as.struct_val : NULL)

#define VALUE_AS_ENUM(v) \
    (((v) && (v)->type == VAL_ENUM) ? (v)->as.enum_val : NULL)

/* GC State Helpers */

static inline bool value_is_marked(const Value *v) {
    return v && (v->gc_state & GC_MARKED);
}

static inline void value_set_marked(Value *v, bool marked) {
    if (v) {
        if (marked) v->gc_state |= GC_MARKED;
        else v->gc_state &= ~GC_MARKED;
    }
}

static inline bool value_is_old_gen(const Value *v) {
    return v && (v->gc_state & GC_OLD_GEN);
}

static inline void value_set_old_gen(Value *v) {
    if (v) v->gc_state |= GC_OLD_GEN;
}

static inline uint8_t value_survival_count(const Value *v) {
    return v ? ((v->gc_state & GC_SURVIVAL_MASK) >> GC_SURVIVAL_SHIFT) : 0;
}

static inline void value_inc_survival(Value *v) {
    if (v) {
        uint8_t count = value_survival_count(v);
        if (count < 7) {
            v->gc_state = (v->gc_state & ~GC_SURVIVAL_MASK) | ((count + 1) << GC_SURVIVAL_SHIFT);
        }
    }
}

static inline bool value_is_remembered(const Value *v) {
    return v && (v->gc_state & GC_REMEMBERED);
}

static inline void value_set_remembered(Value *v, bool remembered) {
    if (v) {
        if (remembered) v->gc_state |= GC_REMEMBERED;
        else v->gc_state &= ~GC_REMEMBERED;
    }
}

/* Value Constructors */

Value *value_nil(void);
Value *value_bool(bool value);
Value *value_int(int64_t value);
Value *value_float(double value);
Value *value_pid(uint64_t pid);
Value *value_function(const char *name, size_t arity);
Value *value_bytes(size_t capacity);

/* Result Constructors */

Value *value_result_ok(Value *value);
Value *value_result_err(Value *error);
bool value_result_is_ok(const Value *v);
bool value_result_is_err(const Value *v);
Value *value_result_unwrap(const Value *v);
Value *value_result_unwrap_or(const Value *v, Value *default_val);
Value *value_result_unwrap_err(const Value *v);

/* Option Constructors */

Value *value_some(Value *value);
Value *value_none(void);
bool value_option_is_some(const Value *v);
bool value_option_is_none(const Value *v);
Value *value_option_unwrap(const Value *v);
Value *value_option_unwrap_or(const Value *v, Value *default_val);

/* Struct Constructors */

Value *value_struct_new(const char *type_name, size_t field_count);
void value_struct_set_field(Value *v, size_t index, const char *name, Value *value);
Value *value_struct_get_field(const Value *v, const char *name);
Value *value_struct_get_field_index(const Value *v, size_t index);
const char *value_struct_type_name(const Value *v);

/* Enum Constructors */

Value *value_enum_unit(const char *type_name, const char *variant_name);
Value *value_enum_with_payload(const char *type_name, const char *variant_name, Value *payload);
const char *value_enum_type_name(const Value *v);
const char *value_enum_variant_name(const Value *v);
Value *value_enum_payload(const Value *v);
bool value_enum_is_variant(const Value *v, const char *variant_name);

/* Value Type Checking */

bool value_is_nil(const Value *v);
bool value_is_bool(const Value *v);
bool value_is_int(const Value *v);
bool value_is_float(const Value *v);
bool value_is_number(const Value *v);
bool value_is_string(const Value *v);
bool value_is_array(const Value *v);
bool value_is_map(const Value *v);
bool value_is_pid(const Value *v);
bool value_is_function(const Value *v);
bool value_is_result(const Value *v);
bool value_is_option(const Value *v);
bool value_is_struct(const Value *v);
bool value_is_enum(const Value *v);
bool value_is_truthy(const Value *v);

/* Value Comparison and Hashing */

bool value_equals(const Value *a, const Value *b);
int value_compare(const Value *a, const Value *b);
size_t value_hash(const Value *v);

/* Value Type Coercion */

int64_t value_to_int(const Value *v);
double value_to_float(const Value *v);
const char *value_to_string(const Value *v);

/* Bytes Operations */

size_t bytes_length(const Value *v);
bool bytes_append(Value *v, const uint8_t *data, size_t length);

/* Debug */

void value_print(const Value *v);
char *value_repr(const Value *v);

/* Memory Management */

void value_free(Value *v);
Value *value_copy(const Value *v);

/* Copy-on-Write Support */

Value *value_retain(Value *v);
void value_release(Value *v);
bool value_needs_cow(const Value *v);
bool value_can_share(const Value *v);
void value_mark_shared(Value *v);
bool value_is_immutable(const Value *v);
Value *value_cow_share(Value *v);

#endif /* AGIM_VM_VALUE_H */
