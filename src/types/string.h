/*
 * Agim - String Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_TYPES_STRING_H
#define AGIM_TYPES_STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Value Value;

/* String Structure */

typedef struct String {
    size_t length;
    size_t hash;
    char data[];
} String;

/* String Creation */

Value *value_string(const char *str);
Value *value_string_n(const char *str, size_t length);

/* String interning for commonly used strings */
Value *string_intern(const char *str, size_t len);

/* String Properties */

size_t string_length(const Value *v);
size_t string_chars(const Value *v);
size_t string_hash(const Value *v);
const char *string_data(const Value *v);

/* String Operations */

Value *string_concat(const Value *a, const Value *b);
Value *string_slice(const Value *v, size_t start, size_t end);
Value *string_index(const Value *v, size_t index);
int64_t string_find(const Value *v, const char *needle);
bool string_equals(const Value *a, const Value *b);
int string_compare(const Value *a, const Value *b);
Value *string_split(const Value *v, const char *delimiter);
Value *string_join(const Value *arr, const char *separator);
Value *string_trim(const Value *v);
Value *string_upper(const Value *v);
Value *string_lower(const Value *v);
Value *string_replace(const Value *v, const char *old_str, const char *new_str);
bool string_starts_with(const Value *v, const char *prefix);
bool string_ends_with(const Value *v, const char *suffix);

#endif /* AGIM_TYPES_STRING_H */
