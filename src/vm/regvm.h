/*
 * Agim - Register-Based Virtual Machine
 *
 * Register-based VM with 256 virtual registers per call frame
 * and 4-byte instructions for improved performance.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_REGVM_H
#define AGIM_VM_REGVM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "vm/nanbox.h"
#include "vm/bytecode.h"

/*
 * Instruction Format (32 bits):
 *   [op:8][rd:8][rs1:8][rs2:8]    - 3-register format
 *   [op:8][rd:8][imm:16]          - immediate format
 *   [op:8][offset:24]             - jump format
 */

typedef union RegInstr {
    uint32_t raw;
    struct {
        uint8_t op;
        uint8_t rd;
        uint8_t rs1;
        uint8_t rs2;
    };
} RegInstr;

static inline uint16_t reg_get_imm(RegInstr i) {
    return ((uint16_t)i.rs1 << 8) | i.rs2;
}

static inline int32_t reg_get_offset(RegInstr i) {
    int32_t offset = ((int32_t)i.rd << 16) | ((int32_t)i.rs1 << 8) | i.rs2;
    if (offset & 0x800000) {
        offset |= 0xFF000000;
    }
    return offset;
}

/* Register Opcodes */

typedef enum RegOp {
    ROP_NOP,
    ROP_MOV,
    ROP_LOAD_K,
    ROP_LOAD_NIL,
    ROP_LOAD_TRUE,
    ROP_LOAD_FALSE,
    ROP_LOAD_INT,

    ROP_ADD,
    ROP_SUB,
    ROP_MUL,
    ROP_DIV,
    ROP_MOD,
    ROP_NEG,

    ROP_EQ,
    ROP_NE,
    ROP_LT,
    ROP_LE,
    ROP_GT,
    ROP_GE,

    ROP_NOT,
    ROP_AND,
    ROP_OR,

    ROP_JMP,
    ROP_JMP_IF,
    ROP_JMP_UNLESS,
    ROP_LOOP,

    ROP_CALL,
    ROP_TAIL_CALL,
    ROP_RET,

    ROP_GET_GLOBAL,
    ROP_SET_GLOBAL,

    ROP_ARRAY_NEW,
    ROP_ARRAY_PUSH,
    ROP_ARRAY_GET,
    ROP_ARRAY_SET,
    ROP_MAP_NEW,
    ROP_MAP_GET,
    ROP_MAP_SET,
    ROP_MAP_GET_IC,

    ROP_CONCAT,

    ROP_CLOSURE,
    ROP_GET_UPVALUE,
    ROP_SET_UPVALUE,
    ROP_CLOSE_UPVALUE,

    ROP_SPAWN,
    ROP_SEND,
    ROP_RECEIVE,
    ROP_SELF,
    ROP_YIELD,

    ROP_LEN,
    ROP_TYPE,
    ROP_PRINT,

    ROP_HALT,

    ROP_COUNT
} RegOp;

/* Register Call Frame */

#define REG_MAX_REGISTERS 256
#define REG_MAX_FRAMES 64

typedef struct RegCallFrame {
    RegInstr *ip;
    NanValue regs[REG_MAX_REGISTERS];
    struct RegChunk *chunk;
    uint8_t base;
    uint8_t result_reg;
} RegCallFrame;

/* Register Chunk */

typedef struct RegChunk {
    RegInstr *code;
    size_t code_size;
    size_t code_capacity;

    Value **constants;
    size_t constants_size;
    size_t constants_capacity;

    struct InlineCache *ic_slots;
    size_t ic_count;
    size_t ic_capacity;

    int *lines;
    size_t lines_capacity;

    uint8_t num_regs;
    uint8_t num_params;
    uint8_t num_upvalues;
} RegChunk;

/* Register VM */

typedef struct RegVM {
    RegCallFrame frames[REG_MAX_FRAMES];
    int frame_count;

    Value *globals;
    struct Upvalue *open_upvalues;

    const char *error;
    int error_line;

    size_t reductions;
    size_t reduction_limit;
    struct Block *block;
    struct Scheduler *scheduler;
} RegVM;

/* VM Result */

typedef enum RegVMResult {
    REGVM_OK,
    REGVM_HALT,
    REGVM_YIELD,
    REGVM_ERROR_COMPILE,
    REGVM_ERROR_RUNTIME,
    REGVM_ERROR_TYPE,
    REGVM_ERROR_OVERFLOW,
} RegVMResult;

/* Register Chunk API */

RegChunk *regchunk_new(void);
void regchunk_free(RegChunk *chunk);

void regchunk_write(RegChunk *chunk, RegInstr instr, int line);
size_t regchunk_add_constant(RegChunk *chunk, Value *value);

static inline RegInstr reg_instr(RegOp op, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return (RegInstr){ .op = op, .rd = rd, .rs1 = rs1, .rs2 = rs2 };
}

static inline RegInstr reg_instr_imm(RegOp op, uint8_t rd, uint16_t imm) {
    RegInstr i;
    i.op = op;
    i.rd = rd;
    i.rs1 = (imm >> 8) & 0xFF;
    i.rs2 = imm & 0xFF;
    return i;
}

static inline RegInstr reg_instr_jump(RegOp op, int32_t offset) {
    RegInstr i;
    i.op = op;
    i.rd = (offset >> 16) & 0xFF;
    i.rs1 = (offset >> 8) & 0xFF;
    i.rs2 = offset & 0xFF;
    return i;
}

static inline RegInstr reg_instr_cond_jump(RegOp op, uint8_t cond_reg, int16_t offset) {
    RegInstr i;
    i.op = op;
    i.rd = cond_reg;
    i.rs1 = (offset >> 8) & 0xFF;
    i.rs2 = offset & 0xFF;
    return i;
}

static inline int16_t reg_get_cond_offset(RegInstr i) {
    return (int16_t)(((uint16_t)i.rs1 << 8) | i.rs2);
}

/* Register VM API */

RegVM *regvm_new(void);
void regvm_free(RegVM *vm);
void regvm_reset(RegVM *vm);

RegVMResult regvm_run(RegVM *vm, RegChunk *chunk);
RegVMResult regvm_call(RegVM *vm, Value *closure, int arg_count);

void regvm_push(RegVM *vm, Value *value);
Value *regvm_pop(RegVM *vm);

const char *regvm_error(const RegVM *vm);
int regvm_error_line(const RegVM *vm);

/* Disassembly */

void regchunk_disassemble(RegChunk *chunk, const char *name);
size_t regchunk_disassemble_instruction(RegChunk *chunk, size_t offset);

#endif /* AGIM_VM_REGVM_H */
