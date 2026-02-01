/*
 * Agim - Register-Based Virtual Machine Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "vm/regvm.h"
#include "vm/value.h"
#include "vm/ic.h"
#include "vm/nanbox_convert.h"
#include "types/array.h"
#include "types/map.h"
#include "types/string.h"
#include "types/closure.h"
#include "util/alloc.h"
#include "debug/log.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * Register Chunk Implementation
 */

RegChunk *regchunk_new(void) {
    RegChunk *chunk = agim_alloc(sizeof(RegChunk));
    if (!chunk) {
        LOG_ERROR("regvm: failed to allocate RegChunk");
        return NULL;
    }

    chunk->code_capacity = 256;
    chunk->code_size = 0;
    chunk->code = agim_alloc(sizeof(RegInstr) * chunk->code_capacity);

    chunk->constants_capacity = 64;
    chunk->constants_size = 0;
    chunk->constants = agim_alloc(sizeof(Value *) * chunk->constants_capacity);

    chunk->ic_capacity = 16;
    chunk->ic_count = 0;
    chunk->ic_slots = agim_alloc(sizeof(InlineCache) * chunk->ic_capacity);

    chunk->lines_capacity = 256;
    chunk->lines = agim_alloc(sizeof(int) * chunk->lines_capacity);

    chunk->num_regs = 0;
    chunk->num_params = 0;
    chunk->num_upvalues = 0;

    return chunk;
}

void regchunk_free(RegChunk *chunk) {
    if (!chunk) return;

    agim_free(chunk->code);

    for (size_t i = 0; i < chunk->constants_size; i++) {
        value_free(chunk->constants[i]);
    }
    agim_free(chunk->constants);

    agim_free(chunk->ic_slots);
    agim_free(chunk->lines);
    agim_free(chunk);
}

void regchunk_write(RegChunk *chunk, RegInstr instr, int line) {
    if (chunk->code_size >= chunk->code_capacity) {
        chunk->code_capacity *= 2;
        chunk->code = agim_realloc(chunk->code, sizeof(RegInstr) * chunk->code_capacity);
    }

    if (chunk->code_size >= chunk->lines_capacity) {
        chunk->lines_capacity *= 2;
        chunk->lines = agim_realloc(chunk->lines, sizeof(int) * chunk->lines_capacity);
    }

    chunk->code[chunk->code_size] = instr;
    chunk->lines[chunk->code_size] = line;
    chunk->code_size++;
}

size_t regchunk_add_constant(RegChunk *chunk, Value *value) {
    if (chunk->constants_size >= chunk->constants_capacity) {
        chunk->constants_capacity *= 2;
        chunk->constants = agim_realloc(chunk->constants,
                                        sizeof(Value *) * chunk->constants_capacity);
    }

    chunk->constants[chunk->constants_size] = value;
    return chunk->constants_size++;
}

/* Register VM Lifecycle */

RegVM *regvm_new(void) {
    RegVM *vm = agim_alloc(sizeof(RegVM));

    vm->frame_count = 0;
    vm->globals = value_map();
    vm->open_upvalues = NULL;
    vm->error = NULL;
    vm->error_line = 0;
    vm->reductions = 0;
    vm->reduction_limit = 10000;
    vm->block = NULL;
    vm->scheduler = NULL;

    return vm;
}

void regvm_free(RegVM *vm) {
    if (!vm) return;
    value_free(vm->globals);
    agim_free(vm);
}

void regvm_reset(RegVM *vm) {
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
    vm->error = NULL;
    vm->error_line = 0;
    vm->reductions = 0;
}

/* Error Handling */

static void regvm_set_error(RegVM *vm, const char *msg) {
    vm->error = msg;
}

const char *regvm_error(const RegVM *vm) {
    return vm->error;
}

int regvm_error_line(const RegVM *vm) {
    return vm->error_line;
}

/* Arithmetic Helpers */

static NanValue nanbox_add(NanValue a, NanValue b) {
    if (nanbox_is_int(a) && nanbox_is_int(b)) {
        return nanbox_int(nanbox_as_int(a) + nanbox_as_int(b));
    }
    if (nanbox_is_double(a) && nanbox_is_double(b)) {
        return nanbox_double(nanbox_as_double(a) + nanbox_as_double(b));
    }
    if (nanbox_is_int(a) && nanbox_is_double(b)) {
        return nanbox_double((double)nanbox_as_int(a) + nanbox_as_double(b));
    }
    if (nanbox_is_double(a) && nanbox_is_int(b)) {
        return nanbox_double(nanbox_as_double(a) + (double)nanbox_as_int(b));
    }
    return NANBOX_NIL;
}

