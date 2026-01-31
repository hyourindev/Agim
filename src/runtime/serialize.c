/*
 * Agim - Value Serialization Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "runtime/serialize.h"
#include "types/string.h"
#include "types/array.h"
#include "types/map.h"

#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Serial Buffer Operations
 *============================================================================*/

#define INITIAL_CAPACITY 256

void serial_buffer_init(SerialBuffer *buf) {
    if (!buf) return;
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
    buf->read_pos = 0;
}

void serial_buffer_init_data(SerialBuffer *buf, const uint8_t *data, size_t size) {
    if (!buf) return;
    buf->data = (uint8_t *)data;  /* Note: not copying, just referencing */
    buf->size = size;
    buf->capacity = size;
    buf->read_pos = 0;
}

void serial_buffer_free(SerialBuffer *buf) {
    if (!buf) return;
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
    buf->read_pos = 0;
}

bool serial_buffer_ensure(SerialBuffer *buf, size_t needed) {
    if (!buf) return false;

    size_t required = buf->size + needed;
    if (required <= buf->capacity) {
        return true;
    }

    /* Grow by doubling */
    size_t new_capacity = buf->capacity ? buf->capacity : INITIAL_CAPACITY;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    uint8_t *new_data = realloc(buf->data, new_capacity);
    if (!new_data) return false;

    buf->data = new_data;
    buf->capacity = new_capacity;
    return true;
}

size_t serial_buffer_position(const SerialBuffer *buf) {
    return buf ? buf->read_pos : 0;
}

size_t serial_buffer_remaining(const SerialBuffer *buf) {
    if (!buf) return 0;
    return buf->size > buf->read_pos ? buf->size - buf->read_pos : 0;
}

/*============================================================================
 * Primitive Write Operations
 *============================================================================*/

bool serial_write_u8(SerialBuffer *buf, uint8_t value) {
    if (!serial_buffer_ensure(buf, 1)) return false;
    buf->data[buf->size++] = value;
    return true;
}

bool serial_write_u16(SerialBuffer *buf, uint16_t value) {
    if (!serial_buffer_ensure(buf, 2)) return false;
    buf->data[buf->size++] = (value >> 8) & 0xFF;
    buf->data[buf->size++] = value & 0xFF;
    return true;
}

bool serial_write_u32(SerialBuffer *buf, uint32_t value) {
    if (!serial_buffer_ensure(buf, 4)) return false;
    buf->data[buf->size++] = (value >> 24) & 0xFF;
    buf->data[buf->size++] = (value >> 16) & 0xFF;
    buf->data[buf->size++] = (value >> 8) & 0xFF;
    buf->data[buf->size++] = value & 0xFF;
    return true;
}

bool serial_write_u64(SerialBuffer *buf, uint64_t value) {
    if (!serial_buffer_ensure(buf, 8)) return false;
    buf->data[buf->size++] = (value >> 56) & 0xFF;
    buf->data[buf->size++] = (value >> 48) & 0xFF;
    buf->data[buf->size++] = (value >> 40) & 0xFF;
    buf->data[buf->size++] = (value >> 32) & 0xFF;
    buf->data[buf->size++] = (value >> 24) & 0xFF;
    buf->data[buf->size++] = (value >> 16) & 0xFF;
    buf->data[buf->size++] = (value >> 8) & 0xFF;
    buf->data[buf->size++] = value & 0xFF;
    return true;
}

bool serial_write_i64(SerialBuffer *buf, int64_t value) {
    return serial_write_u64(buf, (uint64_t)value);
}

bool serial_write_f64(SerialBuffer *buf, double value) {
    union { double f; uint64_t u; } conv;
    conv.f = value;
    return serial_write_u64(buf, conv.u);
}

bool serial_write_bytes(SerialBuffer *buf, const uint8_t *data, size_t len) {
    if (!serial_buffer_ensure(buf, len)) return false;
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return true;
}

bool serial_write_string(SerialBuffer *buf, const char *str) {
    if (!str) {
        return serial_write_u32(buf, 0);
    }
    size_t len = strlen(str);
    if (!serial_write_u32(buf, (uint32_t)len)) return false;
    return serial_write_bytes(buf, (const uint8_t *)str, len);
}

/*============================================================================
 * Primitive Read Operations
 *============================================================================*/

bool serial_read_u8(SerialBuffer *buf, uint8_t *value) {
    if (!buf || buf->read_pos >= buf->size) return false;
    *value = buf->data[buf->read_pos++];
    return true;
}

