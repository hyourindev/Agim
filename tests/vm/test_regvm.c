/*
 * Agim - Register VM Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/regvm.h"
#include "vm/value.h"

/* Basic Tests */

void test_regvm_create(void) {
    RegVM *vm = regvm_new();
    ASSERT(vm != NULL);
    ASSERT(vm->frame_count == 0);
    ASSERT(vm->globals != NULL);
    regvm_free(vm);
}

void test_regchunk_create(void) {
    RegChunk *chunk = regchunk_new();
    ASSERT(chunk != NULL);
    ASSERT_EQ(0, chunk->code_size);
    ASSERT_EQ(0, chunk->constants_size);
    regchunk_free(chunk);
}

/* Arithmetic Tests */

void test_regvm_add(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = 10, r1 = 20, r2 = r0 + r1, halt */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 10), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 20), 1);
    regchunk_write(chunk, reg_instr(ROP_ADD, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    /* Check r2 = 30 */
    ASSERT(nanbox_is_int(vm->frames[0].regs[2]));
    ASSERT_EQ(30, nanbox_as_int(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_regvm_sub(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = 50, r1 = 8, r2 = r0 - r1, halt */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 50), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 8), 1);
    regchunk_write(chunk, reg_instr(ROP_SUB, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(42, nanbox_as_int(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_regvm_mul(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = 6, r1 = 7, r2 = r0 * r1, halt */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 6), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 7), 1);
    regchunk_write(chunk, reg_instr(ROP_MUL, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(42, nanbox_as_int(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_regvm_div(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = 84, r1 = 2, r2 = r0 / r1, halt */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 84), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 2), 1);
    regchunk_write(chunk, reg_instr(ROP_DIV, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(42, nanbox_as_int(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

/* Comparison Tests */

void test_regvm_eq(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = 42, r1 = 42, r2 = r0 == r1, halt */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 42), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 42), 1);
    regchunk_write(chunk, reg_instr(ROP_EQ, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_is_bool(vm->frames[0].regs[2]));
    ASSERT(nanbox_as_bool(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_regvm_lt(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = 10, r1 = 20, r2 = r0 < r1, halt */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 10), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 20), 1);
    regchunk_write(chunk, reg_instr(ROP_LT, 2, 0, 1), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT(nanbox_as_bool(vm->frames[0].regs[2]));

    regchunk_free(chunk);
    regvm_free(vm);
}

/* Control Flow Tests */

void test_regvm_loop(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /*
     * r0 = 0       ; sum
     * r1 = 0       ; i
     * r2 = 10      ; limit
     * loop:
     *   r0 = r0 + r1  ; sum += i
     *   r1 = r1 + 1   ; i++
     *   r3 = r1 < r2  ; i < 10
     *   if r3 goto loop
     * halt
     *
     * Should compute: 0+1+2+...+9 = 45
     */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 0, 0), 1);   /* 0: r0 = 0 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 1, 0), 1);   /* 1: r1 = 0 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 2, 10), 1);  /* 2: r2 = 10 */
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 4, 1), 1);   /* 3: r4 = 1 (constant) */
    /* loop start at index 4 */
    regchunk_write(chunk, reg_instr(ROP_ADD, 0, 0, 1), 1);         /* 4: r0 = r0 + r1 */
    regchunk_write(chunk, reg_instr(ROP_ADD, 1, 1, 4), 1);         /* 5: r1 = r1 + 1 */
    regchunk_write(chunk, reg_instr(ROP_LT, 3, 1, 2), 1);          /* 6: r3 = r1 < r2 */
    /* Use conditional jump: rd=condition reg, offset in rs1/rs2 */
    regchunk_write(chunk, reg_instr_cond_jump(ROP_JMP_IF, 3, -4), 1); /* 7: if r3 goto 4 */
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);        /* 8: halt */

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(45, nanbox_as_int(vm->frames[0].regs[0]));

    regchunk_free(chunk);
    regvm_free(vm);
}

/* Data Structure Tests */

void test_regvm_array(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = [], r0.push(42), r1 = len(r0), halt */
    regchunk_write(chunk, reg_instr(ROP_ARRAY_NEW, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_INT, 2, 42), 1);
    regchunk_write(chunk, reg_instr(ROP_ARRAY_PUSH, 0, 2, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_LEN, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(1, nanbox_as_int(vm->frames[0].regs[1]));

    regchunk_free(chunk);
    regvm_free(vm);
}

void test_regvm_map(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* r0 = {}, r1 = len(r0), halt */
    regchunk_write(chunk, reg_instr(ROP_MAP_NEW, 0, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_LEN, 1, 0, 0), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);
    ASSERT_EQ(0, nanbox_as_int(vm->frames[0].regs[1]));

    regchunk_free(chunk);
    regvm_free(vm);
}

/* Constant Loading Tests */

void test_regvm_load_constant(void) {
    RegVM *vm = regvm_new();
    RegChunk *chunk = regchunk_new();

    /* Add a string constant and load it */
    size_t idx = regchunk_add_constant(chunk, value_string("hello"));
    regchunk_write(chunk, reg_instr_imm(ROP_LOAD_K, 0, (uint16_t)idx), 1);
    regchunk_write(chunk, reg_instr(ROP_HALT, 0, 0, 0), 1);

    RegVMResult result = regvm_run(vm, chunk);
    ASSERT_EQ(REGVM_HALT, result);

    /* r0 should contain the string */
    ASSERT(nanbox_is_obj(vm->frames[0].regs[0]));
    Value *v = (Value *)nanbox_as_obj(vm->frames[0].regs[0]);
    ASSERT(value_is_string(v));
    ASSERT_STR_EQ("hello", v->as.string->data);

    regchunk_free(chunk);
    regvm_free(vm);
}

/* Main */

int main(void) {
    /* Basic tests */
    RUN_TEST(test_regvm_create);
    RUN_TEST(test_regchunk_create);

    /* Arithmetic tests */
    RUN_TEST(test_regvm_add);
    RUN_TEST(test_regvm_sub);
    RUN_TEST(test_regvm_mul);
    RUN_TEST(test_regvm_div);

    /* Comparison tests */
    RUN_TEST(test_regvm_eq);
    RUN_TEST(test_regvm_lt);

    /* Control flow tests */
    RUN_TEST(test_regvm_loop);

    /* Data structure tests */
    RUN_TEST(test_regvm_array);
    RUN_TEST(test_regvm_map);

    /* Constant loading tests */
    RUN_TEST(test_regvm_load_constant);

    return TEST_RESULT();
}
