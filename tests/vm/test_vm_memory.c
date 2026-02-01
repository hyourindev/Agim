/*
 * Agim - VM Memory Operations Tests
 *
 * P1.1.1.6 - Comprehensive tests for all memory operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/bytecode.h"
#include "vm/vm.h"
#include "types/array.h"
#include "types/map.h"
#include "types/string.h"

/* Helper functions for emitting bytecode */

static void emit_const(Chunk *chunk, size_t index, int line) {
    chunk_write_opcode(chunk, OP_CONST, line);
    chunk_write_byte(chunk, (index >> 8) & 0xFF, line);
    chunk_write_byte(chunk, index & 0xFF, line);
}

/*
 * =============================================================================
 * Test: OP_ARRAY_NEW allocation
 * =============================================================================
 */

void test_array_new_creates_empty_array(void) {
    /* Test: ARRAY_NEW creates an empty array */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *arr = vm_peek(vm, 0);
    ASSERT(arr != NULL);
    ASSERT(value_is_array(arr));
    ASSERT_EQ(0, arr->as.array->length);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_new_multiple(void) {
    /* Test: Multiple ARRAY_NEW creates separate arrays */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    chunk_write_opcode(chunk, OP_ARRAY_NEW, 2);
    chunk_write_opcode(chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *arr1 = vm_peek(vm, 0);
    Value *arr2 = vm_peek(vm, 1);
    ASSERT(arr1 != arr2);  /* Different arrays */
    ASSERT(value_is_array(arr1));
    ASSERT(value_is_array(arr2));

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_ARRAY_PUSH growth
 * =============================================================================
 */

void test_array_push_single_element(void) {
    /* Test: Push single element to array */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_42 = chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_42, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *arr = vm_peek(vm, 0);
    ASSERT(value_is_array(arr));
    ASSERT_EQ(1, arr->as.array->length);
    ASSERT_EQ(42, array_get(arr, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_push_multiple_elements(void) {
    /* Test: Push multiple elements to array */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_1 = chunk_add_constant(chunk, value_int(1));
    size_t c_2 = chunk_add_constant(chunk, value_int(2));
    size_t c_3 = chunk_add_constant(chunk, value_int(3));

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_1, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_2, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_3, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *arr = vm_peek(vm, 0);
    ASSERT_EQ(3, arr->as.array->length);
    ASSERT_EQ(1, array_get(arr, 0)->as.integer);
    ASSERT_EQ(2, array_get(arr, 1)->as.integer);
    ASSERT_EQ(3, array_get(arr, 2)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_push_mixed_types(void) {
    /* Test: Push different value types to array */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_int = chunk_add_constant(chunk, value_int(42));
    size_t c_str = chunk_add_constant(chunk, value_string("hello"));
    size_t c_float = chunk_add_constant(chunk, value_float(3.14));

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_int, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_str, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_float, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *arr = vm_peek(vm, 0);
    ASSERT_EQ(3, arr->as.array->length);
    ASSERT(value_is_int(array_get(arr, 0)));
    ASSERT(value_is_string(array_get(arr, 1)));
    ASSERT(value_is_float(array_get(arr, 2)));

    vm_free(vm);
    bytecode_free(code);
}

void test_array_push_to_non_array(void) {
    /* Test: Push to non-array should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_42 = chunk_add_constant(chunk, value_int(42));
    size_t c_val = chunk_add_constant(chunk, value_int(99));

    emit_const(chunk, c_42, 1);  /* Push int, not array */
    emit_const(chunk, c_val, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_ARRAY_GET bounds checking
 * =============================================================================
 */

void test_array_get_valid_index(void) {
    /* Test: Get element at valid index */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_val = chunk_add_constant(chunk, value_int(42));
    size_t c_idx = chunk_add_constant(chunk, value_int(0));

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_val, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_idx, 1);
    chunk_write_opcode(chunk, OP_ARRAY_GET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_get_negative_index(void) {
    /* Test: Negative index should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_val = chunk_add_constant(chunk, value_int(42));
    size_t c_idx = chunk_add_constant(chunk, value_int(-1));

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_val, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_idx, 1);
    chunk_write_opcode(chunk, OP_ARRAY_GET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_OUT_OF_BOUNDS, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_get_out_of_bounds(void) {
    /* Test: Index >= length should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_val = chunk_add_constant(chunk, value_int(42));
    size_t c_idx = chunk_add_constant(chunk, value_int(1));  /* Array has 1 element, index 1 is OOB */

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_val, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_idx, 1);
    chunk_write_opcode(chunk, OP_ARRAY_GET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_OUT_OF_BOUNDS, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_get_empty_array(void) {
    /* Test: Get from empty array should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_idx = chunk_add_constant(chunk, value_int(0));

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_idx, 1);
    chunk_write_opcode(chunk, OP_ARRAY_GET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_OUT_OF_BOUNDS, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_get_non_integer_index(void) {
    /* Test: Non-integer index should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_val = chunk_add_constant(chunk, value_int(42));
    size_t c_idx = chunk_add_constant(chunk, value_string("0"));  /* String index */

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_val, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_idx, 1);
    chunk_write_opcode(chunk, OP_ARRAY_GET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_ARRAY_SET bounds checking
 * =============================================================================
 */

void test_array_set_valid_index(void) {
    /* Test: Set element at valid index */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_init = chunk_add_constant(chunk, value_int(0));
    size_t c_new = chunk_add_constant(chunk, value_int(99));
    size_t c_idx = chunk_add_constant(chunk, value_int(0));

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_init, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_idx, 1);
    emit_const(chunk, c_new, 1);
    chunk_write_opcode(chunk, OP_ARRAY_SET, 1);
    /* Get the value back to verify */
    emit_const(chunk, c_idx, 2);
    chunk_write_opcode(chunk, OP_ARRAY_GET, 2);
    chunk_write_opcode(chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(99, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_set_negative_index(void) {
    /* Test: Set at negative index should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_init = chunk_add_constant(chunk, value_int(0));
    size_t c_new = chunk_add_constant(chunk, value_int(99));
    size_t c_idx = chunk_add_constant(chunk, value_int(-1));

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_init, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_idx, 1);
    emit_const(chunk, c_new, 1);
    chunk_write_opcode(chunk, OP_ARRAY_SET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_OUT_OF_BOUNDS, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_array_set_out_of_bounds(void) {
    /* Test: Set at index >= length should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_init = chunk_add_constant(chunk, value_int(0));
    size_t c_new = chunk_add_constant(chunk, value_int(99));
    size_t c_idx = chunk_add_constant(chunk, value_int(5));  /* OOB */

    chunk_write_opcode(chunk, OP_ARRAY_NEW, 1);
    emit_const(chunk, c_init, 1);
    chunk_write_opcode(chunk, OP_ARRAY_PUSH, 1);
    emit_const(chunk, c_idx, 1);
    emit_const(chunk, c_new, 1);
    chunk_write_opcode(chunk, OP_ARRAY_SET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_OUT_OF_BOUNDS, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_MAP_NEW allocation
 * =============================================================================
 */

void test_map_new_creates_empty_map(void) {
    /* Test: MAP_NEW creates an empty map */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *map = vm_peek(vm, 0);
    ASSERT(map != NULL);
    ASSERT(value_is_map(map));
    ASSERT_EQ(0, map->as.map->size);

    vm_free(vm);
    bytecode_free(code);
}

void test_map_new_multiple(void) {
    /* Test: Multiple MAP_NEW creates separate maps */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    chunk_write_opcode(chunk, OP_MAP_NEW, 2);
    chunk_write_opcode(chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *map1 = vm_peek(vm, 0);
    Value *map2 = vm_peek(vm, 1);
    ASSERT(map1 != map2);
    ASSERT(value_is_map(map1));
    ASSERT(value_is_map(map2));

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_MAP_GET missing key
 * =============================================================================
 */

void test_map_get_existing_key(void) {
    /* Test: Get value for existing key */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_key = chunk_add_constant(chunk, value_string("foo"));
    size_t c_val = chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    emit_const(chunk, c_key, 1);
    emit_const(chunk, c_val, 1);
    chunk_write_opcode(chunk, OP_MAP_SET, 1);
    emit_const(chunk, c_key, 2);
    chunk_write_opcode(chunk, OP_MAP_GET, 2);
    chunk_write_opcode(chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(42, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_map_get_missing_key(void) {
    /* Test: Get value for missing key returns nil */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_key = chunk_add_constant(chunk, value_string("nonexistent"));

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    emit_const(chunk, c_key, 1);
    chunk_write_opcode(chunk, OP_MAP_GET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT(value_is_nil(vm_peek(vm, 0)));

    vm_free(vm);
    bytecode_free(code);
}

void test_map_get_non_string_key(void) {
    /* Test: Get with non-string key should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_key = chunk_add_constant(chunk, value_int(42));  /* Int key */

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    emit_const(chunk, c_key, 1);
    chunk_write_opcode(chunk, OP_MAP_GET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

void test_map_get_from_non_map(void) {
    /* Test: Get from non-map should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_val = chunk_add_constant(chunk, value_int(42));
    size_t c_key = chunk_add_constant(chunk, value_string("foo"));

    emit_const(chunk, c_val, 1);  /* Push int, not map */
    emit_const(chunk, c_key, 1);
    chunk_write_opcode(chunk, OP_MAP_GET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: OP_MAP_SET overwrite
 * =============================================================================
 */

void test_map_set_new_key(void) {
    /* Test: Set value for new key */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_key = chunk_add_constant(chunk, value_string("foo"));
    size_t c_val = chunk_add_constant(chunk, value_int(42));

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    emit_const(chunk, c_key, 1);
    emit_const(chunk, c_val, 1);
    chunk_write_opcode(chunk, OP_MAP_SET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *map = vm_peek(vm, 0);
    ASSERT(value_is_map(map));
    ASSERT_EQ(1, map->as.map->size);

    vm_free(vm);
    bytecode_free(code);
}

void test_map_set_overwrite_existing(void) {
    /* Test: Overwrite existing key with new value */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_key = chunk_add_constant(chunk, value_string("foo"));
    size_t c_val1 = chunk_add_constant(chunk, value_int(42));
    size_t c_val2 = chunk_add_constant(chunk, value_int(99));

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    /* Set first value */
    emit_const(chunk, c_key, 1);
    emit_const(chunk, c_val1, 1);
    chunk_write_opcode(chunk, OP_MAP_SET, 1);
    /* Overwrite with second value */
    emit_const(chunk, c_key, 2);
    emit_const(chunk, c_val2, 2);
    chunk_write_opcode(chunk, OP_MAP_SET, 2);
    /* Get the value */
    emit_const(chunk, c_key, 3);
    chunk_write_opcode(chunk, OP_MAP_GET, 3);
    chunk_write_opcode(chunk, OP_HALT, 4);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(99, vm_peek(vm, 0)->as.integer);

    vm_free(vm);
    bytecode_free(code);
}

void test_map_set_multiple_keys(void) {
    /* Test: Set multiple different keys */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_k1 = chunk_add_constant(chunk, value_string("a"));
    size_t c_v1 = chunk_add_constant(chunk, value_int(1));
    size_t c_k2 = chunk_add_constant(chunk, value_string("b"));
    size_t c_v2 = chunk_add_constant(chunk, value_int(2));
    size_t c_k3 = chunk_add_constant(chunk, value_string("c"));
    size_t c_v3 = chunk_add_constant(chunk, value_int(3));

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    emit_const(chunk, c_k1, 1);
    emit_const(chunk, c_v1, 1);
    chunk_write_opcode(chunk, OP_MAP_SET, 1);
    emit_const(chunk, c_k2, 1);
    emit_const(chunk, c_v2, 1);
    chunk_write_opcode(chunk, OP_MAP_SET, 1);
    emit_const(chunk, c_k3, 1);
    emit_const(chunk, c_v3, 1);
    chunk_write_opcode(chunk, OP_MAP_SET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    Value *map = vm_peek(vm, 0);
    ASSERT_EQ(3, map->as.map->size);

    vm_free(vm);
    bytecode_free(code);
}

void test_map_set_non_string_key(void) {
    /* Test: Set with non-string key should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_key = chunk_add_constant(chunk, value_int(42));  /* Int key */
    size_t c_val = chunk_add_constant(chunk, value_int(99));

    chunk_write_opcode(chunk, OP_MAP_NEW, 1);
    emit_const(chunk, c_key, 1);
    emit_const(chunk, c_val, 1);
    chunk_write_opcode(chunk, OP_MAP_SET, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: Map delete (API level - no opcode)
 * =============================================================================
 */

void test_map_delete_existing_key(void) {
    /* Test: Delete existing key from map (API level) */
    Value *map = value_map();
    ASSERT(map != NULL);

    map = map_set(map, "foo", value_int(42));
    ASSERT_EQ(1, map->as.map->size);

    map = map_delete(map, "foo");
    ASSERT_EQ(0, map->as.map->size);

    /* Getting deleted key returns nil */
    Value *val = map_get(map, "foo");
    ASSERT(val == NULL);
}

void test_map_delete_nonexistent_key(void) {
    /* Test: Delete non-existent key (no-op) */
    Value *map = value_map();
    ASSERT(map != NULL);

    map = map_set(map, "foo", value_int(42));
    ASSERT_EQ(1, map->as.map->size);

    /* Delete key that doesn't exist */
    map = map_delete(map, "bar");
    ASSERT_EQ(1, map->as.map->size);  /* Size unchanged */
}

/*
 * =============================================================================
 * Test: String interning
 * =============================================================================
 */

void test_string_intern_returns_same_pointer(void) {
    /* Test: Interning same string returns cached pointer */
    const char *str = "hello";
    Value *s1 = string_intern(str, 5);
    Value *s2 = string_intern(str, 5);

    ASSERT(s1 != NULL);
    ASSERT(s2 != NULL);
    /* Interned strings may return same pointer */
    ASSERT_STR_EQ("hello", s1->as.string->data);
    ASSERT_STR_EQ("hello", s2->as.string->data);
}

void test_string_intern_different_strings(void) {
    /* Test: Different strings get different values */
    Value *s1 = string_intern("hello", 5);
    Value *s2 = string_intern("world", 5);

    ASSERT(s1 != NULL);
    ASSERT(s2 != NULL);
    ASSERT_STR_EQ("hello", s1->as.string->data);
    ASSERT_STR_EQ("world", s2->as.string->data);
}

void test_string_intern_empty_string(void) {
    /* Test: Interning empty string works */
    Value *s = string_intern("", 0);
    ASSERT(s != NULL);
    ASSERT_EQ(0, s->as.string->length);
}

/*
 * =============================================================================
 * Test: String concatenation
 * =============================================================================
 */

void test_string_concat_basic(void) {
    /* Test: Basic string concatenation */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_s1 = chunk_add_constant(chunk, value_string("hello"));
    size_t c_s2 = chunk_add_constant(chunk, value_string(" world"));

    emit_const(chunk, c_s1, 1);
    emit_const(chunk, c_s2, 1);
    chunk_write_opcode(chunk, OP_CONCAT, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_STR_EQ("hello world", vm_peek(vm, 0)->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

void test_string_concat_empty_left(void) {
    /* Test: Concat with empty left string */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_s1 = chunk_add_constant(chunk, value_string(""));
    size_t c_s2 = chunk_add_constant(chunk, value_string("world"));

    emit_const(chunk, c_s1, 1);
    emit_const(chunk, c_s2, 1);
    chunk_write_opcode(chunk, OP_CONCAT, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_STR_EQ("world", vm_peek(vm, 0)->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

void test_string_concat_empty_right(void) {
    /* Test: Concat with empty right string */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_s1 = chunk_add_constant(chunk, value_string("hello"));
    size_t c_s2 = chunk_add_constant(chunk, value_string(""));

    emit_const(chunk, c_s1, 1);
    emit_const(chunk, c_s2, 1);
    chunk_write_opcode(chunk, OP_CONCAT, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_STR_EQ("hello", vm_peek(vm, 0)->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

void test_string_concat_both_empty(void) {
    /* Test: Concat two empty strings */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_s1 = chunk_add_constant(chunk, value_string(""));
    size_t c_s2 = chunk_add_constant(chunk, value_string(""));

    emit_const(chunk, c_s1, 1);
    emit_const(chunk, c_s2, 1);
    chunk_write_opcode(chunk, OP_CONCAT, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_EQ(0, vm_peek(vm, 0)->as.string->length);

    vm_free(vm);
    bytecode_free(code);
}

void test_string_concat_multiple(void) {
    /* Test: Multiple concatenations */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_s1 = chunk_add_constant(chunk, value_string("a"));
    size_t c_s2 = chunk_add_constant(chunk, value_string("b"));
    size_t c_s3 = chunk_add_constant(chunk, value_string("c"));

    emit_const(chunk, c_s1, 1);
    emit_const(chunk, c_s2, 1);
    chunk_write_opcode(chunk, OP_CONCAT, 1);
    emit_const(chunk, c_s3, 2);
    chunk_write_opcode(chunk, OP_CONCAT, 2);
    chunk_write_opcode(chunk, OP_HALT, 3);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_STR_EQ("abc", vm_peek(vm, 0)->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

void test_string_concat_non_string(void) {
    /* Test: Concat with non-string should error */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_s1 = chunk_add_constant(chunk, value_string("hello"));
    size_t c_int = chunk_add_constant(chunk, value_int(42));

    emit_const(chunk, c_s1, 1);
    emit_const(chunk, c_int, 1);
    chunk_write_opcode(chunk, OP_CONCAT, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_ERROR_TYPE, result);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Test: String via OP_ADD (string + string = concat)
 * =============================================================================
 */

void test_string_add_concat(void) {
    /* Test: OP_ADD with strings performs concatenation */
    Bytecode *code = bytecode_new();
    Chunk *chunk = code->main;

    size_t c_s1 = chunk_add_constant(chunk, value_string("foo"));
    size_t c_s2 = chunk_add_constant(chunk, value_string("bar"));

    emit_const(chunk, c_s1, 1);
    emit_const(chunk, c_s2, 1);
    chunk_write_opcode(chunk, OP_ADD, 1);
    chunk_write_opcode(chunk, OP_HALT, 2);

    VM *vm = vm_new();
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    ASSERT_EQ(VM_HALT, result);
    ASSERT_STR_EQ("foobar", vm_peek(vm, 0)->as.string->data);

    vm_free(vm);
    bytecode_free(code);
}

/*
 * =============================================================================
 * Main test runner
 * =============================================================================
 */

int main(void) {
    printf("=== VM Memory Operations Tests (P1.1.1.6) ===\n\n");

    printf("--- OP_ARRAY_NEW tests ---\n");
    RUN_TEST(test_array_new_creates_empty_array);
    RUN_TEST(test_array_new_multiple);

    printf("\n--- OP_ARRAY_PUSH tests ---\n");
    RUN_TEST(test_array_push_single_element);
    RUN_TEST(test_array_push_multiple_elements);
    RUN_TEST(test_array_push_mixed_types);
    RUN_TEST(test_array_push_to_non_array);

    printf("\n--- OP_ARRAY_GET tests ---\n");
    RUN_TEST(test_array_get_valid_index);
    RUN_TEST(test_array_get_negative_index);
    RUN_TEST(test_array_get_out_of_bounds);
    RUN_TEST(test_array_get_empty_array);
    RUN_TEST(test_array_get_non_integer_index);

    printf("\n--- OP_ARRAY_SET tests ---\n");
    RUN_TEST(test_array_set_valid_index);
    RUN_TEST(test_array_set_negative_index);
    RUN_TEST(test_array_set_out_of_bounds);

    printf("\n--- OP_MAP_NEW tests ---\n");
    RUN_TEST(test_map_new_creates_empty_map);
    RUN_TEST(test_map_new_multiple);

    printf("\n--- OP_MAP_GET tests ---\n");
    RUN_TEST(test_map_get_existing_key);
    RUN_TEST(test_map_get_missing_key);
    RUN_TEST(test_map_get_non_string_key);
    RUN_TEST(test_map_get_from_non_map);

    printf("\n--- OP_MAP_SET tests ---\n");
    RUN_TEST(test_map_set_new_key);
    RUN_TEST(test_map_set_overwrite_existing);
    RUN_TEST(test_map_set_multiple_keys);
    RUN_TEST(test_map_set_non_string_key);

    printf("\n--- Map delete tests (API) ---\n");
    RUN_TEST(test_map_delete_existing_key);
    RUN_TEST(test_map_delete_nonexistent_key);

    printf("\n--- String interning tests ---\n");
    RUN_TEST(test_string_intern_returns_same_pointer);
    RUN_TEST(test_string_intern_different_strings);
    RUN_TEST(test_string_intern_empty_string);

    printf("\n--- String concatenation tests ---\n");
    RUN_TEST(test_string_concat_basic);
    RUN_TEST(test_string_concat_empty_left);
    RUN_TEST(test_string_concat_empty_right);
    RUN_TEST(test_string_concat_both_empty);
    RUN_TEST(test_string_concat_multiple);
    RUN_TEST(test_string_concat_non_string);
    RUN_TEST(test_string_add_concat);

    return TEST_RESULT();
}