static NanValue nanbox_sub(NanValue a, NanValue b) {
    if (nanbox_is_int(a) && nanbox_is_int(b)) {
        return nanbox_int(nanbox_as_int(a) - nanbox_as_int(b));
    }
    if (nanbox_is_double(a) && nanbox_is_double(b)) {
        return nanbox_double(nanbox_as_double(a) - nanbox_as_double(b));
    }
    if (nanbox_is_int(a) && nanbox_is_double(b)) {
        return nanbox_double((double)nanbox_as_int(a) - nanbox_as_double(b));
    }
    if (nanbox_is_double(a) && nanbox_is_int(b)) {
        return nanbox_double(nanbox_as_double(a) - (double)nanbox_as_int(b));
    }
    return NANBOX_NIL;
}

static NanValue nanbox_mul(NanValue a, NanValue b) {
    if (nanbox_is_int(a) && nanbox_is_int(b)) {
        return nanbox_int(nanbox_as_int(a) * nanbox_as_int(b));
    }
    if (nanbox_is_double(a) && nanbox_is_double(b)) {
        return nanbox_double(nanbox_as_double(a) * nanbox_as_double(b));
    }
    if (nanbox_is_int(a) && nanbox_is_double(b)) {
        return nanbox_double((double)nanbox_as_int(a) * nanbox_as_double(b));
    }
    if (nanbox_is_double(a) && nanbox_is_int(b)) {
        return nanbox_double(nanbox_as_double(a) * (double)nanbox_as_int(b));
    }
    return NANBOX_NIL;
}

static NanValue nanbox_div(NanValue a, NanValue b) {
    if (nanbox_is_int(a) && nanbox_is_int(b)) {
        int64_t bv = nanbox_as_int(b);
        if (bv == 0) return NANBOX_NIL;
        return nanbox_int(nanbox_as_int(a) / bv);
    }
    if (nanbox_is_double(a) && nanbox_is_double(b)) {
        return nanbox_double(nanbox_as_double(a) / nanbox_as_double(b));
    }
    if (nanbox_is_int(a) && nanbox_is_double(b)) {
        return nanbox_double((double)nanbox_as_int(a) / nanbox_as_double(b));
    }
    if (nanbox_is_double(a) && nanbox_is_int(b)) {
        return nanbox_double(nanbox_as_double(a) / (double)nanbox_as_int(b));
    }
    return NANBOX_NIL;
}

static NanValue nanbox_mod(NanValue a, NanValue b) {
    if (nanbox_is_int(a) && nanbox_is_int(b)) {
        int64_t bv = nanbox_as_int(b);
        if (bv == 0) return NANBOX_NIL;
        return nanbox_int(nanbox_as_int(a) % bv);
    }
    return NANBOX_NIL;
}

static NanValue nanbox_neg(NanValue a) {
    if (nanbox_is_int(a)) {
        return nanbox_int(-nanbox_as_int(a));
    }
    if (nanbox_is_double(a)) {
        return nanbox_double(-nanbox_as_double(a));
    }
    return NANBOX_NIL;
}

/* Comparison Helpers */

static NanValue nanbox_eq(NanValue a, NanValue b) {
    if (nanbox_is_int(a) && nanbox_is_int(b)) {
        return nanbox_bool(nanbox_as_int(a) == nanbox_as_int(b));
    }
    if (nanbox_is_double(a) && nanbox_is_double(b)) {
        return nanbox_bool(nanbox_as_double(a) == nanbox_as_double(b));
    }
    if (nanbox_is_bool(a) && nanbox_is_bool(b)) {
        return nanbox_bool(nanbox_as_bool(a) == nanbox_as_bool(b));
    }
    if (nanbox_is_nil(a) && nanbox_is_nil(b)) {
        return nanbox_bool(true);
    }
    /* Object comparison by reference */
    if (nanbox_is_obj(a) && nanbox_is_obj(b)) {
        return nanbox_bool(nanbox_as_obj(a) == nanbox_as_obj(b));
    }
    return nanbox_bool(false);
}

static NanValue nanbox_lt(NanValue a, NanValue b) {
    if (nanbox_is_int(a) && nanbox_is_int(b)) {
        return nanbox_bool(nanbox_as_int(a) < nanbox_as_int(b));
    }
    if (nanbox_is_double(a) && nanbox_is_double(b)) {
        return nanbox_bool(nanbox_as_double(a) < nanbox_as_double(b));
    }
    if (nanbox_is_int(a) && nanbox_is_double(b)) {
        return nanbox_bool((double)nanbox_as_int(a) < nanbox_as_double(b));
    }
    if (nanbox_is_double(a) && nanbox_is_int(b)) {
        return nanbox_bool(nanbox_as_double(a) < (double)nanbox_as_int(b));
    }
    if (nanbox_is_obj(a) && nanbox_is_obj(b)) {
        Value *va = (Value *)nanbox_as_obj(a);
        Value *vb = (Value *)nanbox_as_obj(b);
        if (value_is_string(va) && value_is_string(vb)) {
            return nanbox_bool(string_compare(va, vb) < 0);
        }
    }
    return nanbox_bool(false);
}

