/*
 * Agim - Value Representation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "vm/value.h"
#include "util/alloc.h"
#include "util/hash.h"

#include <stdio.h>
#include <string.h>

/* Value Constructors */

Value *value_nil(void) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_NIL;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;
    return v;
}

Value *value_bool(bool value) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_BOOL;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->as.boolean = value;
    v->gc_state = 0;
    v->next = NULL;
    return v;
}

Value *value_int(int64_t value) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_INT;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->as.integer = value;
    v->gc_state = 0;
    v->next = NULL;
    return v;
}

Value *value_float(double value) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_FLOAT;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->as.floating = value;
    v->gc_state = 0;
    v->next = NULL;
    return v;
}

Value *value_pid(uint64_t pid) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_PID;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->as.pid = pid;
    v->gc_state = 0;
    v->next = NULL;
    return v;
}

Value *value_function(const char *name, size_t arity) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_FUNCTION;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    Function *fn = agim_alloc(sizeof(Function));
    if (!fn) {
        agim_free(v);
        return NULL;
    }
    fn->name = name ? strdup(name) : NULL;
    fn->arity = arity;
    fn->code_offset = 0;
    fn->locals_count = 0;
    fn->parent = NULL;

    v->as.function = fn;
    return v;
}

Value *value_bytes(size_t capacity) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_BYTES;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    Bytes *bytes = agim_alloc(sizeof(Bytes));
    if (!bytes) {
        agim_free(v);
        return NULL;
    }
    bytes->length = 0;
    bytes->capacity = capacity > 0 ? capacity : 64;
    bytes->data = agim_alloc(bytes->capacity);
    if (!bytes->data) {
        agim_free(bytes);
        agim_free(v);
        return NULL;
    }

    v->as.bytes = bytes;
    return v;
}

/* Result Constructors */

Value *value_result_ok(Value *value) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_RESULT;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    Result *result = agim_alloc(sizeof(Result));
    if (!result) {
        agim_free(v);
        return NULL;
    }
    result->is_ok = true;
    result->value = value;

    v->as.result = result;
    return v;
}

Value *value_result_err(Value *error) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_RESULT;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    Result *result = agim_alloc(sizeof(Result));
    if (!result) {
        agim_free(v);
        return NULL;
    }
    result->is_ok = false;
    result->value = error;

    v->as.result = result;
    return v;
}

bool value_result_is_ok(const Value *v) {
    if (!v || v->type != VAL_RESULT) return false;
    return v->as.result->is_ok;
}

bool value_result_is_err(const Value *v) {
    if (!v || v->type != VAL_RESULT) return false;
    return !v->as.result->is_ok;
}

Value *value_result_unwrap(const Value *v) {
    if (!v || v->type != VAL_RESULT) return NULL;
    if (!v->as.result->is_ok) return NULL;
    return v->as.result->value;
}

Value *value_result_unwrap_or(const Value *v, Value *default_val) {
    if (!v || v->type != VAL_RESULT) return default_val;
    if (v->as.result->is_ok) {
        return v->as.result->value;
    }
    return default_val;
}

Value *value_result_unwrap_err(const Value *v) {
    if (!v || v->type != VAL_RESULT) return NULL;
    if (v->as.result->is_ok) return NULL;
    return v->as.result->value;
}

/* Option Constructors */

Value *value_some(Value *value) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_OPTION;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    Option *opt = agim_alloc(sizeof(Option));
    if (!opt) {
        agim_free(v);
        return NULL;
    }
    opt->is_some = true;
    opt->value = value;

    v->as.option = opt;
    return v;
}

Value *value_none(void) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_OPTION;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    Option *opt = agim_alloc(sizeof(Option));
    if (!opt) {
        agim_free(v);
        return NULL;
    }
    opt->is_some = false;
    opt->value = NULL;

    v->as.option = opt;
    return v;
}

bool value_option_is_some(const Value *v) {
    if (!v || v->type != VAL_OPTION) return false;
    return v->as.option->is_some;
}

