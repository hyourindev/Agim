/*
 * Agim - Value Serialization
 *
 * Serialization and deserialization of Tofu values for persistence,
 * checkpointing, and distribution.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_SERIALIZE_H
#define AGIM_RUNTIME_SERIALIZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm/value.h"

/*============================================================================
 * Serial Buffer
 *============================================================================*/

/**
 * Growable buffer for serialized data.
 */
typedef struct SerialBuffer {
    uint8_t *data;              /* Buffer data */
    size_t size;                /* Used size */
    size_t capacity;            /* Allocated capacity */
    size_t read_pos;            /* Current read position (for deserialize) */
} SerialBuffer;

/**
 * Initialize a serial buffer.
 */
void serial_buffer_init(SerialBuffer *buf);

/**
 * Initialize with existing data (for deserialization).
 */
void serial_buffer_init_data(SerialBuffer *buf, const uint8_t *data, size_t size);

/**
 * Free buffer resources.
 */
void serial_buffer_free(SerialBuffer *buf);

/**
 * Ensure capacity for at least 'needed' more bytes.
 */
bool serial_buffer_ensure(SerialBuffer *buf, size_t needed);

/**
 * Get current read position.
 */
size_t serial_buffer_position(const SerialBuffer *buf);

/**
 * Get remaining bytes to read.
 */
size_t serial_buffer_remaining(const SerialBuffer *buf);

/*============================================================================
 * Serialization Result
 *============================================================================*/

typedef enum SerializeResult {
    SERIALIZE_OK,
    SERIALIZE_ERROR_BUFFER,
    SERIALIZE_ERROR_UNSUPPORTED,
    SERIALIZE_ERROR_CORRUPT,
    SERIALIZE_ERROR_VERSION,
    SERIALIZE_ERROR_OVERFLOW,
} SerializeResult;

/*============================================================================
 * Value Serialization
 *============================================================================*/

/**
 * Serialize a value to a buffer.
 */
SerializeResult serialize_value(Value *value, SerialBuffer *buf);

/**
 * Deserialize a value from a buffer.
 * Returns NULL on error.
 */
Value *deserialize_value(SerialBuffer *buf, SerializeResult *result);

/*============================================================================
 * Primitive Serialization Helpers
 *============================================================================*/

/**
 * Write primitive types.
 */
bool serial_write_u8(SerialBuffer *buf, uint8_t value);
bool serial_write_u16(SerialBuffer *buf, uint16_t value);
bool serial_write_u32(SerialBuffer *buf, uint32_t value);
bool serial_write_u64(SerialBuffer *buf, uint64_t value);
bool serial_write_i64(SerialBuffer *buf, int64_t value);
bool serial_write_f64(SerialBuffer *buf, double value);
bool serial_write_bytes(SerialBuffer *buf, const uint8_t *data, size_t len);
bool serial_write_string(SerialBuffer *buf, const char *str);

/**
 * Read primitive types.
 */
bool serial_read_u8(SerialBuffer *buf, uint8_t *value);
bool serial_read_u16(SerialBuffer *buf, uint16_t *value);
bool serial_read_u32(SerialBuffer *buf, uint32_t *value);
bool serial_read_u64(SerialBuffer *buf, uint64_t *value);
bool serial_read_i64(SerialBuffer *buf, int64_t *value);
bool serial_read_f64(SerialBuffer *buf, double *value);
bool serial_read_bytes(SerialBuffer *buf, uint8_t *data, size_t len);
char *serial_read_string(SerialBuffer *buf);

/*============================================================================
 * Type Tags for Serialization
 *============================================================================*/

#define SERIAL_TAG_NIL      0x00
#define SERIAL_TAG_BOOL     0x01
#define SERIAL_TAG_INT      0x02
#define SERIAL_TAG_FLOAT    0x03
#define SERIAL_TAG_STRING   0x04
#define SERIAL_TAG_ARRAY    0x05
#define SERIAL_TAG_MAP      0x06
#define SERIAL_TAG_PID      0x07
#define SERIAL_TAG_FUNCTION 0x08
#define SERIAL_TAG_BYTES    0x09
#define SERIAL_TAG_RESULT   0x0A
#define SERIAL_TAG_OPTION   0x0B
#define SERIAL_TAG_STRUCT   0x0C
#define SERIAL_TAG_ENUM     0x0D
#define SERIAL_TAG_VECTOR   0x0E
#define SERIAL_TAG_CLOSURE  0x0F

/* Serialization format version */
#define SERIAL_VERSION 1

#endif /* AGIM_RUNTIME_SERIALIZE_H */