static NanValue nanbox_le(NanValue a, NanValue b) {
    if (nanbox_is_int(a) && nanbox_is_int(b)) {
        return nanbox_bool(nanbox_as_int(a) <= nanbox_as_int(b));
    }
    if (nanbox_is_double(a) && nanbox_is_double(b)) {
        return nanbox_bool(nanbox_as_double(a) <= nanbox_as_double(b));
    }
    if (nanbox_is_int(a) && nanbox_is_double(b)) {
        return nanbox_bool((double)nanbox_as_int(a) <= nanbox_as_double(b));
    }
    if (nanbox_is_double(a) && nanbox_is_int(b)) {
        return nanbox_bool(nanbox_as_double(a) <= (double)nanbox_as_int(b));
    }
    if (nanbox_is_obj(a) && nanbox_is_obj(b)) {
        Value *va = (Value *)nanbox_as_obj(a);
        Value *vb = (Value *)nanbox_as_obj(b);
        if (value_is_string(va) && value_is_string(vb)) {
            return nanbox_bool(string_compare(va, vb) <= 0);
        }
    }
    return nanbox_bool(false);
}

static bool reg_nanbox_is_truthy(NanValue v) {
    if (nanbox_is_nil(v)) return false;
    if (nanbox_is_bool(v)) return nanbox_as_bool(v);
    if (nanbox_is_int(v)) return nanbox_as_int(v) != 0;
    if (nanbox_is_double(v)) return nanbox_as_double(v) != 0.0;
    return true; /* Objects are truthy */
}

/* Register VM Execution */

/* Check for computed goto support */
#if defined(__GNUC__) || defined(__clang__)
#define USE_REG_COMPUTED_GOTO 1
#else
#define USE_REG_COMPUTED_GOTO 0
#endif

