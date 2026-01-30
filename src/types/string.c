/*
 * Agim - String Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "types/string.h"
#include "vm/value.h"
#include "util/alloc.h"
#include "util/hash.h"

#include <ctype.h>
#include <stdatomic.h>
#include <string.h>

/*============================================================================
 * String Creation
 *============================================================================*/

Value *value_string_n(const char *str, size_t length) {
    Value *v = agim_alloc(sizeof(Value));
    v->type = VAL_STRING;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = VALUE_IMMUTABLE;  /* Strings are immutable */
    v->gc_state = 0;
    v->next = NULL;

    String *s = agim_alloc(sizeof(String) + length + 1);
    s->length = length;
    s->hash = agim_hash_string(str, length);
    memcpy(s->data, str, length);
    s->data[length] = '\0';

    v->as.string = s;
    return v;
}

Value *value_string(const char *str) {
    return value_string_n(str, strlen(str));
}

/*============================================================================
 * String Properties
 *============================================================================*/

size_t string_length(const Value *v) {
    if (!v || v->type != VAL_STRING) return 0;
    return v->as.string->length;
}

size_t string_chars(const Value *v) {
    if (!v || v->type != VAL_STRING) return 0;

    /* Count UTF-8 characters */
    const char *s = v->as.string->data;
    size_t len = v->as.string->length;
    size_t count = 0;

    for (size_t i = 0; i < len; i++) {
        /* Count bytes that are not continuation bytes (10xxxxxx) */
        if ((s[i] & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

size_t string_hash(const Value *v) {
    if (!v || v->type != VAL_STRING) return 0;
    return v->as.string->hash;
}

const char *string_data(const Value *v) {
    if (!v || v->type != VAL_STRING) return NULL;
    return v->as.string->data;
}

/*============================================================================
 * String Operations
 *============================================================================*/

Value *string_concat(const Value *a, const Value *b) {
    if (!a || a->type != VAL_STRING || !b || b->type != VAL_STRING) {
        return value_nil();
    }

    size_t len_a = a->as.string->length;
    size_t len_b = b->as.string->length;
    size_t total = len_a + len_b;

    char *buf = agim_alloc(total + 1);
    memcpy(buf, a->as.string->data, len_a);
    memcpy(buf + len_a, b->as.string->data, len_b);
    buf[total] = '\0';

    Value *result = value_string_n(buf, total);
    agim_free(buf);
    return result;
}

Value *string_slice(const Value *v, size_t start, size_t end) {
    if (!v || v->type != VAL_STRING) {
        return value_nil();
    }

    size_t len = v->as.string->length;
    if (start > len) start = len;
    if (end > len) end = len;
    if (start > end) start = end;

    return value_string_n(v->as.string->data + start, end - start);
}

Value *string_index(const Value *v, size_t index) {
    if (!v || v->type != VAL_STRING) {
        return value_nil();
    }

    if (index >= v->as.string->length) {
        return value_nil();
    }

    return value_string_n(v->as.string->data + index, 1);
}

int64_t string_find(const Value *v, const char *needle) {
    if (!v || v->type != VAL_STRING || !needle) {
        return -1;
    }

    const char *found = strstr(v->as.string->data, needle);
    if (!found) {
        return -1;
    }

    return (int64_t)(found - v->as.string->data);
}

bool string_equals(const Value *a, const Value *b) {
    if (!a || !b) return false;
    if (a->type != VAL_STRING || b->type != VAL_STRING) return false;
    if (a->as.string->length != b->as.string->length) return false;
    return memcmp(a->as.string->data, b->as.string->data,
                  a->as.string->length) == 0;
}

int string_compare(const Value *a, const Value *b) {
    if (!a || !b) return 0;
    if (a->type != VAL_STRING || b->type != VAL_STRING) return 0;
    return strcmp(a->as.string->data, b->as.string->data);
}

Value *string_split(const Value *v, const char *delimiter) {
    if (!v || v->type != VAL_STRING || !delimiter) {
        return value_array();
    }

    Value *result = value_array();
    const char *str = v->as.string->data;
    size_t delim_len = strlen(delimiter);

    if (delim_len == 0) {
        /* Split into individual characters */
        for (size_t i = 0; i < v->as.string->length; i++) {
            result = array_push(result, value_string_n(str + i, 1));
        }
        return result;
    }

    const char *start = str;
    const char *pos;

    while ((pos = strstr(start, delimiter)) != NULL) {
        result = array_push(result, value_string_n(start, (size_t)(pos - start)));
        start = pos + delim_len;
    }

    /* Add the remaining part */
    result = array_push(result, value_string(start));

    return result;
}

Value *string_join(const Value *arr, const char *separator) {
    if (!arr || arr->type != VAL_ARRAY || !separator) {
        return value_string("");
    }

    size_t arr_len = array_length(arr);
    if (arr_len == 0) {
        return value_string("");
    }

    size_t sep_len = strlen(separator);

    /* Calculate total length */
    size_t total = 0;
    for (size_t i = 0; i < arr_len; i++) {
        Value *item = array_get(arr, i);
        if (item && item->type == VAL_STRING) {
            total += item->as.string->length;
        }
        if (i < arr_len - 1) {
            total += sep_len;
        }
    }

    /* Build result */
    char *buf = agim_alloc(total + 1);
    char *ptr = buf;

    for (size_t i = 0; i < arr_len; i++) {
        Value *item = array_get(arr, i);
        if (item && item->type == VAL_STRING) {
            memcpy(ptr, item->as.string->data, item->as.string->length);
            ptr += item->as.string->length;
        }
        if (i < arr_len - 1) {
            memcpy(ptr, separator, sep_len);
            ptr += sep_len;
        }
    }
    *ptr = '\0';

    Value *result = value_string_n(buf, total);
    agim_free(buf);
    return result;
}

Value *string_trim(const Value *v) {
    if (!v || v->type != VAL_STRING) {
        return value_nil();
    }

    const char *str = v->as.string->data;
    size_t len = v->as.string->length;

    /* Find start (skip leading whitespace) */
    size_t start = 0;
    while (start < len && isspace((unsigned char)str[start])) {
        start++;
    }

    /* Find end (skip trailing whitespace) */
    size_t end = len;
    while (end > start && isspace((unsigned char)str[end - 1])) {
        end--;
    }

    return value_string_n(str + start, end - start);
}

Value *string_upper(const Value *v) {
    if (!v || v->type != VAL_STRING) {
        return value_nil();
    }

    size_t len = v->as.string->length;
    char *buf = agim_alloc(len + 1);

    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)toupper((unsigned char)v->as.string->data[i]);
    }
    buf[len] = '\0';

    Value *result = value_string_n(buf, len);
    agim_free(buf);
    return result;
}

Value *string_lower(const Value *v) {
    if (!v || v->type != VAL_STRING) {
        return value_nil();
    }

    size_t len = v->as.string->length;
    char *buf = agim_alloc(len + 1);

    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)v->as.string->data[i]);
    }
    buf[len] = '\0';

    Value *result = value_string_n(buf, len);
    agim_free(buf);
    return result;
}