bool value_option_is_none(const Value *v) {
    if (!v || v->type != VAL_OPTION) return false;
    return !v->as.option->is_some;
}

Value *value_option_unwrap(const Value *v) {
    if (!v || v->type != VAL_OPTION) return NULL;
    if (!v->as.option->is_some) return NULL;
    return v->as.option->value;
}

Value *value_option_unwrap_or(const Value *v, Value *default_val) {
    if (!v || v->type != VAL_OPTION) return default_val;
    if (v->as.option->is_some) {
        return v->as.option->value;
    }
    return default_val;
}

/* Struct Constructors */

Value *value_struct_new(const char *type_name, size_t field_count) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_STRUCT;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    StructInstance *inst = agim_alloc(sizeof(StructInstance));
    if (!inst) {
        agim_free(v);
        return NULL;
    }
    inst->type_name = strdup(type_name);
    inst->field_count = field_count;
    inst->field_names = field_count > 0 ? agim_alloc(sizeof(char *) * field_count) : NULL;
    inst->fields = field_count > 0 ? agim_alloc(sizeof(Value *) * field_count) : NULL;

    for (size_t i = 0; i < field_count; i++) {
        inst->field_names[i] = NULL;
        inst->fields[i] = NULL;
    }

    v->as.struct_val = inst;
    return v;
}

void value_struct_set_field(Value *v, size_t index, const char *name, Value *value) {
    if (!v || v->type != VAL_STRUCT) return;
    StructInstance *inst = v->as.struct_val;
    if (index >= inst->field_count) return;

    if (inst->field_names[index]) {
        agim_free(inst->field_names[index]);
    }
    inst->field_names[index] = strdup(name);
    inst->fields[index] = value;
}

Value *value_struct_get_field(const Value *v, const char *name) {
    if (!v || v->type != VAL_STRUCT) return NULL;
    StructInstance *inst = v->as.struct_val;

    for (size_t i = 0; i < inst->field_count; i++) {
        if (inst->field_names[i] && strcmp(inst->field_names[i], name) == 0) {
            return inst->fields[i];
        }
    }
    return NULL;
}

Value *value_struct_get_field_index(const Value *v, size_t index) {
    if (!v || v->type != VAL_STRUCT) return NULL;
    StructInstance *inst = v->as.struct_val;
    if (index >= inst->field_count) return NULL;
    return inst->fields[index];
}

const char *value_struct_type_name(const Value *v) {
    if (!v || v->type != VAL_STRUCT) return NULL;
    return v->as.struct_val->type_name;
}

/* Enum Constructors */

Value *value_enum_unit(const char *type_name, const char *variant_name) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_ENUM;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    EnumInstance *inst = agim_alloc(sizeof(EnumInstance));
    if (!inst) {
        agim_free(v);
        return NULL;
    }
    inst->type_name = strdup(type_name);
    inst->variant_name = strdup(variant_name);
    inst->payload = NULL;

    v->as.enum_val = inst;
    return v;
}

Value *value_enum_with_payload(const char *type_name, const char *variant_name, Value *payload) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;
    v->type = VAL_ENUM;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = 0;
    v->gc_state = 0;
    v->next = NULL;

    EnumInstance *inst = agim_alloc(sizeof(EnumInstance));
    if (!inst) {
        agim_free(v);
        return NULL;
    }
    inst->type_name = strdup(type_name);
    inst->variant_name = strdup(variant_name);
    inst->payload = payload;

    v->as.enum_val = inst;
    return v;
}

const char *value_enum_type_name(const Value *v) {
    if (!v || v->type != VAL_ENUM) return NULL;
    return v->as.enum_val->type_name;
}

const char *value_enum_variant_name(const Value *v) {
    if (!v || v->type != VAL_ENUM) return NULL;
    return v->as.enum_val->variant_name;
}

Value *value_enum_payload(const Value *v) {
    if (!v || v->type != VAL_ENUM) return NULL;
    return v->as.enum_val->payload;
}

bool value_enum_is_variant(const Value *v, const char *variant_name) {
    if (!v || v->type != VAL_ENUM) return false;
    return strcmp(v->as.enum_val->variant_name, variant_name) == 0;
}

