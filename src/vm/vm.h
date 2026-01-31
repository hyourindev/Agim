/*
 * Agim - Virtual Machine
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_VM_H
#define AGIM_VM_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm/bytecode.h"
#include "vm/value.h"
#include "vm/nanbox.h"

/* Constants */

#define VM_STACK_INITIAL 64
#define VM_FRAMES_INITIAL 8
#define VM_STACK_MAX 1024
#define VM_FRAMES_MAX 256

/* Execution Result */

typedef enum VMResult {
    VM_OK,
    VM_ERROR_RUNTIME,
    VM_ERROR_STACK_OVERFLOW,
    VM_ERROR_STACK_UNDERFLOW,
    VM_ERROR_TYPE,
    VM_ERROR_DIVISION_BY_ZERO,
    VM_ERROR_OUT_OF_BOUNDS,
    VM_ERROR_UNDEFINED_VARIABLE,
    VM_ERROR_ARITY,
    VM_ERROR_CAPABILITY,
    VM_ERROR_SEND_FAILED,
    VM_ERROR_NOT_IMPLEMENTED,
    VM_YIELD,
    VM_WAITING,
    VM_HALT,
} VMResult;

/* Call Frame */

typedef struct CallFrame {
    Chunk *chunk;
    uint8_t *ip;
    NanValue *slots;
    Function *function;
} CallFrame;

/* Virtual Machine */

typedef struct Upvalue Upvalue;

typedef struct VM {
    NanValue *stack;
    NanValue *stack_top;
    size_t stack_capacity;

    CallFrame *frames;
    int frame_count;
    size_t frames_capacity;

    bool initialized;

    Value *globals;
    Bytecode *code;

    Upvalue *open_upvalues;

    const char *error;
    int error_line;

    size_t reductions;
    size_t reduction_limit;

    void *block;
    void *scheduler;
} VM;

/* VM Lifecycle */

VM *vm_new(void);
void vm_free(VM *vm);
void vm_reset(VM *vm);

/* Execution */

void vm_load(VM *vm, Bytecode *code);
VMResult vm_run(VM *vm);
VMResult vm_step(VM *vm);
VMResult vm_resume(VM *vm);

/* Stack Operations (NaN-boxed) */

VMResult vm_push_nan(VM *vm, NanValue value);
NanValue vm_pop_nan(VM *vm);
NanValue vm_peek_nan(VM *vm, int distance);

/* Stack Operations (Value* compatibility) */

VMResult vm_push(VM *vm, Value *value);
Value *vm_pop(VM *vm);
Value *vm_peek(VM *vm, int distance);

/* Error Handling */

const char *vm_error(VM *vm);
int vm_error_line(VM *vm);
void vm_set_error(VM *vm, const char *message);

/* Debugging */

void vm_print_stack(VM *vm);
void vm_print_trace(VM *vm);

#endif /* AGIM_VM_VM_H */