bool serial_read_u16(SerialBuffer *buf, uint16_t *value) {
    if (!buf || buf->read_pos + 2 > buf->size) return false;
    *value = ((uint16_t)buf->data[buf->read_pos] << 8) |
             buf->data[buf->read_pos + 1];
    buf->read_pos += 2;
    return true;
}

bool serial_read_u32(SerialBuffer *buf, uint32_t *value) {
    if (!buf || buf->read_pos + 4 > buf->size) return false;
    *value = ((uint32_t)buf->data[buf->read_pos] << 24) |
             ((uint32_t)buf->data[buf->read_pos + 1] << 16) |
             ((uint32_t)buf->data[buf->read_pos + 2] << 8) |
             buf->data[buf->read_pos + 3];
    buf->read_pos += 4;
    return true;
}

bool serial_read_u64(SerialBuffer *buf, uint64_t *value) {
    if (!buf || buf->read_pos + 8 > buf->size) return false;
    *value = ((uint64_t)buf->data[buf->read_pos] << 56) |
             ((uint64_t)buf->data[buf->read_pos + 1] << 48) |
             ((uint64_t)buf->data[buf->read_pos + 2] << 40) |
             ((uint64_t)buf->data[buf->read_pos + 3] << 32) |
             ((uint64_t)buf->data[buf->read_pos + 4] << 24) |
             ((uint64_t)buf->data[buf->read_pos + 5] << 16) |
             ((uint64_t)buf->data[buf->read_pos + 6] << 8) |
             buf->data[buf->read_pos + 7];
    buf->read_pos += 8;
    return true;
}

bool serial_read_i64(SerialBuffer *buf, int64_t *value) {
    uint64_t u;
    if (!serial_read_u64(buf, &u)) return false;
    *value = (int64_t)u;
    return true;
}

bool serial_read_f64(SerialBuffer *buf, double *value) {
    union { double f; uint64_t u; } conv;
    if (!serial_read_u64(buf, &conv.u)) return false;
    *value = conv.f;
    return true;
}

bool serial_read_bytes(SerialBuffer *buf, uint8_t *data, size_t len) {
    if (!buf || buf->read_pos + len > buf->size) return false;
    memcpy(data, buf->data + buf->read_pos, len);
    buf->read_pos += len;
    return true;
}

char *serial_read_string(SerialBuffer *buf) {
    uint32_t len;
    if (!serial_read_u32(buf, &len)) return NULL;
    if (len == 0) return strdup("");

    if (buf->read_pos + len > buf->size) return NULL;

    char *str = malloc(len + 1);
    if (!str) return NULL;

    memcpy(str, buf->data + buf->read_pos, len);
    str[len] = '\0';
    buf->read_pos += len;

    return str;
}

/*============================================================================
 * Value Serialization
 *============================================================================*/

