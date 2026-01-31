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

/* String Creation */

Value *value_string_n(const char *str, size_t length) {
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return NULL;

    v->type = VAL_STRING;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = VALUE_IMMUTABLE;
    v->gc_state = 0;
    v->next = NULL;

    String *s = agim_alloc(sizeof(String) + length + 1);
    if (!s) {
        agim_free(v);
        return NULL;
    }
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

/* String Interning Cache
 *
 * 4-way set-associative cache (4096 entries total) for common strings.
 * Thread-safe using atomic operations with CAS pattern.
 *
 * Memory behavior:
 * - Entries are evicted when new strings hash to the same slot
 * - Max overhead: ~256KB (4096 entries * ~64 bytes avg string)
 * - This overhead is acceptable for typical usage patterns per CLAUDE.md
 *
 * Trade-offs:
 * - Larger cache = better hit rate for repeated strings
 * - No explicit cleanup mechanism - relies on natural eviction
 * - Evicted entries may still be in use (refcounted)
 */

#define INTERN_CACHE_SETS 1024
#define INTERN_CACHE_WAYS 4

typedef struct {
    _Atomic(Value *) entries[INTERN_CACHE_WAYS];
} InternCacheSet;

static InternCacheSet intern_cache[INTERN_CACHE_SETS];

Value *string_intern(const char *str, size_t len) {
    if (!str) return NULL;

    uint64_t hash = agim_hash_string(str, len);
    size_t set_idx = hash % INTERN_CACHE_SETS;
    InternCacheSet *set = &intern_cache[set_idx];

    /* Check all ways in the set */
    for (int way = 0; way < INTERN_CACHE_WAYS; way++) {
        Value *cached = atomic_load_explicit(&set->entries[way], memory_order_acquire);
        if (cached &&
            cached->type == VAL_STRING &&
            cached->as.string->length == len &&
            memcmp(cached->as.string->data, str, len) == 0) {
            /* Use safe retain that handles concurrent freeing */
            Value *retained = value_retain(cached);
            if (retained) return retained;
            /* If retain failed, cached value is being freed - continue search */
        }
    }

    /* Cache miss: create new string with refcount=1 for caller */
    Value *v = value_string_n(str, len);
    if (v) {
        /* Retain once more for the cache's reference (refcount=2)
         * This ensures the value stays alive even if the caller frees their ref */
        value_retain(v);

        /* Insert in a way based on hash (simple LRU approximation) */
        int way = (int)((hash >> 8) % INTERN_CACHE_WAYS);

        /* CAS to store in cache. If we fail, another thread already inserted,
         * so release our extra cache reference */
        Value *expected = atomic_load_explicit(&set->entries[way], memory_order_relaxed);
        if (!atomic_compare_exchange_strong_explicit(
                &set->entries[way], &expected, v,
                memory_order_release, memory_order_relaxed)) {
            /* CAS failed - release the cache's reference */
            value_release(v);
        } else if (expected != NULL) {
            /* CAS succeeded and evicted an old value - release its cache reference.
             * This prevents memory leaks from long-running processes with
             * changing string patterns. */
            value_release(expected);
        }
    }
    return v;
}

/* String Properties */

size_t string_length(const Value *v) {
    if (!v || v->type != VAL_STRING) return 0;
    return v->as.string->length;
}

size_t string_chars(const Value *v) {
    if (!v || v->type != VAL_STRING) return 0;

    const char *s = v->as.string->data;
    size_t len = v->as.string->length;
    size_t count = 0;

    for (size_t i = 0; i < len; i++) {
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

/* String Operations */

Value *string_concat(const Value *a, const Value *b) {
    if (!a || a->type != VAL_STRING || !b || b->type != VAL_STRING) {
        return value_nil();
    }

    size_t len_a = a->as.string->length;
    size_t len_b = b->as.string->length;

    /* Overflow check: ensure len_a + len_b + 1 doesn't overflow */
    if (len_a > SIZE_MAX - len_b - 1) {
        return value_nil();  /* Overflow would occur */
    }

    size_t total = len_a + len_b;

    /* Allocate Value and String directly without temporary buffer */
    Value *v = agim_alloc(sizeof(Value));
    if (!v) return value_nil();

    v->type = VAL_STRING;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = VALUE_IMMUTABLE;
    v->gc_state = 0;
    v->next = NULL;

    String *s = agim_alloc(sizeof(String) + total + 1);
    if (!s) {
        agim_free(v);
        return value_nil();
    }
    s->length = total;
    memcpy(s->data, a->as.string->data, len_a);
    memcpy(s->data + len_a, b->as.string->data, len_b);
    s->data[total] = '\0';
    s->hash = agim_hash_string(s->data, total);

    v->as.string = s;
    return v;
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

    size_t start = 0;
    while (start < len && isspace((unsigned char)str[start])) {
        start++;
    }

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
        return value_string_n(str, v->as.string->length);
    }

    size_t count = 0;
    const char *pos = str;
    while ((pos = strstr(pos, old_str)) != NULL) {
        count++;
        pos += old_len;
    }

    if (count == 0) {
        return value_string_n(str, v->as.string->length);
    }

    /* Calculate result length with overflow protection.
     * Handle both cases: new_len > old_len (grows) and new_len < old_len (shrinks) */
    size_t result_len;
    if (new_len >= old_len) {
        size_t diff = new_len - old_len;
        /* Check for overflow: count * diff */
        if (diff > 0 && count > SIZE_MAX / diff) {
            return value_nil();  /* Overflow would occur */
        }
        size_t growth = count * diff;
        /* Check for overflow: base + growth */
        if (growth > SIZE_MAX - v->as.string->length) {
            return value_nil();  /* Overflow would occur */
        }
        result_len = v->as.string->length + growth;
    } else {
        /* Shrinking: new_len < old_len, so result is smaller */
        size_t diff = old_len - new_len;
        size_t shrinkage = count * diff;
        /* Shrinkage can't exceed original length (count * old_len <= original) */
        result_len = v->as.string->length - shrinkage;
    }

    char *buf = agim_alloc(result_len + 1);
    if (!buf) {
        return value_nil();
    }

    char *dest = buf;
    const char *src = str;

    while ((pos = strstr(src, old_str)) != NULL) {
        size_t prefix_len = (size_t)(pos - src);
        memcpy(dest, src, prefix_len);
        dest += prefix_len;

        memcpy(dest, new_str, new_len);
        dest += new_len;

        src = pos + old_len;
    }

    /* Copy remaining part using memcpy with known length */
    size_t remaining = strlen(src);
    memcpy(dest, src, remaining + 1);  /* +1 for null terminator */

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