/* Value Type Checking */

bool value_is_nil(const Value *v) {
    return v && v->type == VAL_NIL;
}

bool value_is_bool(const Value *v) {
    return v && v->type == VAL_BOOL;
}

bool value_is_int(const Value *v) {
    return v && v->type == VAL_INT;
}

bool value_is_float(const Value *v) {
    return v && v->type == VAL_FLOAT;
}

bool value_is_number(const Value *v) {
    return v && (v->type == VAL_INT || v->type == VAL_FLOAT);
}

bool value_is_string(const Value *v) {
    return v && v->type == VAL_STRING;
}

bool value_is_array(const Value *v) {
    return v && v->type == VAL_ARRAY;
}

bool value_is_map(const Value *v) {
    return v && v->type == VAL_MAP;
}

bool value_is_pid(const Value *v) {
    return v && v->type == VAL_PID;
}

bool value_is_function(const Value *v) {
    return v && v->type == VAL_FUNCTION;
}

bool value_is_result(const Value *v) {
    return v && v->type == VAL_RESULT;
}

bool value_is_option(const Value *v) {
    return v && v->type == VAL_OPTION;
}

bool value_is_struct(const Value *v) {
    return v && v->type == VAL_STRUCT;
}

bool value_is_enum(const Value *v) {
    return v && v->type == VAL_ENUM;
}

bool value_is_truthy(const Value *v) {
    if (!v) return false;

    switch (v->type) {
    case VAL_NIL:
        return false;
    case VAL_BOOL:
        return v->as.boolean;
    case VAL_INT:
        return v->as.integer != 0;
    case VAL_FLOAT:
        return v->as.floating != 0.0;
    case VAL_STRING:
        return v->as.string->length > 0;
    case VAL_ARRAY:
        return v->as.array->length > 0;
    case VAL_MAP:
        return v->as.map->size > 0;
    case VAL_OPTION:
        return v->as.option->is_some;
    case VAL_RESULT:
        return v->as.result->is_ok;
    default:
        return true;
    }
}

/* Value Comparison and Hashing */

bool value_equals(const Value *a, const Value *b) {
    if (!a || !b) return false;
    if (a->type != b->type) return false;

    switch (a->type) {
    case VAL_NIL:
        return true;
    case VAL_BOOL:
        return a->as.boolean == b->as.boolean;
    case VAL_INT:
        return a->as.integer == b->as.integer;
    case VAL_FLOAT:
        return a->as.floating == b->as.floating;
    case VAL_STRING:
        return string_equals(a, b);
    case VAL_PID:
        return a->as.pid == b->as.pid;
    case VAL_ARRAY: {
        Array *arr_a = a->as.array;
        Array *arr_b = b->as.array;
        if (arr_a->length != arr_b->length) return false;
        for (size_t i = 0; i < arr_a->length; i++) {
            if (!value_equals(arr_a->items[i], arr_b->items[i])) {
                return false;
            }
        }
        return true;
    }
    case VAL_MAP: {
        Map *map_a = a->as.map;
        Map *map_b = b->as.map;
        if (map_a->size != map_b->size) return false;
        /* Check all entries in map_a exist in map_b with same value */
        for (size_t i = 0; i < map_a->capacity; i++) {
            MapEntry *entry = map_a->buckets[i];
            while (entry) {
                Value *val_b = map_get(b, entry->key->data);
                if (!val_b || !value_equals(entry->value, val_b)) {
                    return false;
                }
                entry = entry->next;
            }
        }
        return true;
    }
    default:
        return a == b;
    }
}

int value_compare(const Value *a, const Value *b) {
    if (!a || !b) return 0;

    if (a->type != b->type) {
        return (int)a->type - (int)b->type;
    }

    switch (a->type) {
    case VAL_NIL:
        return 0;
    case VAL_BOOL:
        return (int)a->as.boolean - (int)b->as.boolean;
    case VAL_INT:
        if (a->as.integer < b->as.integer) return -1;
        if (a->as.integer > b->as.integer) return 1;
        return 0;
    case VAL_FLOAT:
        if (a->as.floating < b->as.floating) return -1;
        if (a->as.floating > b->as.floating) return 1;
        return 0;
    case VAL_STRING:
        return string_compare(a, b);
    default:
        return 0;
    }
}

