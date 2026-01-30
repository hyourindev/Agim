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

/*============================================================================
 * Constants
 *============================================================================*/

/* Initial sizes for dynamic allocation - grow as needed */
#define VM_STACK_INITIAL 64      /* 512 bytes initial (was 8KB fixed) */
#define VM_FRAMES_INITIAL 8      /* ~256 bytes initial (was 8KB fixed) */

/* Legacy constants for compatibility */
#define VM_STACK_MAX 1024
#define VM_FRAMES_MAX 256

/*============================================================================
 * Execution Result
 *============================================================================*/

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
    VM_YIELD,
    VM_WAITING,
    VM_HALT,
} VMResult;

/*============================================================================
 * Call Frame
 *============================================================================*/

typedef struct CallFrame {
    Chunk *chunk;       /* Currently executing chunk */
    uint8_t *ip;        /* Instruction pointer */
    NanValue *slots;    /* First slot for this frame (NaN-boxed values) */
    Function *function; /* The function being called */
} CallFrame;

/*============================================================================
 * Virtual Machine
 *============================================================================*/

/* Forward declaration for closures */
typedef struct Upvalue Upvalue;

typedef struct VM {
    /* Stack (NaN-boxed values - 8 bytes each, dynamically allocated) */
    /* NULL until vm_ensure_initialized() is called (lazy allocation) */
    NanValue *stack;
    NanValue *stack_top;
    size_t stack_capacity;

    /* Call frames (dynamically allocated) */
    /* NULL until vm_ensure_initialized() is called (lazy allocation) */
    CallFrame *frames;
    int frame_count;
    size_t frames_capacity;

    /* Lazy initialization flag */
    bool initialized;

    /* Global variables */
    Value *globals;

    /* Bytecode */
    Bytecode *code;

    /* Closure support */
    Upvalue *open_upvalues;  /* Linked list of open upvalues */

    /* Error handling */
    const char *error;
    int error_line;

    /* Execution stats */
    size_t reductions;      /* Instructions executed */
    size_t reduction_limit; /* Max before yield */

    /* Block reference (set by runtime) */
    void *block;

    /* Scheduler reference (set by runtime) */
    void *scheduler;
} VM;

/*============================================================================
 * VM Lifecycle
 *============================================================================*/

/**
 * Create a new VM instance.
 */
VM *vm_new(void);

/**
 * Free a VM instance and all associated resources.
 */
void vm_free(VM *vm);

/**
 * Reset VM state (for reuse).
 */
void vm_reset(VM *vm);

/*============================================================================
 * Execution
 *============================================================================*/

/**
 * Load bytecode into the VM.
 */
void vm_load(VM *vm, Bytecode *code);

/**
 * Run the VM until completion or yield.
 * Returns VM_OK on success, VM_YIELD if paused, or an error code.
 */
VMResult vm_run(VM *vm);

/**
 * Execute a single instruction.
 * Useful for debugging and testing.
 */
VMResult vm_step(VM *vm);

/**
 * Resume execution after yield.
 */
VMResult vm_resume(VM *vm);

/*============================================================================
 * Stack Operations (NaN-boxed)
 *============================================================================*/

/**
 * Push a NaN-boxed value onto the stack.
 */
VMResult vm_push_nan(VM *vm, NanValue value);

/**
 * Pop a NaN-boxed value from the stack.
 */
NanValue vm_pop_nan(VM *vm);

/**
 * Peek at the top of the stack (NaN-boxed).
 */
NanValue vm_peek_nan(VM *vm, int distance);

/*============================================================================
 * Stack Operations (Value* compatibility)
 *============================================================================*/

/**
 * Push a Value* onto the stack (wraps as object pointer).
 */
VMResult vm_push(VM *vm, Value *value);

/**
 * Pop and convert to Value* (allocates for primitives).
 * Note: For primitives, this allocates a new Value. Prefer vm_pop_nan().
 */
Value *vm_pop(VM *vm);

/**
 * Peek at the top of the stack (as Value*).
 * Note: For primitives, this allocates a new Value. Prefer vm_peek_nan().
 */
Value *vm_peek(VM *vm, int distance);

/*============================================================================
 * Error Handling
 *============================================================================*/

/**
 * Get the last error message.
 */
const char *vm_error(VM *vm);

/**
 * Get the line number where the error occurred.
 */
int vm_error_line(VM *vm);

/**
 * Set an error message.
 */
void vm_set_error(VM *vm, const char *message);

/*============================================================================
 * Debugging
 *============================================================================*/

/**
 * Print the current stack contents.
 */
void vm_print_stack(VM *vm);

/**
 * Print a stack trace.
 */
void vm_print_trace(VM *vm);

#endif /* AGIM_VM_VM_H */