SerializeResult serialize_value(Value *value, SerialBuffer *buf) {
    if (!buf) return SERIALIZE_ERROR_BUFFER;

    /* Handle NULL as nil */
    if (!value) {
        if (!serial_write_u8(buf, SERIAL_TAG_NIL)) return SERIALIZE_ERROR_BUFFER;
        return SERIALIZE_OK;
    }

    switch (value->type) {
    case VAL_NIL:
        if (!serial_write_u8(buf, SERIAL_TAG_NIL)) return SERIALIZE_ERROR_BUFFER;
        break;

    case VAL_BOOL:
        if (!serial_write_u8(buf, SERIAL_TAG_BOOL)) return SERIALIZE_ERROR_BUFFER;
        if (!serial_write_u8(buf, value->as.boolean ? 1 : 0)) return SERIALIZE_ERROR_BUFFER;
        break;

    case VAL_INT:
        if (!serial_write_u8(buf, SERIAL_TAG_INT)) return SERIALIZE_ERROR_BUFFER;
        if (!serial_write_i64(buf, value->as.integer)) return SERIALIZE_ERROR_BUFFER;
        break;

    case VAL_FLOAT:
        if (!serial_write_u8(buf, SERIAL_TAG_FLOAT)) return SERIALIZE_ERROR_BUFFER;
        if (!serial_write_f64(buf, value->as.floating)) return SERIALIZE_ERROR_BUFFER;
        break;

    case VAL_STRING:
        if (!serial_write_u8(buf, SERIAL_TAG_STRING)) return SERIALIZE_ERROR_BUFFER;
        if (!value->as.string) {
            if (!serial_write_u32(buf, 0)) return SERIALIZE_ERROR_BUFFER;
        } else {
            if (!serial_write_u32(buf, (uint32_t)value->as.string->length)) return SERIALIZE_ERROR_BUFFER;
            if (!serial_write_bytes(buf, (uint8_t *)value->as.string->data, value->as.string->length))
                return SERIALIZE_ERROR_BUFFER;
        }
        break;

    case VAL_PID:
        if (!serial_write_u8(buf, SERIAL_TAG_PID)) return SERIALIZE_ERROR_BUFFER;
        if (!serial_write_u64(buf, value->as.pid)) return SERIALIZE_ERROR_BUFFER;
        break;

    case VAL_ARRAY: {
        if (!serial_write_u8(buf, SERIAL_TAG_ARRAY)) return SERIALIZE_ERROR_BUFFER;
        size_t len = array_length(value);
        if (!serial_write_u32(buf, (uint32_t)len)) return SERIALIZE_ERROR_BUFFER;
        for (size_t i = 0; i < len; i++) {
            Value *elem = array_get(value, i);
            SerializeResult res = serialize_value(elem, buf);
            if (res != SERIALIZE_OK) return res;
        }
        break;
    }

    case VAL_MAP: {
        if (!serial_write_u8(buf, SERIAL_TAG_MAP)) return SERIALIZE_ERROR_BUFFER;
        /* Get map keys */
        Value *keys = map_keys(value);
        size_t len = keys ? array_length(keys) : 0;
        if (!serial_write_u32(buf, (uint32_t)len)) return SERIALIZE_ERROR_BUFFER;
        for (size_t i = 0; i < len; i++) {
            Value *key = array_get(keys, i);
            if (!key || !value_is_string(key)) continue;

            /* Write key */
            if (!serial_write_string(buf, key->as.string->data)) return SERIALIZE_ERROR_BUFFER;

            /* Write value */
            Value *val = map_get(value, key->as.string->data);
            SerializeResult res = serialize_value(val, buf);
            if (res != SERIALIZE_OK) return res;
        }
        break;
    }

    case VAL_BYTES: {
        if (!serial_write_u8(buf, SERIAL_TAG_BYTES)) return SERIALIZE_ERROR_BUFFER;
        size_t len = value->as.bytes ? value->as.bytes->length : 0;
        if (!serial_write_u32(buf, (uint32_t)len)) return SERIALIZE_ERROR_BUFFER;
        if (len > 0 && value->as.bytes->data) {
            if (!serial_write_bytes(buf, value->as.bytes->data, len)) return SERIALIZE_ERROR_BUFFER;
        }
        break;
    }

    case VAL_RESULT: {
        if (!serial_write_u8(buf, SERIAL_TAG_RESULT)) return SERIALIZE_ERROR_BUFFER;
        if (!serial_write_u8(buf, value->as.result->is_ok ? 1 : 0)) return SERIALIZE_ERROR_BUFFER;
        SerializeResult res = serialize_value(value->as.result->value, buf);
        if (res != SERIALIZE_OK) return res;
        break;
    }

    case VAL_OPTION: {
        if (!serial_write_u8(buf, SERIAL_TAG_OPTION)) return SERIALIZE_ERROR_BUFFER;
        bool is_some = value->as.option && value->as.option->value != NULL;
        if (!serial_write_u8(buf, is_some ? 1 : 0)) return SERIALIZE_ERROR_BUFFER;
        if (is_some) {
            SerializeResult res = serialize_value(value->as.option->value, buf);
            if (res != SERIALIZE_OK) return res;
        }
        break;
    }

    case VAL_FUNCTION:
    case VAL_CLOSURE:
        /* Functions cannot be serialized (code references) */
        return SERIALIZE_ERROR_UNSUPPORTED;

    case VAL_VECTOR:
        /* Vectors need special handling (persistent data structure) */
        return SERIALIZE_ERROR_UNSUPPORTED;

    case VAL_STRUCT:
    case VAL_ENUM:
        /* TODO: Implement struct/enum serialization */
        return SERIALIZE_ERROR_UNSUPPORTED;

    default:
        return SERIALIZE_ERROR_UNSUPPORTED;
    }

    return SERIALIZE_OK;
}

/*============================================================================
 * Value Deserialization
 *============================================================================*/

