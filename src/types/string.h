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

/* Forward declaration */
typedef struct Value Value;

/*============================================================================
 * String Structure
 *============================================================================*/

/**
 * Heap-allocated string with cached hash.
 * Uses flexible array member for data storage.
 */
typedef struct String {
    size_t length;      /* Length in bytes (not including null terminator) */
    size_t hash;        /* Cached FNV-1a hash */
    char data[];        /* Flexible array member */
} String;

/*============================================================================
 * String Creation
 *============================================================================*/

/**
 * Create a string value from a null-terminated C string.
 */
Value *value_string(const char *str);

/**
 * Create a string value with explicit length (can contain nulls).
 */
Value *value_string_n(const char *str, size_t length);

/*============================================================================
 * String Properties
 *============================================================================*/

/**
 * Get the length of a string in bytes.
 * Returns 0 if not a string.
 */
size_t string_length(const Value *v);

/**
 * Get the number of UTF-8 characters in a string.
 * Returns 0 if not a string.
 */
size_t string_chars(const Value *v);

/**
 * Get the hash of a string.
 * Returns 0 if not a string.
 */
size_t string_hash(const Value *v);

/**
 * Get the raw data pointer of a string.
 * Returns NULL if not a string.
 */
const char *string_data(const Value *v);

/*============================================================================
 * String Operations
 *============================================================================*/

/**
 * Concatenate two strings.
 * Returns nil if either argument is not a string.
 */
Value *string_concat(const Value *a, const Value *b);

/**
 * Extract a substring (slice).
 * Returns nil if not a string.
 */
Value *string_slice(const Value *v, size_t start, size_t end);

/**
 * Get character at index (returns single-character string).
 * Returns nil if out of bounds or not a string.
 */
Value *string_index(const Value *v, size_t index);

/**
 * Find first occurrence of substring.
 * Returns -1 if not found.
 */
int64_t string_find(const Value *v, const char *needle);

/**
 * Check if two strings are equal.
 */
bool string_equals(const Value *a, const Value *b);

/**
 * Compare two strings lexicographically.
 * Returns <0 if a<b, 0 if equal, >0 if a>b.
 */
int string_compare(const Value *a, const Value *b);

/**
 * Split string by delimiter into array.
 */
Value *string_split(const Value *v, const char *delimiter);

/**
 * Join array of strings with separator.
 */
Value *string_join(const Value *arr, const char *separator);

/**
 * Remove leading and trailing whitespace.
 */
Value *string_trim(const Value *v);

/**
 * Convert to uppercase.
 */
Value *string_upper(const Value *v);

/**
 * Convert to lowercase.
 */
Value *string_lower(const Value *v);

/**
 * Replace all occurrences of old_str with new_str.
 */
Value *string_replace(const Value *v, const char *old_str, const char *new_str);

/**
 * Check if string starts with prefix.
 */
bool string_starts_with(const Value *v, const char *prefix);

/**
 * Check if string ends with suffix.
 */
bool string_ends_with(const Value *v, const char *suffix);

#endif /* AGIM_TYPES_STRING_H */
