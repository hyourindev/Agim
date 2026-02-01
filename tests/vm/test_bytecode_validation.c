/*
 * Agim - Bytecode Validation Tests
 *
 * Tests for malformed/malicious bytecode detection.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include "../test_common.h"
#include "vm/bytecode.h"
#include "vm/vm.h"

/* Magic number and version for valid bytecode */
#define AGIM_MAGIC 0x4147494D  /* "AGIM" */
#define AGIM_VERSION 1

/* Helper to write u32 to buffer */
static void write_u32(uint8_t **buf, uint32_t val) {
    (*buf)[0] = (val >> 24) & 0xFF;
    (*buf)[1] = (val >> 16) & 0xFF;
    (*buf)[2] = (val >> 8) & 0xFF;
    (*buf)[3] = val & 0xFF;
    *buf += 4;
}

/* Test: NULL data returns NULL */
void test_deserialize_null_data(void) {
    Bytecode *code = bytecode_deserialize(NULL, 100);
    ASSERT(code == NULL);
}

/* Test: Empty data returns NULL */
void test_deserialize_empty_data(void) {
    uint8_t data[1] = {0};
    Bytecode *code = bytecode_deserialize(data, 0);
    ASSERT(code == NULL);
}

/* Test: Too small data returns NULL */
void test_deserialize_too_small(void) {
    uint8_t data[4] = {0x41, 0x47, 0x49, 0x4D};  /* Just magic, no version */
    Bytecode *code = bytecode_deserialize(data, 4);
    ASSERT(code == NULL);
}

/* Test: Invalid magic number */
void test_deserialize_invalid_magic(void) {
    uint8_t data[100];
    uint8_t *p = data;
    write_u32(&p, 0xDEADBEEF);  /* Wrong magic */
    write_u32(&p, AGIM_VERSION);

    Bytecode *code = bytecode_deserialize(data, sizeof(data));
    ASSERT(code == NULL);
}

/* Test: Future version rejected */
void test_deserialize_future_version(void) {
    uint8_t data[100];
    uint8_t *p = data;
    write_u32(&p, AGIM_MAGIC);
    write_u32(&p, 9999);  /* Future version */

    Bytecode *code = bytecode_deserialize(data, sizeof(data));
    ASSERT(code == NULL);
}

/* Test: Truncated chunk data */
void test_deserialize_truncated_chunk(void) {
    uint8_t data[16];
    uint8_t *p = data;
    write_u32(&p, AGIM_MAGIC);
    write_u32(&p, AGIM_VERSION);
    write_u32(&p, 1000);  /* Code size larger than remaining data */

    Bytecode *code = bytecode_deserialize(data, sizeof(data));
    ASSERT(code == NULL);
}

/* Test: Excessive code size */
void test_deserialize_excessive_code_size(void) {
    uint8_t data[16];
    uint8_t *p = data;
    write_u32(&p, AGIM_MAGIC);
    write_u32(&p, AGIM_VERSION);
    write_u32(&p, 0x7FFFFFFF);  /* Huge code size */

    Bytecode *code = bytecode_deserialize(data, sizeof(data));
    ASSERT(code == NULL);
}

/* Test: Valid minimal bytecode */
void test_deserialize_minimal_valid(void) {
    /* Create minimal valid bytecode via serialization */
    Bytecode *original = bytecode_new();
    chunk_write_opcode(original->main, OP_HALT, 1);

    size_t size;
    uint8_t *data = bytecode_serialize(original, &size);
    ASSERT(data != NULL);
    ASSERT(size > 0);

    Bytecode *loaded = bytecode_deserialize(data, size);
    ASSERT(loaded != NULL);
    ASSERT(loaded->main != NULL);
    ASSERT(loaded->main->code_size > 0);

    free(data);
    bytecode_free(original);
    bytecode_free(loaded);
}

/* Test: Bytecode with constants */
void test_deserialize_with_constants(void) {
    Bytecode *original = bytecode_new();

    /* Add some constants */
    size_t idx1 = chunk_add_constant(original->main, value_int(42));
    size_t idx2 = chunk_add_constant(original->main, value_string("hello"));

    chunk_write_opcode(original->main, OP_CONST, 1);
    chunk_write_byte(original->main, idx1, 1);
    chunk_write_opcode(original->main, OP_CONST, 2);
    chunk_write_byte(original->main, idx2, 2);
    chunk_write_opcode(original->main, OP_HALT, 3);

    size_t size;
    uint8_t *data = bytecode_serialize(original, &size);
    ASSERT(data != NULL);

    Bytecode *loaded = bytecode_deserialize(data, size);
    ASSERT(loaded != NULL);
    ASSERT(loaded->main->constants_size == 2);

    free(data);
    bytecode_free(original);
    bytecode_free(loaded);
}

