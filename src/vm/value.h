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

/*============================================================================
 * Value Types
 *============================================================================*/

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
    VAL_VECTOR,     /* For embeddings */
    VAL_CLOSURE,    /* For closures */
    VAL_RESULT,     /* Result<T, E> for error handling */
    VAL_OPTION,     /* Option<T> - Some(value) or None */
    VAL_STRUCT,     /* User-defined struct instance */
    VAL_ENUM,       /* User-defined enum variant */
} ValueType;

/*============================================================================
 * Type Structures (from types/ modules)
 *============================================================================*/

#include "types/string.h"
#include "types/array.h"
#include "types/map.h"
#include "types/vector.h"
#include "types/closure.h"

/*============================================================================
 * Function Object
 *============================================================================*/

typedef struct Function {
    const char *name;
    size_t arity;
    size_t code_offset;      /* Offset into bytecode */
    size_t locals_count;     /* Number of local variables */
    struct Function *parent; /* Enclosing function (for closures) */
} Function;

/*============================================================================
 * Byte Buffer
 *============================================================================*/

typedef struct Bytes {
    size_t length;
    size_t capacity;
    uint8_t *data;
} Bytes;

/*============================================================================
 * Result Type (for error handling)
 *============================================================================*/

typedef struct Result {
    bool is_ok;              /* true = Ok(value), false = Err(value) */
    struct Value *value;     /* The wrapped value (either success or error) */
} Result;

/*============================================================================
 * Option Type
 *============================================================================*/

typedef struct Option {
    bool is_some;            /* true = Some(value), false = None */
    struct Value *value;     /* The wrapped value (if Some) */
} Option;

/*============================================================================
 * Struct Instance
 *============================================================================*/

typedef struct StructInstance {
    char *type_name;         /* Name of the struct type */
    char **field_names;      /* Field names (for reflection) */
    struct Value **fields;   /* Field values */
    size_t field_count;      /* Number of fields */
} StructInstance;

/*============================================================================
 * Enum Variant Instance
 *============================================================================*/

typedef struct EnumInstance {
    char *type_name;         /* Name of the enum type */
    char *variant_name;      /* Name of the variant */
    struct Value *payload;   /* Payload value (NULL for unit variants) */
} EnumInstance;

/*============================================================================
 * Value Structure
 *============================================================================*/

/* COW flags */
#define VALUE_COW_SHARED   0x01  /* Value is shared, copy before mutating */
#define VALUE_IMMUTABLE    0x02  /* Value cannot be mutated */

/* Refcount sentinel values */
#define REFCOUNT_FREEING   UINT32_MAX  /* Sentinel: object is being freed by GC */
#define REFCOUNT_SATURATED (UINT32_MAX - 1)  /* Refcount at max, never decremented */

/*
 * GC state bits (stored in gc_state field):
 * - Bit 0: marked (for current GC cycle)
 * - Bit 1: generation (0=young, 1=old)
 * - Bits 2-4: survival count (0-7, promotes to old after threshold)
 * - Bit 5: remembered (in remember set for generational GC)
 */
#define GC_MARKED       0x01
#define GC_OLD_GEN      0x02
#define GC_SURVIVAL_MASK 0x1C  /* Bits 2-4 */
#define GC_SURVIVAL_SHIFT 2
#define GC_REMEMBERED   0x20

typedef struct Value {
    ValueType type;
    _Atomic(uint32_t) refcount;  /* Reference count for COW (atomic for thread-safe sharing) */
    uint8_t flags;               /* COW flags */
    uint8_t gc_state;            /* GC state: marked, generation, survival count, remembered */
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
        void *vector;              /* Vector* - forward declared */
        void *closure;             /* Closure* - forward declared */
        Result *result;            /* Result* - for error handling */
        Option *option;            /* Option* - for optional values */
        StructInstance *struct_val; /* Struct instance */
        EnumInstance *enum_val;    /* Enum variant instance */
    } as;
    /* GC fields */
    struct Value *next; /* Intrusive list for GC */
} Value;

/*============================================================================
 * Safe Type Access Macros
 *
 * These macros provide type-safe access to Value union members.
 * They validate the type before accessing, returning NULL on mismatch
 * to prevent type confusion vulnerabilities.
 *============================================================================*/

/**
 * Safely access Value as String*.
 * Returns NULL if v is NULL or not VAL_STRING.
 */
#define VALUE_AS_STRING(v) \
    (((v) && (v)->type == VAL_STRING) ? (v)->as.string : NULL)

