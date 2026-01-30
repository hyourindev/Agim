/*
 * Agim - Register-Based Virtual Machine
 *
 * A register-based VM design for improved performance over stack-based.
 * Uses 256 virtual registers per call frame with 4-byte instructions.
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

/*============================================================================
 * Register Instruction Format
 *
 * All instructions are 4 bytes (32 bits):
 *   [op:8][rd:8][rs1:8][rs2:8]    - 3-register format
 *   [op:8][rd:8][imm:16]          - immediate format
 *   [op:8][offset:24]             - jump format
 *============================================================================*/

typedef union RegInstr {
    uint32_t raw;
    struct {
        uint8_t op;   /* Opcode */
        uint8_t rd;   /* Destination register */
        uint8_t rs1;  /* Source register 1 */
        uint8_t rs2;  /* Source register 2 */
    };
} RegInstr;

/* Extract immediate value from instruction (uses rd + rs1 + rs2 as 16-bit imm) */
static inline uint16_t reg_get_imm(RegInstr i) {
    return ((uint16_t)i.rs1 << 8) | i.rs2;
}

/* Extract signed jump offset from instruction (uses rd + rs1 + rs2 as 24-bit offset) */
static inline int32_t reg_get_offset(RegInstr i) {
    int32_t offset = ((int32_t)i.rd << 16) | ((int32_t)i.rs1 << 8) | i.rs2;
    /* Sign extend from 24 bits */
    if (offset & 0x800000) {
        offset |= 0xFF000000;
    }
    return offset;
}

/*============================================================================
 * Register Opcodes
 *============================================================================*/

typedef enum RegOp {
    /* Data movement */
    ROP_NOP,         /* No operation */
    ROP_MOV,         /* rd = rs1 */
    ROP_LOAD_K,      /* rd = constants[imm] */
    ROP_LOAD_NIL,    /* rd = nil */
    ROP_LOAD_TRUE,   /* rd = true */
    ROP_LOAD_FALSE,  /* rd = false */
    ROP_LOAD_INT,    /* rd = (int16_t)imm */

    /* Arithmetic */
    ROP_ADD,         /* rd = rs1 + rs2 */
    ROP_SUB,         /* rd = rs1 - rs2 */
    ROP_MUL,         /* rd = rs1 * rs2 */
    ROP_DIV,         /* rd = rs1 / rs2 */
    ROP_MOD,         /* rd = rs1 % rs2 */
    ROP_NEG,         /* rd = -rs1 */

    /* Comparison */
    ROP_EQ,          /* rd = rs1 == rs2 */
    ROP_NE,          /* rd = rs1 != rs2 */
    ROP_LT,          /* rd = rs1 < rs2 */
    ROP_LE,          /* rd = rs1 <= rs2 */
    ROP_GT,          /* rd = rs1 > rs2 */
    ROP_GE,          /* rd = rs1 >= rs2 */

    /* Logic */
    ROP_NOT,         /* rd = !rs1 */
    ROP_AND,         /* rd = rs1 && rs2 */
    ROP_OR,          /* rd = rs1 || rs2 */

    /* Control flow */
    ROP_JMP,         /* ip += offset */
    ROP_JMP_IF,      /* if rd then ip += offset */
    ROP_JMP_UNLESS,  /* if !rd then ip += offset */
    ROP_LOOP,        /* ip -= offset (backward jump) */

    /* Functions */
    ROP_CALL,        /* rd = rs1(args starting at rs2) */
    ROP_TAIL_CALL,   /* tail call optimization */
    ROP_RET,         /* return rd */

    /* Variables */
    ROP_GET_GLOBAL,  /* rd = globals[imm] */
    ROP_SET_GLOBAL,  /* globals[imm] = rs1 */

    /* Data structures */
    ROP_ARRAY_NEW,   /* rd = [] */
    ROP_ARRAY_PUSH,  /* rd.push(rs1) */
    ROP_ARRAY_GET,   /* rd = rs1[rs2] */
    ROP_ARRAY_SET,   /* rs1[rs2] = rd */
    ROP_MAP_NEW,     /* rd = {} */
    ROP_MAP_GET,     /* rd = rs1[rs2] */
    ROP_MAP_SET,     /* rs1[rs2] = rd */
    ROP_MAP_GET_IC,  /* rd = rs1.key with inline cache */

    /* String */
    ROP_CONCAT,      /* rd = rs1 + rs2 (string concat) */

    /* Closures */
    ROP_CLOSURE,     /* rd = closure(func_idx) */
    ROP_GET_UPVALUE, /* rd = upvalues[imm] */
    ROP_SET_UPVALUE, /* upvalues[imm] = rs1 */
    ROP_CLOSE_UPVALUE, /* close upvalue at rs1 */

    /* Process operations */
    ROP_SPAWN,       /* rd = spawn(rs1) */
    ROP_SEND,        /* send(rd, rs1) */
    ROP_RECEIVE,     /* rd = receive() */
    ROP_SELF,        /* rd = self() */
    ROP_YIELD,       /* yield execution */

    /* Utility */
    ROP_LEN,         /* rd = len(rs1) */
    ROP_TYPE,        /* rd = type(rs1) */
    ROP_PRINT,       /* print(rd) */

    /* End */
    ROP_HALT,        /* stop execution */

    ROP_COUNT        /* Number of opcodes */
} RegOp;