RegVMResult regvm_run(RegVM *vm, RegChunk *chunk) {
    if (!vm || !chunk) return REGVM_ERROR_RUNTIME;

    /* Set up initial frame */
    RegCallFrame *frame = &vm->frames[vm->frame_count++];
    frame->ip = chunk->code;
    frame->chunk = chunk;
    frame->base = 0;
    frame->result_reg = 0;

    /* Lazy register initialization: only initialize registers actually used.
     * This reduces per-call overhead from 2KB to typically <256 bytes. */
    uint8_t max_reg = chunk->num_regs > 0 ? chunk->num_regs : 16;
    for (int i = 0; i <= max_reg && i < REG_MAX_REGISTERS; i++) {
        frame->regs[i] = NANBOX_NIL;
    }

    /* Convenience macro for register access.
     * Note: Bytecode should be validated at load time to ensure register
     * indices are within bounds. Runtime bounds checking would be too slow. */
    #define R(n) frame->regs[n]
    /* In debug builds, verify bounds before accessing */
    #ifndef NDEBUG
    #define CHECK_REG(n) assert((n) < REG_MAX_REGISTERS)
    #else
    #define CHECK_REG(n) ((void)0)
    #endif

#if USE_REG_COMPUTED_GOTO
    /* Computed goto dispatch */

    static void *dispatch_table[ROP_COUNT] = {
        [ROP_NOP] = &&op_nop,
        [ROP_MOV] = &&op_mov,
        [ROP_LOAD_K] = &&op_load_k,
        [ROP_LOAD_NIL] = &&op_load_nil,
        [ROP_LOAD_TRUE] = &&op_load_true,
        [ROP_LOAD_FALSE] = &&op_load_false,
        [ROP_LOAD_INT] = &&op_load_int,
        [ROP_ADD] = &&op_add,
        [ROP_SUB] = &&op_sub,
        [ROP_MUL] = &&op_mul,
        [ROP_DIV] = &&op_div,
        [ROP_MOD] = &&op_mod,
        [ROP_NEG] = &&op_neg,
        [ROP_EQ] = &&op_eq,
        [ROP_NE] = &&op_ne,
        [ROP_LT] = &&op_lt,
        [ROP_LE] = &&op_le,
        [ROP_GT] = &&op_gt,
        [ROP_GE] = &&op_ge,
        [ROP_NOT] = &&op_not,
        [ROP_AND] = &&op_and,
        [ROP_OR] = &&op_or,
        [ROP_JMP] = &&op_jmp,
        [ROP_JMP_IF] = &&op_jmp_if,
        [ROP_JMP_UNLESS] = &&op_jmp_unless,
        [ROP_LOOP] = &&op_loop,
        [ROP_CALL] = &&op_call,
        [ROP_RET] = &&op_ret,
        [ROP_ARRAY_NEW] = &&op_array_new,
        [ROP_ARRAY_PUSH] = &&op_array_push,
        [ROP_ARRAY_GET] = &&op_array_get,
        [ROP_MAP_NEW] = &&op_map_new,
        [ROP_MAP_GET] = &&op_map_get,
        [ROP_MAP_SET] = &&op_map_set,
        [ROP_CONCAT] = &&op_concat,
        [ROP_LEN] = &&op_len,
        [ROP_PRINT] = &&op_print,
        [ROP_HALT] = &&op_halt,
    };

    /* Pre-fetch instruction to avoid reading before buffer on first dispatch */
    RegInstr cur_instr;
    #define DISPATCH() do { cur_instr = *frame->ip++; goto *dispatch_table[cur_instr.op]; } while(0)

    DISPATCH();

    op_nop:
        DISPATCH();

    op_mov: {
        RegInstr i = cur_instr;
        R(i.rd) = R(i.rs1);
        DISPATCH();
    }

    op_load_k: {
        RegInstr i = cur_instr;
        uint16_t idx = reg_get_imm(i);
        if (idx < chunk->constants_size) {
            R(i.rd) = value_to_nanbox(chunk->constants[idx]);
        }
        DISPATCH();
    }

    op_load_nil: {
        RegInstr i = cur_instr;
        R(i.rd) = NANBOX_NIL;
        DISPATCH();
    }

    op_load_true: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_bool(true);
        DISPATCH();
    }

    op_load_false: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_bool(false);
        DISPATCH();
    }

    op_load_int: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_int((int16_t)reg_get_imm(i));
        DISPATCH();
    }

    op_add: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_add(R(i.rs1), R(i.rs2));
        DISPATCH();
    }

    op_sub: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_sub(R(i.rs1), R(i.rs2));
        DISPATCH();
    }

    op_mul: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_mul(R(i.rs1), R(i.rs2));
        DISPATCH();
    }

    op_div: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_div(R(i.rs1), R(i.rs2));
        DISPATCH();
    }

    op_mod: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_mod(R(i.rs1), R(i.rs2));
        DISPATCH();
    }

    op_neg: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_neg(R(i.rs1));
        DISPATCH();
    }

    op_eq: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_eq(R(i.rs1), R(i.rs2));
        DISPATCH();
    }

    op_ne: {
        RegInstr i = cur_instr;
        NanValue eq = nanbox_eq(R(i.rs1), R(i.rs2));
        R(i.rd) = nanbox_bool(!nanbox_as_bool(eq));
        DISPATCH();
    }

    op_lt: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_lt(R(i.rs1), R(i.rs2));
        DISPATCH();
    }

    op_le: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_le(R(i.rs1), R(i.rs2));
        DISPATCH();
    }

    op_gt: {
        RegInstr i = cur_instr;
        /* a > b is equivalent to b < a */
        R(i.rd) = nanbox_lt(R(i.rs2), R(i.rs1));
        DISPATCH();
    }

    op_ge: {
        RegInstr i = cur_instr;
        /* a >= b is equivalent to b <= a */
        R(i.rd) = nanbox_le(R(i.rs2), R(i.rs1));
        DISPATCH();
    }

    op_not: {
        RegInstr i = cur_instr;
        R(i.rd) = nanbox_bool(!reg_nanbox_is_truthy(R(i.rs1)));
        DISPATCH();
    }

    op_and: {
        RegInstr i = cur_instr;
        if (!reg_nanbox_is_truthy(R(i.rs1))) {
            R(i.rd) = R(i.rs1);
        } else {
            R(i.rd) = R(i.rs2);
        }
        DISPATCH();
    }

    op_or: {
        RegInstr i = cur_instr;
        if (reg_nanbox_is_truthy(R(i.rs1))) {
            R(i.rd) = R(i.rs1);
        } else {
            R(i.rd) = R(i.rs2);
        }
        DISPATCH();
    }

    op_jmp: {
        RegInstr i = cur_instr;
        frame->ip += reg_get_offset(i);
        DISPATCH();
    }

    op_jmp_if: {
        RegInstr i = cur_instr;
        if (reg_nanbox_is_truthy(R(i.rd))) {
            frame->ip += reg_get_cond_offset(i);
        }
        DISPATCH();
    }

    op_jmp_unless: {
        RegInstr i = cur_instr;
        if (!reg_nanbox_is_truthy(R(i.rd))) {
            frame->ip += reg_get_cond_offset(i);
        }
        DISPATCH();
    }

    op_loop: {
        RegInstr i = cur_instr;
        frame->ip -= (-reg_get_offset(i)); /* Negative offset for backward jump */
        DISPATCH();
    }

    op_call: {
        /* ROP_CALL: rd = rs1(args starting at rs2)
         * rs1 = register containing the function value (with RegChunk pointer)
         * rs2 = first argument register
         * rd = destination register for result
         *
         * The register VM uses a different function representation than the
         * stack-based VM. Function values are RegChunk pointers wrapped as objects.
         */
        RegInstr i = cur_instr;
        NanValue func_val = R(i.rs1);
        uint8_t first_arg_reg = i.rs2;
        uint8_t result_reg = i.rd;

        /* Get the function value - expect an object pointer */
        if (!nanbox_is_obj(func_val)) {
            regvm_set_error(vm, "cannot call non-function value");
            return REGVM_ERROR_TYPE;
        }

        void *obj = nanbox_as_obj(func_val);
        if (!obj) {
            regvm_set_error(vm, "cannot call nil value");
            return REGVM_ERROR_TYPE;
        }

        /* In the register VM, object pointers are RegChunk* for function calls. */
        RegChunk *target_chunk = (RegChunk *)obj;

        /* Validate the chunk (simple check - it should have code) */
        if (!target_chunk->code || target_chunk->code_size == 0) {
            regvm_set_error(vm, "invalid function: no code");
            return REGVM_ERROR_RUNTIME;
        }

        /* Check frame limit */
        if (vm->frame_count >= REG_MAX_FRAMES) {
            regvm_set_error(vm, "stack overflow");
            return REGVM_ERROR_OVERFLOW;
        }

        /* Get arity from the target chunk */
        uint8_t arity = target_chunk->num_params;

        /* Push new frame */
        RegCallFrame *new_frame = &vm->frames[vm->frame_count++];
        new_frame->ip = target_chunk->code;
        new_frame->chunk = target_chunk;
        new_frame->base = 0;
        new_frame->result_reg = result_reg;

        /* Initialize only registers that will be used (lazy initialization).
         * This reduces per-call overhead from 2KB to typically <256 bytes. */
        uint8_t max_reg = target_chunk->num_regs > 0 ? target_chunk->num_regs : 16;
        for (int j = 0; j <= max_reg && j < REG_MAX_REGISTERS; j++) {
            new_frame->regs[j] = NANBOX_NIL;
        }

        /* Copy arguments to new frame registers (registers 0 to arity-1) */
        for (uint8_t j = 0; j < arity; j++) {
            new_frame->regs[j] = R(first_arg_reg + j);
        }

        /* Store caller's frame result register info */
        frame->result_reg = result_reg;

        /* Switch to new frame */
        frame = new_frame;
        DISPATCH();
    }

    op_ret: {
        RegInstr i = cur_instr;
        NanValue result = R(i.rd);

        vm->frame_count--;
        if (vm->frame_count == 0) {
            return REGVM_OK;
        }

        frame = &vm->frames[vm->frame_count - 1];
        R(frame->result_reg) = result;
        DISPATCH();
    }

    op_array_new: {
        RegInstr i = cur_instr;
        R(i.rd) = value_to_nanbox(value_array());
        DISPATCH();
    }

    op_array_push: {
        RegInstr i = cur_instr;
        Value *arr = nanbox_to_value(R(i.rd));
        Value *val = nanbox_to_value(R(i.rs1));
        if (arr && value_is_array(arr)) {
            arr = array_push(arr, val);
            R(i.rd) = value_to_nanbox(arr);  /* Update register with possibly-new array */
        }
        DISPATCH();
    }

    op_array_get: {
        RegInstr i = cur_instr;
        Value *arr = nanbox_to_value(R(i.rs1));
        if (arr && value_is_array(arr) && nanbox_is_int(R(i.rs2))) {
            int64_t idx = nanbox_as_int(R(i.rs2));
            if (idx >= 0) {
                Value *item = array_get(arr, (size_t)idx);
                R(i.rd) = item ? value_to_nanbox(item) : NANBOX_NIL;
            } else {
                R(i.rd) = NANBOX_NIL;
            }
        } else {
            R(i.rd) = NANBOX_NIL;
        }
        DISPATCH();
    }

    op_map_new: {
        RegInstr i = cur_instr;
        R(i.rd) = value_to_nanbox(value_map());
        DISPATCH();
    }

    op_map_get: {
        RegInstr i = cur_instr;
        Value *map = nanbox_to_value(R(i.rs1));
        Value *key = nanbox_to_value(R(i.rs2));
        if (map && value_is_map(map) && key && value_is_string(key)) {
            Value *val = map_get(map, key->as.string->data);
            R(i.rd) = val ? value_to_nanbox(val) : NANBOX_NIL;
        } else {
            R(i.rd) = NANBOX_NIL;
        }
        DISPATCH();
    }

    op_map_set: {
        RegInstr i = cur_instr;
        Value *map = nanbox_to_value(R(i.rs1));
        Value *key = nanbox_to_value(R(i.rs2));
        Value *val = nanbox_to_value(R(i.rd));
        if (map && value_is_map(map) && key && value_is_string(key)) {
            map = map_set(map, key->as.string->data, val);
            R(i.rs1) = value_to_nanbox(map);  /* Update register with possibly-new map */
        }
        DISPATCH();
    }

    op_concat: {
        RegInstr i = cur_instr;
        Value *a = nanbox_to_value(R(i.rs1));
        Value *b = nanbox_to_value(R(i.rs2));
        /* Handle nil as empty string */
        bool a_str = !a || value_is_nil(a) || value_is_string(a);
        bool b_str = !b || value_is_nil(b) || value_is_string(b);
        if (a_str && b_str) {
            Value *str_a = (!a || value_is_nil(a)) ? value_string("") : a;
            Value *str_b = (!b || value_is_nil(b)) ? value_string("") : b;
            R(i.rd) = value_to_nanbox(string_concat(str_a, str_b));
        } else {
            R(i.rd) = NANBOX_NIL;
        }
        DISPATCH();
    }

    op_len: {
        RegInstr i = cur_instr;
        Value *v = nanbox_to_value(R(i.rs1));
        int64_t len = 0;
        if (v) {
            if (value_is_array(v)) {
                len = (int64_t)array_length(v);
            } else if (value_is_string(v)) {
                len = (int64_t)string_length(v);
            } else if (value_is_map(v)) {
                len = (int64_t)map_size(v);
            }
        }
        R(i.rd) = nanbox_int(len);
        DISPATCH();
    }

    op_print: {
        RegInstr i = cur_instr;
        Value *v = nanbox_to_value(R(i.rd));
        value_print(v);
        printf("\n");
        DISPATCH();
    }

    op_halt:
        return REGVM_HALT;

    #undef DISPATCH