/**
 * Safely access Value as Array*.
 * Returns NULL if v is NULL or not VAL_ARRAY.
 */
#define VALUE_AS_ARRAY(v) \
    (((v) && (v)->type == VAL_ARRAY) ? (v)->as.array : NULL)

/**
 * Safely access Value as Map*.
 * Returns NULL if v is NULL or not VAL_MAP.
 */
#define VALUE_AS_MAP(v) \
    (((v) && (v)->type == VAL_MAP) ? (v)->as.map : NULL)

/**
 * Safely access Value as Function*.
 * Returns NULL if v is NULL or not VAL_FUNCTION.
 */
#define VALUE_AS_FUNCTION(v) \
    (((v) && (v)->type == VAL_FUNCTION) ? (v)->as.function : NULL)

/**
 * Safely access Value as Bytes*.
 * Returns NULL if v is NULL or not VAL_BYTES.
 */
#define VALUE_AS_BYTES(v) \
    (((v) && (v)->type == VAL_BYTES) ? (v)->as.bytes : NULL)

/**
 * Safely access Value as boolean.
 * Returns false if v is NULL or not VAL_BOOL.
 */
#define VALUE_AS_BOOL(v) \
    (((v) && (v)->type == VAL_BOOL) ? (v)->as.boolean : false)

/**
 * Safely access Value as int64_t.
 * Returns 0 if v is NULL or not VAL_INT.
 */
#define VALUE_AS_INT(v) \
    (((v) && (v)->type == VAL_INT) ? (v)->as.integer : 0)

/**
 * Safely access Value as double.
 * Returns 0.0 if v is NULL or not VAL_FLOAT.
 */
#define VALUE_AS_FLOAT(v) \
    (((v) && (v)->type == VAL_FLOAT) ? (v)->as.floating : 0.0)

/**
 * Safely access Value as PID.
 * Returns 0 if v is NULL or not VAL_PID.
 */
#define VALUE_AS_PID(v) \
    (((v) && (v)->type == VAL_PID) ? (v)->as.pid : 0)

/**
 * Safely access Value as void* (vector).
 * Returns NULL if v is NULL or not VAL_VECTOR.
 */
#define VALUE_AS_VECTOR(v) \
    (((v) && (v)->type == VAL_VECTOR) ? (v)->as.vector : NULL)

/**
 * Safely access Value as void* (closure).
 * Returns NULL if v is NULL or not VAL_CLOSURE.
 */
#define VALUE_AS_CLOSURE(v) \
    (((v) && (v)->type == VAL_CLOSURE) ? (v)->as.closure : NULL)

/**
 * Safely access Value as Result*.
 * Returns NULL if v is NULL or not VAL_RESULT.
 */
#define VALUE_AS_RESULT(v) \
    (((v) && (v)->type == VAL_RESULT) ? (v)->as.result : NULL)

/**
 * Safely access Value as Option*.
 * Returns NULL if v is NULL or not VAL_OPTION.
 */
#define VALUE_AS_OPTION(v) \
    (((v) && (v)->type == VAL_OPTION) ? (v)->as.option : NULL)

/**
 * Safely access Value as StructInstance*.
 * Returns NULL if v is NULL or not VAL_STRUCT.
 */
#define VALUE_AS_STRUCT(v) \
    (((v) && (v)->type == VAL_STRUCT) ? (v)->as.struct_val : NULL)

/**
 * Safely access Value as EnumInstance*.
 * Returns NULL if v is NULL or not VAL_ENUM.
 */
#define VALUE_AS_ENUM(v) \
    (((v) && (v)->type == VAL_ENUM) ? (v)->as.enum_val : NULL)

/* GC state helpers */
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

/*============================================================================
 * Value Constructors (primitives)
 *============================================================================*/

Value *value_nil(void);
Value *value_bool(bool value);
Value *value_int(int64_t value);
Value *value_float(double value);
Value *value_pid(uint64_t pid);
Value *value_function(const char *name, size_t arity);
Value *value_bytes(size_t capacity);

/*============================================================================
 * Result Constructors
 *============================================================================*/

/**
 * Create an Ok result wrapping the given value.
 */
Value *value_result_ok(Value *value);

/**
 * Create an Err result wrapping the given error value.
 */
Value *value_result_err(Value *error);

/**
 * Check if a Result value is Ok.
 */
bool value_result_is_ok(const Value *v);

/**
 * Check if a Result value is Err.
 */
bool value_result_is_err(const Value *v);

/**
 * Unwrap a Result value, getting the inner value.
 * Returns NULL if the result is Err.
 */