size_t value_hash(const Value *v) {
    if (!v) return 0;

    switch (v->type) {
    case VAL_NIL:
        return 0;
    case VAL_BOOL:
        return v->as.boolean ? 1 : 0;
    case VAL_INT:
        return (size_t)v->as.integer;
    case VAL_FLOAT: {
        union {
            double d;
            size_t s;
        } u;
        u.d = v->as.floating;
        return u.s;
    }
    case VAL_STRING:
        return v->as.string->hash;
    case VAL_PID:
        return (size_t)v->as.pid;
    default:
        return (size_t)(uintptr_t)v;
    }
}

/* Value Type Coercion */

int64_t value_to_int(const Value *v) {
    if (!v) return 0;

    switch (v->type) {
    case VAL_INT:
        return v->as.integer;
    case VAL_FLOAT:
        return (int64_t)v->as.floating;
    case VAL_BOOL:
        return v->as.boolean ? 1 : 0;
    default:
        return 0;
    }
}

double value_to_float(const Value *v) {
    if (!v) return 0.0;

    switch (v->type) {
    case VAL_FLOAT:
        return v->as.floating;
    case VAL_INT:
        return (double)v->as.integer;
    case VAL_BOOL:
        return v->as.boolean ? 1.0 : 0.0;
    default:
        return 0.0;
    }
}

const char *value_to_string(const Value *v) {
    if (!v || v->type != VAL_STRING) return NULL;
    return v->as.string->data;
}

/* Bytes Operations */

size_t bytes_length(const Value *v) {
    if (!v || v->type != VAL_BYTES) return 0;
    return v->as.bytes->length;
}

bool bytes_append(Value *v, const uint8_t *data, size_t length) {
    if (!v || v->type != VAL_BYTES) return false;
    Bytes *bytes = v->as.bytes;

    if (bytes->length > SIZE_MAX - length) {
        return false;
    }

    while (bytes->length + length > bytes->capacity) {
        if (bytes->capacity > SIZE_MAX / 2) {
            return false;
        }
        size_t new_cap = bytes->capacity * 2;
        uint8_t *new_data = agim_realloc(bytes->data, new_cap);
        if (!new_data) return false;
        bytes->data = new_data;
        bytes->capacity = new_cap;
    }

    memcpy(bytes->data + bytes->length, data, length);
    bytes->length += length;
    return true;
}

/* Debug */

void value_print(const Value *v) {
    if (!v) {
        printf("(null)");
        return;
    }

    switch (v->type) {
    case VAL_NIL:
        printf("nil");
        break;
    case VAL_BOOL:
        printf("%s", v->as.boolean ? "true" : "false");
        break;
    case VAL_INT:
        printf("%ld", v->as.integer);
        break;
    case VAL_FLOAT:
        printf("%g", v->as.floating);
        break;
    case VAL_STRING:
        printf("\"%s\"", v->as.string->data);
        break;
    case VAL_ARRAY:
        printf("[array:%zu]", v->as.array->length);
        break;
    case VAL_MAP:
        printf("{map:%zu}", v->as.map->size);
        break;
    case VAL_PID:
        printf("<pid:%lu>", v->as.pid);
        break;
    case VAL_FUNCTION:
        printf("<fn:%s>", v->as.function->name ? v->as.function->name : "?");
        break;
    case VAL_BYTES:
        printf("<bytes:%zu>", v->as.bytes->length);
        break;
    case VAL_VECTOR:
        printf("<vector>");
        break;
    case VAL_CLOSURE:
        printf("<closure>");
        break;
    case VAL_RESULT:
        if (v->as.result->is_ok) {
            printf("ok(");
            value_print(v->as.result->value);
            printf(")");
        } else {
            printf("err(");
            value_print(v->as.result->value);
            printf(")");
        }
        break;
    case VAL_OPTION:
        if (v->as.option->is_some) {
            printf("some(");
            value_print(v->as.option->value);
            printf(")");
        } else {
            printf("none");
        }
        break;
    case VAL_STRUCT: {
        StructInstance *s = v->as.struct_val;
        printf("%s{", s->type_name);
        for (size_t i = 0; i < s->field_count; i++) {
            if (i > 0) printf(", ");
            printf("%s: ", s->field_names[i]);
            value_print(s->fields[i]);
        }
        printf("}");
        break;
    }
    case VAL_ENUM: {
        EnumInstance *e = v->as.enum_val;
        printf("%s::%s", e->type_name, e->variant_name);
        if (e->payload) {
            printf("(");
            value_print(e->payload);
            printf(")");
        }
        break;
    }
    }
}