#else
    /* Switch-based dispatch (portable fallback) */

    for (;;) {
        RegInstr i = *frame->ip++;

        switch (i.op) {
        case ROP_NOP:
            break;

        case ROP_MOV:
            R(i.rd) = R(i.rs1);
            break;

        case ROP_LOAD_K: {
            uint16_t idx = reg_get_imm(i);
            if (idx < chunk->constants_size) {
                R(i.rd) = value_to_nanbox(chunk->constants[idx]);
            }
            break;
        }

        case ROP_LOAD_NIL:
            R(i.rd) = NANBOX_NIL;
            break;

        case ROP_LOAD_TRUE:
            R(i.rd) = nanbox_bool(true);
            break;

        case ROP_LOAD_FALSE:
            R(i.rd) = nanbox_bool(false);
            break;

        case ROP_LOAD_INT:
            R(i.rd) = nanbox_int((int16_t)reg_get_imm(i));
            break;

        case ROP_ADD:
            R(i.rd) = nanbox_add(R(i.rs1), R(i.rs2));
            break;

        case ROP_SUB:
            R(i.rd) = nanbox_sub(R(i.rs1), R(i.rs2));
            break;

        case ROP_MUL:
            R(i.rd) = nanbox_mul(R(i.rs1), R(i.rs2));
            break;

        case ROP_DIV:
            R(i.rd) = nanbox_div(R(i.rs1), R(i.rs2));
            break;

        case ROP_MOD:
            R(i.rd) = nanbox_mod(R(i.rs1), R(i.rs2));
            break;

        case ROP_NEG:
            R(i.rd) = nanbox_neg(R(i.rs1));
            break;

        case ROP_EQ:
            R(i.rd) = nanbox_eq(R(i.rs1), R(i.rs2));
            break;

        case ROP_NE: {
            NanValue eq = nanbox_eq(R(i.rs1), R(i.rs2));
            R(i.rd) = nanbox_bool(!nanbox_as_bool(eq));
            break;
        }

        case ROP_LT:
            R(i.rd) = nanbox_lt(R(i.rs1), R(i.rs2));
            break;

        case ROP_LE:
            R(i.rd) = nanbox_le(R(i.rs1), R(i.rs2));
            break;

        case ROP_GT:
            R(i.rd) = nanbox_lt(R(i.rs2), R(i.rs1));
            break;

        case ROP_GE:
            R(i.rd) = nanbox_le(R(i.rs2), R(i.rs1));
            break;

        case ROP_NOT:
            R(i.rd) = nanbox_bool(!reg_nanbox_is_truthy(R(i.rs1)));
            break;

        case ROP_AND:
            R(i.rd) = reg_nanbox_is_truthy(R(i.rs1)) ? R(i.rs2) : R(i.rs1);
            break;

        case ROP_OR:
            R(i.rd) = reg_nanbox_is_truthy(R(i.rs1)) ? R(i.rs1) : R(i.rs2);
            break;

        case ROP_JMP:
            frame->ip += reg_get_offset(i);
            break;

        case ROP_JMP_IF:
            if (reg_nanbox_is_truthy(R(i.rd))) {
                frame->ip += reg_get_cond_offset(i);
            }
            break;

        case ROP_JMP_UNLESS:
            if (!reg_nanbox_is_truthy(R(i.rd))) {
                frame->ip += reg_get_cond_offset(i);
            }
            break;

        case ROP_LOOP:
            frame->ip -= (-reg_get_offset(i));
            break;

        case ROP_RET: {
            NanValue result = R(i.rd);
            vm->frame_count--;
            if (vm->frame_count == 0) {
                return REGVM_OK;
            }
            frame = &vm->frames[vm->frame_count - 1];
            R(frame->result_reg) = result;
            break;
        }

        case ROP_ARRAY_NEW:
            R(i.rd) = value_to_nanbox(value_array());
            break;

        case ROP_ARRAY_PUSH: {
            Value *arr = nanbox_to_value(R(i.rd));
            Value *val = nanbox_to_value(R(i.rs1));
            if (arr && value_is_array(arr)) {
                arr = array_push(arr, val);
                R(i.rd) = value_to_nanbox(arr);
            }
            break;
        }

        case ROP_ARRAY_GET: {
            Value *arr = nanbox_to_value(R(i.rs1));
            if (arr && value_is_array(arr) && nanbox_is_int(R(i.rs2))) {
                int64_t idx = nanbox_as_int(R(i.rs2));
                if (idx >= 0) {
                    Value *item = array_get(arr, (size_t)idx);
                    R(i.rd) = item ? value_to_nanbox(item) : NANBOX_NIL;
                } else {
                    R(i.rd) = NANBOX_NIL;
                }
            } else {
                R(i.rd) = NANBOX_NIL;
            }
            break;
        }

        case ROP_MAP_NEW:
            R(i.rd) = value_to_nanbox(value_map());
            break;

        case ROP_MAP_GET: {
            Value *map = nanbox_to_value(R(i.rs1));
            Value *key = nanbox_to_value(R(i.rs2));
            if (map && value_is_map(map) && key && value_is_string(key)) {
                Value *val = map_get(map, key->as.string->data);
                R(i.rd) = val ? value_to_nanbox(val) : NANBOX_NIL;
            } else {
                R(i.rd) = NANBOX_NIL;
            }
            break;
        }

        case ROP_MAP_SET: {
            Value *map = nanbox_to_value(R(i.rs1));
            Value *key = nanbox_to_value(R(i.rs2));
            Value *val = nanbox_to_value(R(i.rd));
            if (map && value_is_map(map) && key && value_is_string(key)) {
                map = map_set(map, key->as.string->data, val);
                R(i.rs1) = value_to_nanbox(map);
            }
            break;
        }

        case ROP_CONCAT: {
            Value *a = nanbox_to_value(R(i.rs1));
            Value *b = nanbox_to_value(R(i.rs2));
            /* Handle nil as empty string */
            bool a_str = !a || value_is_nil(a) || value_is_string(a);
            bool b_str = !b || value_is_nil(b) || value_is_string(b);
            if (a_str && b_str) {
                Value *str_a = (!a || value_is_nil(a)) ? value_string("") : a;
                Value *str_b = (!b || value_is_nil(b)) ? value_string("") : b;
                R(i.rd) = value_to_nanbox(string_concat(str_a, str_b));
            } else {
                R(i.rd) = NANBOX_NIL;
            }
            break;
        }

        case ROP_LEN: {
            Value *v = nanbox_to_value(R(i.rs1));
            int64_t len = 0;
            if (v) {
                if (value_is_array(v)) {
                    len = (int64_t)array_length(v);
                } else if (value_is_string(v)) {
                    len = (int64_t)string_length(v);
                } else if (value_is_map(v)) {
                    len = (int64_t)map_size(v);
                }
            }
            R(i.rd) = nanbox_int(len);
            break;
        }

        case ROP_PRINT: {
            Value *v = nanbox_to_value(R(i.rd));
            value_print(v);
            printf("\n");
            break;
        }

        case ROP_HALT:
            return REGVM_HALT;

        default:
            regvm_set_error(vm, "unknown opcode");
            return REGVM_ERROR_RUNTIME;
        }
    }