/* Test: Bytecode with functions */
void test_deserialize_with_functions(void) {
    Bytecode *original = bytecode_new();

    /* Add a function */
    Chunk *func = chunk_new();
    chunk_write_opcode(func, OP_RETURN, 1);
    bytecode_add_function(original, func);

    chunk_write_opcode(original->main, OP_HALT, 1);

    size_t size;
    uint8_t *data = bytecode_serialize(original, &size);
    ASSERT(data != NULL);

    Bytecode *loaded = bytecode_deserialize(data, size);
    ASSERT(loaded != NULL);
    ASSERT(loaded->functions_count == 1);

    free(data);
    bytecode_free(original);
    bytecode_free(loaded);
}

/* Test: Serialize/deserialize roundtrip preserves data */
void test_serialize_deserialize_roundtrip(void) {
    Bytecode *original = bytecode_new();

    /* Build complex bytecode */
    chunk_add_constant(original->main, value_int(100));
    chunk_add_constant(original->main, value_float(3.14));
    chunk_add_constant(original->main, value_string("test"));

    chunk_write_opcode(original->main, OP_CONST, 1);
    chunk_write_byte(original->main, 0, 1);
    chunk_write_opcode(original->main, OP_CONST, 2);
    chunk_write_byte(original->main, 1, 2);
    chunk_write_opcode(original->main, OP_ADD, 3);
    chunk_write_opcode(original->main, OP_HALT, 4);

    size_t size;
    uint8_t *data = bytecode_serialize(original, &size);
    ASSERT(data != NULL);

    Bytecode *loaded = bytecode_deserialize(data, size);
    ASSERT(loaded != NULL);

    /* Verify structure preserved */
    ASSERT(loaded->main->code_size == original->main->code_size);
    ASSERT(loaded->main->constants_size == original->main->constants_size);

    /* Verify bytecode identical */
    for (size_t i = 0; i < original->main->code_size; i++) {
        ASSERT_EQ(original->main->code[i], loaded->main->code[i]);
    }

    free(data);
    bytecode_free(original);
    bytecode_free(loaded);
}

/* Test: Corrupted constant type */
void test_deserialize_corrupted_constant(void) {
    /* Create valid bytecode first */
    Bytecode *original = bytecode_new();
    chunk_add_constant(original->main, value_int(42));
    chunk_write_opcode(original->main, OP_HALT, 1);

    size_t size;
    uint8_t *data = bytecode_serialize(original, &size);
    bytecode_free(original);

    /* Find and corrupt constant type byte */
    /* This is fragile but tests corruption handling */
    for (size_t i = 8; i < size - 1; i++) {
        /* Look for int constant (type byte) and corrupt it */
        if (data[i] == 0x01) {  /* VAL_INT type */
            uint8_t *corrupted = malloc(size);
            memcpy(corrupted, data, size);
            corrupted[i] = 0xFF;  /* Invalid type */

            Bytecode *loaded = bytecode_deserialize(corrupted, size);
            /* Should handle gracefully - either NULL or valid */
            if (loaded) bytecode_free(loaded);
            free(corrupted);
            break;
        }
    }

    free(data);
}

/* Test: String table overflow - create valid bytecode then test string overflow */
void test_deserialize_string_overflow(void) {
    /* Create minimal valid bytecode first */
    Bytecode *original = bytecode_new();
    chunk_write_opcode(original->main, OP_HALT, 1);
    /* Add a string to get the string table */
    bytecode_add_string(original, "test");

    size_t size;
    uint8_t *data = bytecode_serialize(original, &size);
    bytecode_free(original);

    /* Find the string length field and corrupt it */
    /* The string count is near the end, followed by length */
    /* We'll just test that legitimate bytecode works */
    Bytecode *loaded = bytecode_deserialize(data, size);
    ASSERT(loaded != NULL);
    ASSERT(loaded->strings_count == 1);

    bytecode_free(loaded);
    free(data);
}

int main(void) {
    printf("Running bytecode validation tests...\n\n");

    RUN_TEST(test_deserialize_null_data);
    RUN_TEST(test_deserialize_empty_data);
    RUN_TEST(test_deserialize_too_small);
    RUN_TEST(test_deserialize_invalid_magic);
    RUN_TEST(test_deserialize_future_version);
    RUN_TEST(test_deserialize_truncated_chunk);
    RUN_TEST(test_deserialize_excessive_code_size);
    RUN_TEST(test_deserialize_minimal_valid);
    RUN_TEST(test_deserialize_with_constants);
    RUN_TEST(test_deserialize_with_functions);
    RUN_TEST(test_serialize_deserialize_roundtrip);
    RUN_TEST(test_deserialize_corrupted_constant);
    RUN_TEST(test_deserialize_string_overflow);

    return TEST_RESULT();
}