static bool append_to_buf(char **buf, size_t *capacity, size_t *len, const char *str) {
    size_t str_len = strlen(str);

    if (*len > SIZE_MAX - str_len - 1) {
        return false;
    }

    while (*len + str_len + 1 > *capacity) {
        if (*capacity > SIZE_MAX / 2) {
            return false;
        }
        size_t new_cap = *capacity * 2;
        char *new_buf = agim_realloc(*buf, new_cap);
        if (!new_buf) return false;
        *buf = new_buf;
        *capacity = new_cap;
    }
    memcpy(*buf + *len, str, str_len);
    *len += str_len;
    (*buf)[*len] = '\0';
    return true;
}

static char *json_escape_string(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);

    /* Worst case: every char becomes \uXXXX (6 chars) + quotes + null
     * Check for overflow before allocation */
    if (len > (SIZE_MAX - 3) / 6) {
        return NULL;  /* Would overflow */
    }
    size_t cap = len * 6 + 3;

    char *out = agim_alloc(cap);
    if (!out) return NULL;

    size_t j = 0;
    out[j++] = '"';
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '"') {
            out[j++] = '\\'; out[j++] = '"';
        } else if (c == '\\') {
            out[j++] = '\\'; out[j++] = '\\';
        } else if (c == '\n') {
            out[j++] = '\\'; out[j++] = 'n';
        } else if (c == '\r') {
            out[j++] = '\\'; out[j++] = 'r';
        } else if (c == '\t') {
            out[j++] = '\\'; out[j++] = 't';
        } else if (c < 0x20) {
            /* Control characters -> \uXXXX */
            j += snprintf(out + j, 7, "\\u%04x", c);
        } else {
            out[j++] = c;
        }
    }
    out[j++] = '"';
    out[j] = '\0';
    return out;
}