#endif

    #undef R
}

/* Disassembly */

static const char *rop_names[] = {
    [ROP_NOP] = "NOP",
    [ROP_MOV] = "MOV",
    [ROP_LOAD_K] = "LOAD_K",
    [ROP_LOAD_NIL] = "LOAD_NIL",
    [ROP_LOAD_TRUE] = "LOAD_TRUE",
    [ROP_LOAD_FALSE] = "LOAD_FALSE",
    [ROP_LOAD_INT] = "LOAD_INT",
    [ROP_ADD] = "ADD",
    [ROP_SUB] = "SUB",
    [ROP_MUL] = "MUL",
    [ROP_DIV] = "DIV",
    [ROP_MOD] = "MOD",
    [ROP_NEG] = "NEG",
    [ROP_EQ] = "EQ",
    [ROP_NE] = "NE",
    [ROP_LT] = "LT",
    [ROP_LE] = "LE",
    [ROP_GT] = "GT",
    [ROP_GE] = "GE",
    [ROP_NOT] = "NOT",
    [ROP_AND] = "AND",
    [ROP_OR] = "OR",
    [ROP_JMP] = "JMP",
    [ROP_JMP_IF] = "JMP_IF",
    [ROP_JMP_UNLESS] = "JMP_UNLESS",
    [ROP_LOOP] = "LOOP",
    [ROP_CALL] = "CALL",
    [ROP_TAIL_CALL] = "TAIL_CALL",
    [ROP_RET] = "RET",
    [ROP_GET_GLOBAL] = "GET_GLOBAL",
    [ROP_SET_GLOBAL] = "SET_GLOBAL",
    [ROP_ARRAY_NEW] = "ARRAY_NEW",
    [ROP_ARRAY_PUSH] = "ARRAY_PUSH",
    [ROP_ARRAY_GET] = "ARRAY_GET",
    [ROP_ARRAY_SET] = "ARRAY_SET",
    [ROP_MAP_NEW] = "MAP_NEW",
    [ROP_MAP_GET] = "MAP_GET",
    [ROP_MAP_SET] = "MAP_SET",
    [ROP_MAP_GET_IC] = "MAP_GET_IC",
    [ROP_CONCAT] = "CONCAT",
    [ROP_CLOSURE] = "CLOSURE",
    [ROP_GET_UPVALUE] = "GET_UPVALUE",
    [ROP_SET_UPVALUE] = "SET_UPVALUE",
    [ROP_CLOSE_UPVALUE] = "CLOSE_UPVALUE",
    [ROP_SPAWN] = "SPAWN",
    [ROP_SEND] = "SEND",
    [ROP_RECEIVE] = "RECEIVE",
    [ROP_SELF] = "SELF",
    [ROP_YIELD] = "YIELD",
    [ROP_LEN] = "LEN",
    [ROP_TYPE] = "TYPE",
    [ROP_PRINT] = "PRINT",
    [ROP_HALT] = "HALT",
};