/*============================================================================
 * Register Call Frame
 *============================================================================*/

#define REG_MAX_REGISTERS 256
#define REG_MAX_FRAMES 64

typedef struct RegCallFrame {
    RegInstr *ip;              /* Instruction pointer */
    NanValue regs[REG_MAX_REGISTERS]; /* Virtual registers */
    struct RegChunk *chunk;    /* Current chunk */
    uint8_t base;              /* Base register for this frame */
    uint8_t result_reg;        /* Register to store return value */
} RegCallFrame;

/*============================================================================
 * Register Chunk (bytecode container)
 *============================================================================*/

typedef struct RegChunk {
    RegInstr *code;
    size_t code_size;
    size_t code_capacity;

    Value **constants;
    size_t constants_size;
    size_t constants_capacity;

    /* Inline cache slots */
    struct InlineCache *ic_slots;
    size_t ic_count;
    size_t ic_capacity;

    /* Debug info */
    int *lines;
    size_t lines_capacity;

    /* Metadata */
    uint8_t num_regs;    /* Number of registers used */
    uint8_t num_params;  /* Number of parameters */
    uint8_t num_upvalues; /* Number of upvalues */
} RegChunk;

/*============================================================================
 * Register VM
 *============================================================================*/

typedef struct RegVM {
    RegCallFrame frames[REG_MAX_FRAMES];
    int frame_count;

    Value *globals;
    struct Upvalue *open_upvalues;

    /* Error handling */
    const char *error;
    int error_line;

    /* Scheduling */
    size_t reductions;
    size_t reduction_limit;
    struct Block *block;
    struct Scheduler *scheduler;
} RegVM;

/*============================================================================
 * VM Result
 *============================================================================*/

typedef enum RegVMResult {
    REGVM_OK,
    REGVM_HALT,
    REGVM_YIELD,
    REGVM_ERROR_COMPILE,
    REGVM_ERROR_RUNTIME,
    REGVM_ERROR_TYPE,
    REGVM_ERROR_OVERFLOW,
} RegVMResult;

/*============================================================================
 * Register Chunk API
 *============================================================================*/

RegChunk *regchunk_new(void);
void regchunk_free(RegChunk *chunk);

void regchunk_write(RegChunk *chunk, RegInstr instr, int line);
size_t regchunk_add_constant(RegChunk *chunk, Value *value);

/* Instruction builders */
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

/* Conditional jump: rd = condition register, rs1/rs2 = 16-bit signed offset */
static inline RegInstr reg_instr_cond_jump(RegOp op, uint8_t cond_reg, int16_t offset) {
    RegInstr i;
    i.op = op;
    i.rd = cond_reg;
    i.rs1 = (offset >> 8) & 0xFF;
    i.rs2 = offset & 0xFF;
    return i;
}

/* Get 16-bit signed offset from conditional jump (rs1/rs2) */
static inline int16_t reg_get_cond_offset(RegInstr i) {
    return (int16_t)(((uint16_t)i.rs1 << 8) | i.rs2);
}

/*============================================================================
 * Register VM API
 *============================================================================*/

RegVM *regvm_new(void);
void regvm_free(RegVM *vm);
void regvm_reset(RegVM *vm);

RegVMResult regvm_run(RegVM *vm, RegChunk *chunk);
RegVMResult regvm_call(RegVM *vm, Value *closure, int arg_count);

/* Stack operations (for interop with stack VM) */
void regvm_push(RegVM *vm, Value *value);
Value *regvm_pop(RegVM *vm);

/* Error handling */
const char *regvm_error(const RegVM *vm);
int regvm_error_line(const RegVM *vm);

/*============================================================================
 * Disassembly
 *============================================================================*/

void regchunk_disassemble(RegChunk *chunk, const char *name);
size_t regchunk_disassemble_instruction(RegChunk *chunk, size_t offset);

#endif /* AGIM_VM_REGVM_H */