static bool value_to_json_impl(const Value *v, char **buf, size_t *cap, size_t *len) {
    if (!v) {
        return append_to_buf(buf, cap, len, "null");
    }

    switch (v->type) {
    case VAL_NIL:
        return append_to_buf(buf, cap, len, "null");
    case VAL_BOOL:
        return append_to_buf(buf, cap, len, v->as.boolean ? "true" : "false");
    case VAL_INT: {
        char num[32];
        snprintf(num, sizeof(num), "%ld", v->as.integer);
        return append_to_buf(buf, cap, len, num);
    }
    case VAL_FLOAT: {
        char num[32];
        snprintf(num, sizeof(num), "%g", v->as.floating);
        return append_to_buf(buf, cap, len, num);
    }
    case VAL_STRING: {
        char *escaped = json_escape_string(v->as.string->data);
        if (!escaped) return false;
        bool ok = append_to_buf(buf, cap, len, escaped);
        agim_free(escaped);
        return ok;
    }
    case VAL_ARRAY: {
        if (!append_to_buf(buf, cap, len, "[")) return false;
        for (size_t i = 0; i < v->as.array->length; i++) {
            if (i > 0 && !append_to_buf(buf, cap, len, ",")) return false;
            if (!value_to_json_impl(v->as.array->items[i], buf, cap, len)) return false;
        }
        return append_to_buf(buf, cap, len, "]");
    }
    case VAL_MAP: {
        if (!append_to_buf(buf, cap, len, "{")) return false;
        Map *map = v->as.map;
        bool first = true;
        for (size_t i = 0; i < map->capacity; i++) {
            MapEntry *entry = map->buckets[i];
            while (entry) {
                if (!first && !append_to_buf(buf, cap, len, ",")) return false;
                first = false;
                char *key_escaped = json_escape_string(entry->key->data);
                if (!key_escaped) return false;
                bool ok = append_to_buf(buf, cap, len, key_escaped);
                agim_free(key_escaped);
                if (!ok) return false;
                if (!append_to_buf(buf, cap, len, ":")) return false;
                if (!value_to_json_impl(entry->value, buf, cap, len)) return false;
                entry = entry->next;
            }
        }
        return append_to_buf(buf, cap, len, "}");
    }
    case VAL_RESULT:
        if (v->as.result->is_ok) {
            if (!append_to_buf(buf, cap, len, "{\"ok\":")) return false;
            if (!value_to_json_impl(v->as.result->value, buf, cap, len)) return false;
            return append_to_buf(buf, cap, len, "}");
        } else {
            if (!append_to_buf(buf, cap, len, "{\"err\":")) return false;
            if (!value_to_json_impl(v->as.result->value, buf, cap, len)) return false;
            return append_to_buf(buf, cap, len, "}");
        }
    case VAL_OPTION:
        if (v->as.option->is_some) {
            if (!append_to_buf(buf, cap, len, "{\"some\":")) return false;
            if (!value_to_json_impl(v->as.option->value, buf, cap, len)) return false;
            return append_to_buf(buf, cap, len, "}");
        } else {
            return append_to_buf(buf, cap, len, "{\"none\":true}");
        }
    case VAL_STRUCT: {
        StructInstance *s = v->as.struct_val;
        if (!append_to_buf(buf, cap, len, "{")) return false;
        for (size_t i = 0; i < s->field_count; i++) {
            if (i > 0 && !append_to_buf(buf, cap, len, ",")) return false;
            char *key_escaped = json_escape_string(s->field_names[i]);
            if (!key_escaped) return false;
            bool ok = append_to_buf(buf, cap, len, key_escaped);
            agim_free(key_escaped);
            if (!ok) return false;
            if (!append_to_buf(buf, cap, len, ":")) return false;
            if (!value_to_json_impl(s->fields[i], buf, cap, len)) return false;
        }
        return append_to_buf(buf, cap, len, "}");
    }
    case VAL_ENUM: {
        EnumInstance *e = v->as.enum_val;
        if (!append_to_buf(buf, cap, len, "{\"")) return false;
        if (!append_to_buf(buf, cap, len, e->variant_name)) return false;
        if (!append_to_buf(buf, cap, len, "\":")) return false;
        if (e->payload) {
            if (!value_to_json_impl(e->payload, buf, cap, len)) return false;
        } else {
            if (!append_to_buf(buf, cap, len, "true")) return false;
        }
        return append_to_buf(buf, cap, len, "}");
    }
    default:
        return append_to_buf(buf, cap, len, "null");
    }
}

char *value_repr(const Value *v) {
    size_t capacity = 256;
    size_t len = 0;
    char *buf = agim_alloc(capacity);
    buf[0] = '\0';

    value_to_json_impl(v, &buf, &capacity, &len);

    return buf;
}

/* Memory Management */