Value *deserialize_value(SerialBuffer *buf, SerializeResult *result) {
    if (!buf) {
        if (result) *result = SERIALIZE_ERROR_BUFFER;
        return NULL;
    }

    uint8_t tag;
    if (!serial_read_u8(buf, &tag)) {
        if (result) *result = SERIALIZE_ERROR_CORRUPT;
        return NULL;
    }

    switch (tag) {
    case SERIAL_TAG_NIL:
        if (result) *result = SERIALIZE_OK;
        return value_nil();

    case SERIAL_TAG_BOOL: {
        uint8_t b;
        if (!serial_read_u8(buf, &b)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        if (result) *result = SERIALIZE_OK;
        return value_bool(b != 0);
    }

    case SERIAL_TAG_INT: {
        int64_t v;
        if (!serial_read_i64(buf, &v)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        if (result) *result = SERIALIZE_OK;
        return value_int(v);
    }

    case SERIAL_TAG_FLOAT: {
        double v;
        if (!serial_read_f64(buf, &v)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        if (result) *result = SERIALIZE_OK;
        return value_float(v);
    }

    case SERIAL_TAG_STRING: {
        uint32_t len;
        if (!serial_read_u32(buf, &len)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        if (len == 0) {
            if (result) *result = SERIALIZE_OK;
            return value_string("");
        }
        if (buf->read_pos + len > buf->size) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        Value *str = value_string_n((char *)buf->data + buf->read_pos, len);
        buf->read_pos += len;
        if (result) *result = SERIALIZE_OK;
        return str;
    }

    case SERIAL_TAG_PID: {
        uint64_t pid;
        if (!serial_read_u64(buf, &pid)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        if (result) *result = SERIALIZE_OK;
        return value_pid(pid);
    }

    case SERIAL_TAG_ARRAY: {
        uint32_t len;
        if (!serial_read_u32(buf, &len)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        Value *arr = value_array();
        for (uint32_t i = 0; i < len; i++) {
            SerializeResult elem_result;
            Value *elem = deserialize_value(buf, &elem_result);
            if (elem_result != SERIALIZE_OK) {
                if (result) *result = elem_result;
                return NULL;
            }
            array_push(arr, elem);
        }
        if (result) *result = SERIALIZE_OK;
        return arr;
    }

    case SERIAL_TAG_MAP: {
        uint32_t len;
        if (!serial_read_u32(buf, &len)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        Value *map = value_map();
        for (uint32_t i = 0; i < len; i++) {
            char *key = serial_read_string(buf);
            if (!key) {
                if (result) *result = SERIALIZE_ERROR_CORRUPT;
                return NULL;
            }
            SerializeResult val_result;
            Value *val = deserialize_value(buf, &val_result);
            if (val_result != SERIALIZE_OK) {
                free(key);
                if (result) *result = val_result;
                return NULL;
            }
            map_set(map, key, val);
            free(key);
        }
        if (result) *result = SERIALIZE_OK;
        return map;
    }

    case SERIAL_TAG_BYTES: {
        uint32_t len;
        if (!serial_read_u32(buf, &len)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        if (buf->read_pos + len > buf->size) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        Value *bytes = value_bytes(len);
        if (len > 0) {
            bytes_append(bytes, buf->data + buf->read_pos, len);
        }
        buf->read_pos += len;
        if (result) *result = SERIALIZE_OK;
        return bytes;
    }

    case SERIAL_TAG_RESULT: {
        uint8_t is_ok;
        if (!serial_read_u8(buf, &is_ok)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        SerializeResult val_result;
        Value *val = deserialize_value(buf, &val_result);
        if (val_result != SERIALIZE_OK) {
            if (result) *result = val_result;
            return NULL;
        }
        if (result) *result = SERIALIZE_OK;
        return is_ok ? value_result_ok(val) : value_result_err(val);
    }

    case SERIAL_TAG_OPTION: {
        uint8_t is_some;
        if (!serial_read_u8(buf, &is_some)) {
            if (result) *result = SERIALIZE_ERROR_CORRUPT;
            return NULL;
        }
        if (!is_some) {
            if (result) *result = SERIALIZE_OK;
            return value_none();
        }
        SerializeResult val_result;
        Value *val = deserialize_value(buf, &val_result);
        if (val_result != SERIALIZE_OK) {
            if (result) *result = val_result;
            return NULL;
        }
        if (result) *result = SERIALIZE_OK;
        return value_some(val);
    }

    default:
        if (result) *result = SERIALIZE_ERROR_UNSUPPORTED;
        return NULL;
    }
}