Value *value_result_unwrap(const Value *v);

/**
 * Unwrap a Result value with a default.
 * Returns the inner value if Ok, or the default if Err.
 */
Value *value_result_unwrap_or(const Value *v, Value *default_val);

/**
 * Get the error value from an Err result.
 * Returns NULL if the result is Ok.
 */
Value *value_result_unwrap_err(const Value *v);

/*============================================================================
 * Option Constructors
 *============================================================================*/

/**
 * Create a Some option wrapping the given value.
 */
Value *value_some(Value *value);

/**
 * Create a None option.
 */
Value *value_none(void);

/**
 * Check if an Option value is Some.
 */
bool value_option_is_some(const Value *v);

/**
 * Check if an Option value is None.
 */
bool value_option_is_none(const Value *v);

/**
 * Unwrap an Option value, getting the inner value.
 * Returns NULL if the option is None.
 */
Value *value_option_unwrap(const Value *v);

/**
 * Unwrap an Option value with a default.
 * Returns the inner value if Some, or the default if None.
 */
Value *value_option_unwrap_or(const Value *v, Value *default_val);

/*============================================================================
 * Struct Constructors
 *============================================================================*/

/**
 * Create a struct instance.
 */
Value *value_struct_new(const char *type_name, size_t field_count);

/**
 * Set a field in a struct instance.
 */
void value_struct_set_field(Value *v, size_t index, const char *name, Value *value);

/**
 * Get a field from a struct instance by name.
 */
Value *value_struct_get_field(const Value *v, const char *name);

/**
 * Get a field from a struct instance by index.
 */
Value *value_struct_get_field_index(const Value *v, size_t index);

/**
 * Get the type name of a struct instance.
 */
const char *value_struct_type_name(const Value *v);

/*============================================================================
 * Enum Constructors
 *============================================================================*/

/**
 * Create an enum variant instance without payload.
 */
Value *value_enum_unit(const char *type_name, const char *variant_name);

/**
 * Create an enum variant instance with payload.
 */
Value *value_enum_with_payload(const char *type_name, const char *variant_name, Value *payload);

/**
 * Get the type name of an enum instance.
 */
const char *value_enum_type_name(const Value *v);

/**
 * Get the variant name of an enum instance.
 */
const char *value_enum_variant_name(const Value *v);

/**
 * Get the payload of an enum instance (NULL for unit variants).
 */
Value *value_enum_payload(const Value *v);

/**
 * Check if enum variant matches a given name.
 */
bool value_enum_is_variant(const Value *v, const char *variant_name);

/*============================================================================
 * Value Type Checking
 *============================================================================*/

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

/*============================================================================
 * Value Comparison and Hashing
 *============================================================================*/

bool value_equals(const Value *a, const Value *b);
int value_compare(const Value *a, const Value *b);
size_t value_hash(const Value *v);

/*============================================================================
 * Value Type Coercion
 *============================================================================*/

int64_t value_to_int(const Value *v);
double value_to_float(const Value *v);
const char *value_to_string(const Value *v);

/*============================================================================
 * Bytes Operations
 *============================================================================*/

size_t bytes_length(const Value *v);
bool bytes_append(Value *v, const uint8_t *data, size_t length);

/*============================================================================
 * Debug
 *============================================================================*/

void value_print(const Value *v);
char *value_repr(const Value *v);

/*============================================================================
 * Memory Management
 *============================================================================*/

void value_free(Value *v);
Value *value_copy(const Value *v);

/*============================================================================
 * Copy-on-Write (COW) Support
 *============================================================================*/

/**
 * Increment reference count (for sharing).
 * Returns the value for chaining.
 */
Value *value_retain(Value *v);

/**
 * Decrement reference count.
 * Does NOT free the value - GC handles that.
 */
void value_release(Value *v);

/**
 * Check if value needs copy-on-write before mutation.
 * Returns true if refcount > 1.
 */
bool value_needs_cow(const Value *v);

/**
 * Check if value can be shared (immutable or COW-safe).
 */
bool value_can_share(const Value *v);

/**
 * Mark value as shared (sets COW_SHARED flag).
 */
void value_mark_shared(Value *v);

/**
 * Check if value is immutable (safe to share without COW).
 */
bool value_is_immutable(const Value *v);

/**
 * Create a COW-shared copy of a value for message passing.
 * Returns the same value with incremented refcount if immutable,
 * or a COW-marked shared reference if mutable.
 */
Value *value_cow_share(Value *v);

#endif /* AGIM_VM_VALUE_H */