void value_free(Value *v) {
    if (!v) return;

    /* Use CAS loop to safely decrement refcount.
     * When going from 1 to 0, set REFCOUNT_FREEING instead to prevent
     * concurrent value_retain from resurrecting the object. */
    uint32_t current = atomic_load_explicit(&v->refcount, memory_order_acquire);

    while (true) {
        if (current == REFCOUNT_FREEING || current == 0) {
            /* Already being freed or already freed */
            return;
        }
        if (current == REFCOUNT_SATURATED) {
            /* Never free saturated values */
            return;
        }

        uint32_t new_count;
        if (current == 1) {
            /* Last reference - mark as freeing to prevent concurrent retain */
            new_count = REFCOUNT_FREEING;
        } else {
            new_count = current - 1;
        }

        if (atomic_compare_exchange_weak_explicit(
                &v->refcount, &current, new_count,
                memory_order_acq_rel, memory_order_acquire)) {
            if (new_count != REFCOUNT_FREEING) {
                /* Not the last reference, just decremented */
                return;
            }
            /* We set REFCOUNT_FREEING - proceed to actually free */
            break;
        }
        /* CAS failed, current was updated - retry */
    }

    switch (v->type) {
    case VAL_STRING:
        agim_free(v->as.string);
        break;
    case VAL_ARRAY:
        agim_free(v->as.array->items);
        agim_free(v->as.array);
        break;
    case VAL_MAP: {
        Map *map = v->as.map;
        for (size_t i = 0; i < map->capacity; i++) {
            MapEntry *entry = map->buckets[i];
            while (entry) {
                MapEntry *next = entry->next;
                agim_free(entry->key);
                agim_free(entry);
                entry = next;
            }
        }
        agim_free(map->buckets);
        agim_free(map);
        break;
    }
    case VAL_FUNCTION:
        agim_free((void *)v->as.function->name);
        agim_free(v->as.function);
        break;
    case VAL_BYTES:
        agim_free(v->as.bytes->data);
        agim_free(v->as.bytes);
        break;
    case VAL_VECTOR:
        agim_free(v->as.vector);
        break;
    case VAL_CLOSURE: {
        Closure *closure = (Closure *)v->as.closure;
        agim_free(closure->upvalues);
        agim_free(closure);
        break;
    }
    case VAL_RESULT:
        agim_free(v->as.result);
        break;
    case VAL_OPTION:
        agim_free(v->as.option);
        break;
    case VAL_STRUCT: {
        StructInstance *s = v->as.struct_val;
        if (s) {
            agim_free(s->type_name);
            for (size_t i = 0; i < s->field_count; i++) {
                agim_free(s->field_names[i]);
            }
            agim_free(s->field_names);
            agim_free(s->fields);
            agim_free(s);
        }
        break;
    }
    case VAL_ENUM: {
        EnumInstance *e = v->as.enum_val;
        if (e) {
            agim_free(e->type_name);
            agim_free(e->variant_name);
            agim_free(e);
        }
        break;
    }
    default:
        break;
    }

    agim_free(v);
}

Value *value_copy(const Value *v) {
    if (!v) return NULL;

    switch (v->type) {
    case VAL_NIL:
        return value_nil();
    case VAL_BOOL:
        return value_bool(v->as.boolean);
    case VAL_INT:
        return value_int(v->as.integer);
    case VAL_FLOAT:
        return value_float(v->as.floating);
    case VAL_STRING:
        return value_string_n(v->as.string->data, v->as.string->length);
    case VAL_ARRAY: {
        Value *copy = value_array_with_capacity(v->as.array->capacity);
        Array *arr = v->as.array;
        for (size_t i = 0; i < arr->length; i++) {
            copy = array_push(copy, value_copy(arr->items[i]));
        }
        return copy;
    }
    case VAL_MAP: {
        Value *copy = value_map_with_capacity(v->as.map->capacity);
        Map *map = v->as.map;
        for (size_t i = 0; i < map->capacity; i++) {
            MapEntry *entry = map->buckets[i];
            while (entry) {
                copy = map_set(copy, entry->key->data, value_copy(entry->value));
                entry = entry->next;
            }
        }
        return copy;
    }
    case VAL_PID:
        return value_pid(v->as.pid);
    case VAL_FUNCTION: {
        Value *copy = value_function(v->as.function->name, v->as.function->arity);
        copy->as.function->code_offset = v->as.function->code_offset;
        copy->as.function->locals_count = v->as.function->locals_count;
        copy->as.function->parent = v->as.function->parent;
        return copy;
    }
    case VAL_VECTOR: {
        Vector *vec = (Vector *)v->as.vector;
        return value_vector_from(vec->data, vec->dim);
    }
    case VAL_CLOSURE:
        return value_nil();
    case VAL_RESULT:
        if (v->as.result->is_ok) {
            return value_result_ok(value_copy(v->as.result->value));
        } else {
            return value_result_err(value_copy(v->as.result->value));
        }
    case VAL_OPTION:
        if (v->as.option->is_some) {
            return value_some(value_copy(v->as.option->value));
        } else {
            return value_none();
        }
    case VAL_STRUCT: {
        StructInstance *s = v->as.struct_val;
        Value *copy = value_struct_new(s->type_name, s->field_count);
        for (size_t i = 0; i < s->field_count; i++) {
            value_struct_set_field(copy, i, s->field_names[i], value_copy(s->fields[i]));
        }
        return copy;
    }
    case VAL_ENUM: {
        EnumInstance *e = v->as.enum_val;
        if (e->payload) {
            return value_enum_with_payload(e->type_name, e->variant_name, value_copy(e->payload));
        } else {
            return value_enum_unit(e->type_name, e->variant_name);
        }
    }
    default:
        return value_nil();
    }
}