void regchunk_disassemble(RegChunk *chunk, const char *name) {
    printf("== %s (register) ==\n", name);

    for (size_t i = 0; i < chunk->code_size; i++) {
        regchunk_disassemble_instruction(chunk, i);
    }
}

size_t regchunk_disassemble_instruction(RegChunk *chunk, size_t offset) {
    printf("%04zu ", offset);

    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    RegInstr i = chunk->code[offset];

    if (i.op < ROP_COUNT && rop_names[i.op]) {
        printf("%-12s", rop_names[i.op]);
    } else {
        printf("UNKNOWN(%d) ", i.op);
    }

    /* Print operands based on instruction type */
    switch (i.op) {
    case ROP_MOV:
    case ROP_NEG:
    case ROP_NOT:
    case ROP_LEN:
        printf(" r%d, r%d", i.rd, i.rs1);
        break;

    case ROP_ADD:
    case ROP_SUB:
    case ROP_MUL:
    case ROP_DIV:
    case ROP_MOD:
    case ROP_EQ:
    case ROP_NE:
    case ROP_LT:
    case ROP_LE:
    case ROP_GT:
    case ROP_GE:
    case ROP_AND:
    case ROP_OR:
    case ROP_CONCAT:
    case ROP_ARRAY_GET:
    case ROP_MAP_GET:
        printf(" r%d, r%d, r%d", i.rd, i.rs1, i.rs2);
        break;

    case ROP_LOAD_K:
    case ROP_LOAD_INT:
        printf(" r%d, %d", i.rd, reg_get_imm(i));
        break;

    case ROP_LOAD_NIL:
    case ROP_LOAD_TRUE:
    case ROP_LOAD_FALSE:
    case ROP_ARRAY_NEW:
    case ROP_MAP_NEW:
    case ROP_PRINT:
        printf(" r%d", i.rd);
        break;

    case ROP_JMP:
    case ROP_LOOP:
        printf(" %+d -> %zu", reg_get_offset(i), offset + 1 + reg_get_offset(i));
        break;

    case ROP_JMP_IF:
    case ROP_JMP_UNLESS:
        printf(" r%d, %+d", i.rd, reg_get_offset(i));
        break;

    case ROP_RET:
        printf(" r%d", i.rd);
        break;

    default:
        break;
    }

    printf("\n");
    return offset + 1;
}