Value *string_replace(const Value *v, const char *old_str, const char *new_str) {
    if (!v || v->type != VAL_STRING || !old_str || !new_str) {
        return value_nil();
    }

    const char *str = v->as.string->data;
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);

    if (old_len == 0) {
        /* Can't replace empty string */
        return value_string_n(str, v->as.string->length);
    }

    /* Count occurrences */
    size_t count = 0;
    const char *pos = str;
    while ((pos = strstr(pos, old_str)) != NULL) {
        count++;
        pos += old_len;
    }

    if (count == 0) {
        return value_string_n(str, v->as.string->length);
    }

    /* Calculate new length */
    size_t result_len = v->as.string->length + count * (new_len - old_len);
    char *buf = agim_alloc(result_len + 1);

    /* Build result */
    char *dest = buf;
    const char *src = str;

    while ((pos = strstr(src, old_str)) != NULL) {
        /* Copy part before match */
        size_t prefix_len = (size_t)(pos - src);
        memcpy(dest, src, prefix_len);
        dest += prefix_len;

        /* Copy replacement */
        memcpy(dest, new_str, new_len);
        dest += new_len;

        src = pos + old_len;
    }

    /* Copy remaining */
    strcpy(dest, src);

    Value *result = value_string_n(buf, result_len);
    agim_free(buf);
    return result;
}

bool string_starts_with(const Value *v, const char *prefix) {
    if (!v || v->type != VAL_STRING || !prefix) {
        return false;
    }

    size_t prefix_len = strlen(prefix);
    if (prefix_len > v->as.string->length) {
        return false;
    }

    return memcmp(v->as.string->data, prefix, prefix_len) == 0;
}

bool string_ends_with(const Value *v, const char *suffix) {
    if (!v || v->type != VAL_STRING || !suffix) {
        return false;
    }

    size_t suffix_len = strlen(suffix);
    if (suffix_len > v->as.string->length) {
        return false;
    }

    size_t offset = v->as.string->length - suffix_len;
    return memcmp(v->as.string->data + offset, suffix, suffix_len) == 0;
}