/* Copy-on-Write Support */

Value *value_retain(Value *v) {
    if (!v) return NULL;

    uint32_t current = atomic_load_explicit(&v->refcount, memory_order_acquire);

    while (true) {
        /* Check for values being freed or already freed */
        if (current == REFCOUNT_FREEING || current == 0) {
            return NULL;
        }
        if (current >= REFCOUNT_SATURATED) {
            return v;
        }

        uint32_t new_count = current + 1;
        if (new_count >= REFCOUNT_SATURATED) {
            new_count = REFCOUNT_SATURATED;
        }

        if (atomic_compare_exchange_weak_explicit(
                &v->refcount, &current, new_count,
                memory_order_acq_rel, memory_order_acquire)) {
            return v;
        }
    }
}

void value_release(Value *v) {
    if (!v) return;

    uint32_t current = atomic_load_explicit(&v->refcount, memory_order_acquire);

    while (true) {
        if (current == REFCOUNT_FREEING || current == REFCOUNT_SATURATED) {
            return;
        }
        if (current == 0) {
            return;
        }

        if (atomic_compare_exchange_weak_explicit(
                &v->refcount, &current, current - 1,
                memory_order_acq_rel, memory_order_acquire)) {
            return;
        }
    }
}

bool value_needs_cow(const Value *v) {
    return v && atomic_load_explicit(&v->refcount, memory_order_acquire) > 1;
}

bool value_can_share(const Value *v) {
    if (!v) return false;

    switch (v->type) {
    case VAL_NIL:
    case VAL_BOOL:
    case VAL_INT:
    case VAL_FLOAT:
    case VAL_STRING:
    case VAL_PID:
    case VAL_FUNCTION:
    case VAL_VECTOR:
        return true;

    case VAL_ARRAY:
    case VAL_MAP:
    case VAL_BYTES:
        return true;

    case VAL_CLOSURE:
        return false;

    default:
        return false;
    }
}

void value_mark_shared(Value *v) {
    if (v) {
        v->flags |= VALUE_COW_SHARED;
    }
}

bool value_is_immutable(const Value *v) {
    if (!v) return true;

    switch (v->type) {
    case VAL_NIL:
    case VAL_BOOL:
    case VAL_INT:
    case VAL_FLOAT:
    case VAL_STRING:
    case VAL_PID:
    case VAL_FUNCTION:
    case VAL_VECTOR:
        return true;

    case VAL_ARRAY:
    case VAL_MAP:
    case VAL_BYTES:
    case VAL_CLOSURE:
        return (v->flags & VALUE_IMMUTABLE) != 0;

    default:
        return false;
    }
}

Value *value_cow_share(Value *v) {
    if (!v) return NULL;

    if (value_is_immutable(v)) {
        return value_retain(v);
    }

    v->flags |= VALUE_COW_SHARED;
    return value_retain(v);
}
