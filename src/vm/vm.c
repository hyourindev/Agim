/*
 * Agim - Virtual Machine
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "vm/vm.h"
#include "vm/ic.h"
#include "vm/nanbox_convert.h"
#include "util/hash.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "runtime/supervisor.h"
#include "runtime/procgroup.h"
#include "runtime/telemetry.h"
#include "runtime/timer.h"
#include "vm/primitives.h"
#include "types/closure.h"
#include "debug/trace.h"
#include "debug/log.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "vm/sandbox.h"
#include "builtin/tools.h"

/* Memory Helpers */

static void *alloc(size_t size) {
    void *ptr = malloc(size);
    /* Return NULL on OOM - caller must handle gracefully */
    return ptr;
}

/* Secure Random Number Generation
 *
 * Uses xorshift64 PRNG seeded from /dev/urandom for fast, high-quality
 * random numbers. Falls back to time-based seeding if /dev/urandom fails.
 */

static uint64_t vm_xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static uint64_t secure_seed(void) {
    uint64_t seed = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t read = fread(&seed, sizeof(seed), 1, f);
        fclose(f);
        if (read == 1 && seed != 0) {
            return seed;
        }
    }
    /* Fallback: mix time, clock, and address for entropy */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    seed = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
    seed ^= (uint64_t)(uintptr_t)&seed * 2654435761ULL;
    seed ^= (uint64_t)clock() << 32;
    if (seed == 0) seed = 1;
    return seed;
}

/* VM Lifecycle */

VM *vm_new(void) {
    VM *vm = alloc(sizeof(VM));
    if (!vm) {
        LOG_ERROR("vm_new: allocation failed");
        return NULL;
    }

    /*
     * Lazy initialization: defer stack/frame allocation until first use.
     * This saves ~768 bytes per block that hasn't started executing yet,
     * enabling more blocks to be created before memory pressure.
     */
    vm->stack = NULL;
    vm->stack_top = NULL;
    vm->stack_capacity = 0;
    vm->frames = NULL;
    vm->frame_count = 0;
    vm->frames_capacity = 0;
    vm->initialized = false;

    vm->globals = value_map();
    vm->code = NULL;
    vm->open_upvalues = NULL;
    vm->error = NULL;
    vm->error_line = 0;
    vm->reductions = 0;
    vm->reduction_limit = 10000; /* Default limit */
    vm->block = NULL;
    vm->scheduler = NULL;

    /* Initialize secure RNG - seeded from /dev/urandom */
    vm->rng_state = secure_seed();

    return vm;
}

/**
 * Ensure VM is initialized (lazy initialization).
 * Allocates stack and frames on first use.
 * Returns true on success, false on allocation failure.
 */
static bool vm_ensure_initialized(VM *vm) {
    if (vm->initialized) return true;

    /* Allocate stack */
    vm->stack_capacity = VM_STACK_INITIAL;
    vm->stack = malloc(sizeof(NanValue) * vm->stack_capacity);
    if (!vm->stack) {
        LOG_ERROR("vm: stack allocation failed");
        return false;
    }
    vm->stack_top = vm->stack;

    /* Allocate frames */
    vm->frames_capacity = VM_FRAMES_INITIAL;
    vm->frames = malloc(sizeof(CallFrame) * vm->frames_capacity);
    if (!vm->frames) {
        LOG_ERROR("vm: frames allocation failed");
        free(vm->stack);
        vm->stack = NULL;
        vm->stack_top = NULL;
        vm->stack_capacity = 0;
        return false;
    }
    vm->frame_count = 0;

    vm->initialized = true;
    LOG_DEBUG("vm: initialized with stack_capacity=%zu frames_capacity=%zu",
              vm->stack_capacity, vm->frames_capacity);
    return true;
}

void vm_free(VM *vm) {
    if (!vm) return;

    /* Clear stack if allocated */
    if (vm->stack) {
        while (vm->stack_top > vm->stack) {
            vm->stack_top--;
            /* Note: values are owned by GC, not freed here */
        }
    }

    /* Free dynamically allocated stack and frames (may be NULL if lazy) */
    free(vm->stack);
    free(vm->frames);

    value_free(vm->globals);
    free(vm);
}

void vm_reset(VM *vm) {
    if (vm->stack) {
        vm->stack_top = vm->stack;
    }
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
    vm->error = NULL;
    vm->error_line = 0;
    vm->reductions = 0;
}

/* Dynamic Stack/Frame Growth */

/**
 * Ensure stack has room for at least 'needed' more values.
 * Returns true on success, false if allocation fails.
 */
static bool vm_ensure_stack(VM *vm, size_t needed) {
    size_t used = (size_t)(vm->stack_top - vm->stack);
    size_t required = used + needed;

    if (required <= vm->stack_capacity) {
        return true;
    }

    /* Grow by 2x until sufficient, starting from initial size if uninitialized */
    size_t new_capacity = vm->stack_capacity ? vm->stack_capacity : VM_STACK_INITIAL;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    /* Cap at maximum (for safety) */
    if (new_capacity > VM_STACK_MAX * 4) {
        new_capacity = VM_STACK_MAX * 4;
    }

    NanValue *new_stack = realloc(vm->stack, sizeof(NanValue) * new_capacity);
    if (!new_stack) {
        LOG_ERROR("vm: stack growth failed (requested %zu slots)", new_capacity);
        return false;
    }

    /* Update pointers */
    vm->stack_top = new_stack + used;
    vm->stack = new_stack;
    vm->stack_capacity = new_capacity;

    /* Update frame slot pointers */
    for (int i = 0; i < vm->frame_count; i++) {
        size_t slot_offset = (size_t)(vm->frames[i].slots - (vm->stack - used));
        vm->frames[i].slots = new_stack + slot_offset;
    }

    return true;
}

/**
 * Ensure frames array has room for one more frame.
 * Returns true on success, false if allocation fails.
 */
static bool vm_ensure_frames(VM *vm) {
    if ((size_t)vm->frame_count < vm->frames_capacity) {
        return true;
    }

    /* Grow by 2x, starting from initial size if uninitialized */
    size_t new_capacity = vm->frames_capacity ? vm->frames_capacity * 2 : VM_FRAMES_INITIAL;

    /* Cap at maximum (for safety) */
    if (new_capacity > VM_FRAMES_MAX * 4) {
        new_capacity = VM_FRAMES_MAX * 4;
    }

    CallFrame *new_frames = realloc(vm->frames, sizeof(CallFrame) * new_capacity);
    if (!new_frames) {
        LOG_ERROR("vm: frames growth failed (requested %zu frames)", new_capacity);
        return false;
    }

    vm->frames = new_frames;
    vm->frames_capacity = new_capacity;
    return true;
}

/*
 * Upvalue Management (for closures)
 */

/**
 * Capture a local variable as an upvalue.
 * Reuses existing upvalue if one already points to this slot.
 */
static Upvalue *capture_upvalue(VM *vm, NanValue *local) {
    Upvalue *prev = NULL;
    Upvalue *upvalue = vm->open_upvalues;

    /* Find existing upvalue for this slot or insertion point */
    while (upvalue != NULL && upvalue->location > local) {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    /* Reuse if already captured */
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    /* Create new upvalue */
    Upvalue *created = upvalue_new(local);

    /* Insert into sorted list */
    created->next = upvalue;
    if (prev == NULL) {
        vm->open_upvalues = created;
    } else {
        prev->next = created;
    }

    return created;
}

/**
 * Close all upvalues pointing at or above the given stack slot.
 * Called when variables go out of scope.
 */
static void close_upvalues(VM *vm, NanValue *last) {
    while (vm->open_upvalues != NULL &&
           vm->open_upvalues->location >= last) {
        Upvalue *upvalue = vm->open_upvalues;
        upvalue_close(upvalue);
        vm->open_upvalues = upvalue->next;
    }
}

/* Stack Operations (NaN-boxed) */

VMResult vm_push_nan(VM *vm, NanValue value) {
    /* Grow stack if needed */
    if (vm->stack_top >= vm->stack + vm->stack_capacity) {
        if (!vm_ensure_stack(vm, 1)) {
            vm_set_error(vm, "stack overflow");
            return VM_ERROR_STACK_OVERFLOW;
        }
    }
    *vm->stack_top++ = value;
    return VM_OK;
}

NanValue vm_pop_nan(VM *vm) {
    if (vm->stack_top <= vm->stack) {
        vm_set_error(vm, "stack underflow");
        return NANBOX_NIL;
    }
    return *--vm->stack_top;
}

NanValue vm_peek_nan(VM *vm, int distance) {
    if (vm->stack_top - distance <= vm->stack) {
        return NANBOX_NIL;
    }
    return vm->stack_top[-1 - distance];
}

/* Stack Operations (Value* compatibility) */

VMResult vm_push(VM *vm, Value *value) {
    return vm_push_nan(vm, value_to_nanbox(value));
}

Value *vm_pop(VM *vm) {
    NanValue v = vm_pop_nan(vm);
    return nanbox_to_value(v);
}

Value *vm_peek(VM *vm, int distance) {
    NanValue v = vm_peek_nan(vm, distance);
    return nanbox_to_value(v);
}

/* Execution Helpers */

static inline uint8_t read_byte(CallFrame *frame) {
    return *frame->ip++;
}

static inline uint16_t read_short(CallFrame *frame) {
    frame->ip += 2;
    return (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]);
}

static inline Value *read_constant(CallFrame *frame) {
    uint16_t index = read_short(frame);
    if (index >= frame->chunk->constants_size) {
        return NULL;  /* Invalid constant index */
    }
    return frame->chunk->constants[index];
}

/* Read constant as NanValue */
static inline NanValue read_constant_nan(CallFrame *frame) {
    uint16_t index = read_short(frame);
    if (index >= frame->chunk->constants_size) {
        return NANBOX_NIL;  /* Invalid constant index */
    }
    return value_to_nanbox(frame->chunk->constants[index]);
}

/* Jump Bounds Checking */

/**
 * Check if a forward jump target is within bounds.
 * Returns true if the jump is valid.
 */
static inline bool check_jump_forward(CallFrame *frame, uint16_t offset) {
    uint8_t *target = frame->ip + offset;
    uint8_t *code_end = frame->chunk->code + frame->chunk->code_size;
    return target <= code_end;
}

/**
 * Check if a backward jump target is within bounds.
 * Returns true if the jump is valid.
 */
static inline bool check_jump_backward(CallFrame *frame, uint16_t offset) {
    return frame->ip >= frame->chunk->code + offset;
}

/* NaN-Boxed Binary Operation Macros */

#define BINARY_OP_NUM_NAN(vm, op)                                       \
    do {                                                                \
        NanValue b = vm_pop_nan(vm);                                    \
        NanValue a = vm_pop_nan(vm);                                    \
        if (nanbox_is_int(a) && nanbox_is_int(b)) {                     \
            int64_t ia = nanbox_as_int(a);                              \
            int64_t ib = nanbox_as_int(b);                              \
            vm_push_nan(vm, nanbox_int(ia op ib));                      \
        } else if (nanbox_is_number(a) && nanbox_is_number(b)) {        \
            double da = nanbox_to_float(a);                             \
            double db = nanbox_to_float(b);                             \
            vm_push_nan(vm, nanbox_double(da op db));                   \
        } else {                                                        \
            vm_set_error(vm, "operands must be numbers");               \
            return VM_ERROR_TYPE;                                       \
        }                                                               \
    } while (0)

#define BINARY_OP_CMP_NAN(vm, op)                                       \
    do {                                                                \
        NanValue b = vm_pop_nan(vm);                                    \
        NanValue a = vm_pop_nan(vm);                                    \
        if (nanbox_is_int(a) && nanbox_is_int(b)) {                     \
            int64_t ia = nanbox_as_int(a);                              \
            int64_t ib = nanbox_as_int(b);                              \
            vm_push_nan(vm, nanbox_bool(ia op ib));                     \
        } else if (nanbox_is_number(a) && nanbox_is_number(b)) {        \
            double da = nanbox_to_float(a);                             \
            double db = nanbox_to_float(b);                             \
            vm_push_nan(vm, nanbox_bool(da op db));                     \
        } else if (nanbox_is_obj(a) && nanbox_is_obj(b)) {              \
            Value *va = (Value *)nanbox_as_obj(a);                      \
            Value *vb = (Value *)nanbox_as_obj(b);                      \
            if (value_is_string(va) && value_is_string(vb)) {           \
                int cmp = string_compare(va, vb);                       \
                vm_push_nan(vm, nanbox_bool(cmp op 0));                 \
            } else {                                                    \
                vm_set_error(vm, "cannot compare these types");         \
                return VM_ERROR_TYPE;                                   \
            }                                                           \
        } else {                                                        \
            vm_set_error(vm, "cannot compare these types");             \
            return VM_ERROR_TYPE;                                       \
        }                                                               \
    } while (0)

/* Keep old macros for compatibility with cold path */
#define BINARY_OP_NUM(vm, op)                                           \
    do {                                                                \
        Value *b = vm_pop(vm);                                          \
        Value *a = vm_pop(vm);                                          \
        if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;                  \
        if (value_is_int(a) && value_is_int(b)) {                       \
            vm_push(vm, value_int(a->as.integer op b->as.integer));     \
        } else if ((value_is_int(a) || value_is_float(a)) &&            \
                   (value_is_int(b) || value_is_float(b))) {            \
            double da = value_to_float(a);                              \
            double db = value_to_float(b);                              \
            vm_push(vm, value_float(da op db));                         \
        } else {                                                        \
            vm_set_error(vm, "operands must be numbers");               \
            return VM_ERROR_TYPE;                                       \
        }                                                               \
    } while (0)

#define BINARY_OP_CMP(vm, op)                                           \
    do {                                                                \
        Value *b = vm_pop(vm);                                          \
        Value *a = vm_pop(vm);                                          \
        if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;                  \
        if (value_is_int(a) && value_is_int(b)) {                       \
            vm_push(vm, value_bool(a->as.integer op b->as.integer));    \
        } else if ((value_is_int(a) || value_is_float(a)) &&            \
                   (value_is_int(b) || value_is_float(b))) {            \
            double da = value_to_float(a);                              \
            double db = value_to_float(b);                              \
            vm_push(vm, value_bool(da op db));                          \
        } else if (value_is_string(a) && value_is_string(b)) {          \
            int cmp = string_compare(a, b);                             \
            vm_push(vm, value_bool(cmp op 0));                          \
        } else {                                                        \
            vm_set_error(vm, "cannot compare these types");             \
            return VM_ERROR_TYPE;                                       \
        }                                                               \
    } while (0)

/* Computed Goto Dispatch */

#if defined(__GNUC__) && !defined(AGIM_NO_COMPUTED_GOTO)
#define USE_COMPUTED_GOTO 1
#else
#define USE_COMPUTED_GOTO 0
#endif

/* Reduction batching: check every 64 instructions instead of every one.
 * This significantly reduces branch overhead in the hot dispatch loop.
 * Must be a power of 2 for efficient bitmask operation. */
#define REDUCTION_BATCH 64

/* Main Execution Loop */

void vm_load(VM *vm, Bytecode *code) {
    /* Ensure lazy initialization before accessing stack/frames */
    if (!vm_ensure_initialized(vm)) {
        LOG_ERROR("vm: failed to initialize VM");
        return;
    }

    vm_reset(vm);
    vm->code = code;

    /* Set up initial frame */
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->chunk = code->main;
    frame->ip = code->main->code;
    frame->slots = vm->stack;
    frame->function = NULL;
}

VMResult vm_run(VM *vm) {
    /* Ensure lazy initialization (should already be done via vm_load) */
    if (!vm->initialized && !vm_ensure_initialized(vm)) {
        vm_set_error(vm, "failed to initialize VM");
        return VM_ERROR_RUNTIME;
    }

    if (vm->frame_count == 0) {
        vm_set_error(vm, "no code loaded");
        return VM_ERROR_RUNTIME;
    }

    CallFrame *frame = &vm->frames[vm->frame_count - 1];

#if USE_COMPUTED_GOTO
    /* Computed goto dispatch */

    /* Dispatch table mapping opcodes to label addresses
     * Hot opcodes (stack ops, arithmetic, jumps, calls) use dedicated labels
     * Cold opcodes (I/O, strings, etc.) use fallback to switch handler
     */
    static void *dispatch_table[] = {
        /* Hot path opcodes - dedicated computed goto handlers */
        [OP_NOP] = &&op_nop, [OP_POP] = &&op_pop, [OP_DUP] = &&op_dup,
        [OP_DUP2] = &&op_slow, [OP_SWAP] = &&op_swap,
        [OP_CONST] = &&op_const, [OP_NIL] = &&op_nil,
        [OP_TRUE] = &&op_true, [OP_FALSE] = &&op_false, [OP_ADD] = &&op_add,
        [OP_SUB] = &&op_sub, [OP_MUL] = &&op_mul, [OP_DIV] = &&op_div,
        [OP_MOD] = &&op_mod, [OP_NEG] = &&op_neg, [OP_EQ] = &&op_eq,
        [OP_NE] = &&op_ne, [OP_LT] = &&op_lt, [OP_LE] = &&op_le,
        [OP_GT] = &&op_gt, [OP_GE] = &&op_ge, [OP_NOT] = &&op_not,
        [OP_AND] = &&op_and, [OP_OR] = &&op_or,
        [OP_GET_LOCAL] = &&op_get_local, [OP_SET_LOCAL] = &&op_set_local,
        [OP_GET_GLOBAL] = &&op_get_global, [OP_SET_GLOBAL] = &&op_set_global,
        [OP_JUMP] = &&op_jump, [OP_JUMP_IF] = &&op_jump_if,
        [OP_JUMP_UNLESS] = &&op_jump_unless, [OP_LOOP] = &&op_loop,
        [OP_CALL] = &&op_call, [OP_RETURN] = &&op_return,
        [OP_HALT] = &&op_halt,

        /* Cold path opcodes - use switch fallback for complex operations */
        [OP_CLOSURE] = &&op_slow, [OP_ARRAY_NEW] = &&op_slow,
        [OP_ARRAY_PUSH] = &&op_slow, [OP_ARRAY_GET] = &&op_slow,
        [OP_ARRAY_SET] = &&op_slow, [OP_MAP_NEW] = &&op_slow,
        [OP_MAP_GET] = &&op_slow, [OP_MAP_SET] = &&op_slow,
        [OP_MAP_GET_IC] = &&op_map_get_ic,
        [OP_CONCAT] = &&op_slow, [OP_SPAWN] = &&op_slow, [OP_SEND] = &&op_slow,
        [OP_RECEIVE] = &&op_slow, [OP_SELF] = &&op_slow, [OP_YIELD] = &&op_slow,
        [OP_INFER] = &&op_slow, [OP_TOOL_CALL] = &&op_slow,
        [OP_MEMORY_GET] = &&op_slow, [OP_MEMORY_SET] = &&op_slow,
        [OP_LEN] = &&op_slow, [OP_TYPE] = &&op_slow, [OP_KEYS] = &&op_slow,
        [OP_PUSH] = &&op_slow, [OP_POP_ARRAY] = &&op_slow,
        [OP_SLICE] = &&op_slow, [OP_TO_STRING] = &&op_slow,
        [OP_TO_INT] = &&op_slow, [OP_TO_FLOAT] = &&op_slow,
        [OP_FILE_READ] = &&op_slow, [OP_FILE_WRITE] = &&op_slow,
        [OP_FILE_EXISTS] = &&op_slow, [OP_FILE_LINES] = &&op_slow,
        [OP_FILE_WRITE_BYTES] = &&op_slow,
        [OP_HTTP_GET] = &&op_slow, [OP_HTTP_POST] = &&op_slow,
        [OP_HTTP_PUT] = &&op_slow, [OP_HTTP_DELETE] = &&op_slow,
        [OP_HTTP_PATCH] = &&op_slow, [OP_HTTP_REQUEST] = &&op_slow,
        [OP_SHELL] = &&op_slow, [OP_JSON_PARSE] = &&op_slow,
        [OP_JSON_ENCODE] = &&op_slow, [OP_ENV_GET] = &&op_slow,
        [OP_ENV_SET] = &&op_slow, [OP_SLEEP] = &&op_slow,
        [OP_TIME] = &&op_slow, [OP_TIME_FORMAT] = &&op_slow,
        [OP_RANDOM] = &&op_slow, [OP_RANDOM_INT] = &&op_slow,
        [OP_SPLIT] = &&op_slow, [OP_JOIN] = &&op_slow, [OP_TRIM] = &&op_slow,
        [OP_REPLACE] = &&op_slow, [OP_CONTAINS] = &&op_slow,
        [OP_STARTS_WITH] = &&op_slow, [OP_ENDS_WITH] = &&op_slow,
        [OP_UPPER] = &&op_slow, [OP_LOWER] = &&op_slow,
        [OP_CHAR_AT] = &&op_slow, [OP_INDEX_OF] = &&op_slow,
        [OP_BASE64_ENCODE] = &&op_slow, [OP_BASE64_DECODE] = &&op_slow,
        [OP_READ_STDIN] = &&op_slow, [OP_PRINT_ERR] = &&op_slow,
        [OP_FLOOR] = &&op_slow, [OP_CEIL] = &&op_slow, [OP_ROUND] = &&op_slow,
        [OP_ABS] = &&op_slow, [OP_SQRT] = &&op_slow, [OP_POW] = &&op_slow,
        [OP_MIN] = &&op_slow, [OP_MAX] = &&op_slow,
        [OP_WS_CONNECT] = &&op_slow, [OP_WS_SEND] = &&op_slow,
        [OP_WS_RECV] = &&op_slow, [OP_WS_CLOSE] = &&op_slow,
        [OP_HTTP_STREAM] = &&op_slow, [OP_STREAM_READ] = &&op_slow,
        [OP_STREAM_CLOSE] = &&op_slow, [OP_EXEC] = &&op_slow,
        [OP_EXEC_ASYNC] = &&op_slow, [OP_PROC_WRITE] = &&op_slow,
        [OP_PROC_READ] = &&op_slow, [OP_PROC_CLOSE] = &&op_slow,
        [OP_UUID] = &&op_slow, [OP_HASH_MD5] = &&op_slow,
        [OP_HASH_SHA256] = &&op_slow, [OP_PRINT] = &&op_slow,
        [OP_RESULT_OK] = &&op_slow, [OP_RESULT_ERR] = &&op_slow,
        [OP_RESULT_IS_OK] = &&op_slow, [OP_RESULT_IS_ERR] = &&op_slow,
        [OP_RESULT_UNWRAP] = &&op_slow, [OP_RESULT_UNWRAP_OR] = &&op_slow,
        [OP_RESULT_MATCH] = &&op_slow,
        [OP_SOME] = &&op_slow, [OP_NONE] = &&op_slow,
        [OP_IS_SOME] = &&op_slow, [OP_IS_NONE] = &&op_slow,
        [OP_UNWRAP_OPTION] = &&op_slow, [OP_UNWRAP_OPTION_OR] = &&op_slow,
        [OP_LIST_TOOLS] = &&op_slow, [OP_TOOL_SCHEMA] = &&op_slow,
        /* Struct operations */
        [OP_STRUCT_NEW] = &&op_slow, [OP_STRUCT_GET] = &&op_slow,
        [OP_STRUCT_SET] = &&op_slow, [OP_STRUCT_GET_INDEX] = &&op_slow,
        /* Enum operations */
        [OP_ENUM_NEW] = &&op_slow, [OP_ENUM_IS] = &&op_slow,
        [OP_ENUM_PAYLOAD] = &&op_slow,
    };

    /* Dispatch macro: batched reduction check every REDUCTION_BATCH instructions */
    #define DISPATCH()                                                      \
        do {                                                                \
            if ((++vm->reductions & (REDUCTION_BATCH - 1)) == 0) {          \
                if (vm->reductions >= vm->reduction_limit)                  \
                    return VM_YIELD;                                        \
            }                                                               \
            goto *dispatch_table[read_byte(frame)];                         \
        } while (0)

    /* Dispatch without reduction check (for chained operations) */
    #define DISPATCH_FAST() goto *dispatch_table[read_byte(frame)]

    /* Target label macro */
    #define TARGET(op) op_##op

    /* Start dispatch */
    DISPATCH();

    /* Hot path opcodes using NaN boxing for maximum performance */

    TARGET(nop):
        DISPATCH();

    TARGET(pop):
        vm_pop_nan(vm);
        DISPATCH();

    TARGET(dup): {
        /* Security: check for underflow before duplicating */
        if (vm->stack_top <= vm->stack) {
            vm_set_error(vm, "stack underflow");
            return VM_ERROR_STACK_UNDERFLOW;
        }
        NanValue top = vm_peek_nan(vm, 0);
        vm_push_nan(vm, top);
        DISPATCH();
    }

    TARGET(swap): {
        /* Security: check for underflow - need at least 2 elements */
        if (vm->stack_top - vm->stack < 2) {
            vm_set_error(vm, "stack underflow");
            return VM_ERROR_STACK_UNDERFLOW;
        }
        NanValue a = vm_pop_nan(vm);
        NanValue b = vm_pop_nan(vm);
        vm_push_nan(vm, a);
        vm_push_nan(vm, b);
        DISPATCH();
    }

    TARGET(const): {
        /* Constants are still Value*, convert to NanValue */
        NanValue constant = read_constant_nan(frame);
        vm_push_nan(vm, constant);
        DISPATCH();
    }

    TARGET(nil):
        vm_push_nan(vm, NANBOX_NIL);
        DISPATCH();

    TARGET(true):
        vm_push_nan(vm, NANBOX_TRUE);
        DISPATCH();

    TARGET(false):
        vm_push_nan(vm, NANBOX_FALSE);
        DISPATCH();

    TARGET(add): {
        NanValue b = vm_peek_nan(vm, 0);
        NanValue a = vm_peek_nan(vm, 1);
        /* Check for string concatenation - handle nil as empty string */
        bool a_str = nanbox_is_nil(a) || (nanbox_is_obj(a) && value_is_string((Value *)nanbox_as_obj(a)));
        bool b_str = nanbox_is_nil(b) || (nanbox_is_obj(b) && value_is_string((Value *)nanbox_as_obj(b)));
        if (a_str && b_str) {
            vm_pop_nan(vm);
            vm_pop_nan(vm);
            Value *str_a = nanbox_is_nil(a) ? value_string("") : (Value *)nanbox_as_obj(a);
            Value *str_b = nanbox_is_nil(b) ? value_string("") : (Value *)nanbox_as_obj(b);
            vm_push(vm, string_concat(str_a, str_b));
            DISPATCH();
        }
        /* Numeric addition */
        BINARY_OP_NUM_NAN(vm, +);
        DISPATCH();
    }

    TARGET(sub):
        BINARY_OP_NUM_NAN(vm, -);
        DISPATCH();

    TARGET(mul):
        BINARY_OP_NUM_NAN(vm, *);
        DISPATCH();

    TARGET(div): {
        NanValue b = vm_peek_nan(vm, 0);
        if (nanbox_is_int(b) && nanbox_as_int(b) == 0) {
            vm_set_error(vm, "division by zero");
            return VM_ERROR_DIVISION_BY_ZERO;
        }
        if (nanbox_is_double(b) && nanbox_as_double(b) == 0.0) {
            vm_set_error(vm, "division by zero");
            return VM_ERROR_DIVISION_BY_ZERO;
        }
        BINARY_OP_NUM_NAN(vm, /);
        DISPATCH();
    }

    TARGET(mod): {
        NanValue b = vm_pop_nan(vm);
        NanValue a = vm_pop_nan(vm);
        if (!nanbox_is_int(a) || !nanbox_is_int(b)) {
            vm_set_error(vm, "modulo requires integers");
            return VM_ERROR_TYPE;
        }
        int64_t ib = nanbox_as_int(b);
        if (ib == 0) {
            vm_set_error(vm, "division by zero");
            return VM_ERROR_DIVISION_BY_ZERO;
        }
        vm_push_nan(vm, nanbox_int(nanbox_as_int(a) % ib));
        DISPATCH();
    }

    TARGET(neg): {
        NanValue v = vm_pop_nan(vm);
        if (nanbox_is_int(v)) {
            vm_push_nan(vm, nanbox_int(-nanbox_as_int(v)));
        } else if (nanbox_is_double(v)) {
            vm_push_nan(vm, nanbox_double(-nanbox_as_double(v)));
        } else {
            vm_set_error(vm, "operand must be a number");
            return VM_ERROR_TYPE;
        }
        DISPATCH();
    }

    TARGET(eq): {
        NanValue b = vm_pop_nan(vm);
        NanValue a = vm_pop_nan(vm);
        /* For objects, need deep equality check */
        if (nanbox_is_obj(a) && nanbox_is_obj(b)) {
            Value *va = (Value *)nanbox_as_obj(a);
            Value *vb = (Value *)nanbox_as_obj(b);
            vm_push_nan(vm, nanbox_bool(value_equals(va, vb)));
        } else {
            vm_push_nan(vm, nanbox_bool(nanbox_equal(a, b)));
        }
        DISPATCH();
    }

    TARGET(ne): {
        NanValue b = vm_pop_nan(vm);
        NanValue a = vm_pop_nan(vm);
        if (nanbox_is_obj(a) && nanbox_is_obj(b)) {
            Value *va = (Value *)nanbox_as_obj(a);
            Value *vb = (Value *)nanbox_as_obj(b);
            vm_push_nan(vm, nanbox_bool(!value_equals(va, vb)));
        } else {
            vm_push_nan(vm, nanbox_bool(!nanbox_equal(a, b)));
        }
        DISPATCH();
    }

    TARGET(lt):
        BINARY_OP_CMP_NAN(vm, <);
        DISPATCH();

    TARGET(le):
        BINARY_OP_CMP_NAN(vm, <=);
        DISPATCH();

    TARGET(gt):
        BINARY_OP_CMP_NAN(vm, >);
        DISPATCH();

    TARGET(ge):
        BINARY_OP_CMP_NAN(vm, >=);
        DISPATCH();

    TARGET(not): {
        NanValue v = vm_pop_nan(vm);
        vm_push_nan(vm, nanbox_bool(!nanbox_is_truthy(v)));
        DISPATCH();
    }

    TARGET(and):
        /* Short-circuit AND handled in compiler via jumps */
        DISPATCH();

    TARGET(or):
        /* Short-circuit OR handled in compiler via jumps */
        DISPATCH();

    TARGET(get_local): {
        uint16_t slot = read_short(frame);
        vm_push_nan(vm, frame->slots[slot]);
        DISPATCH();
    }

    TARGET(set_local): {
        uint16_t slot = read_short(frame);
        frame->slots[slot] = vm_peek_nan(vm, 0);
        DISPATCH();
    }

    TARGET(get_global): {
        uint16_t index = read_short(frame);
        const char *name = bytecode_get_string(vm->code, index);
        Value *value = map_get(vm->globals, name);
        if (!value) {
            vm_set_error(vm, "undefined variable");
            return VM_ERROR_UNDEFINED_VARIABLE;
        }
        vm_push_nan(vm, value_to_nanbox(value));
        DISPATCH();
    }

    TARGET(set_global): {
        uint16_t index = read_short(frame);
        const char *name = bytecode_get_string(vm->code, index);
        NanValue v = vm_peek_nan(vm, 0);
        map_set(vm->globals, name, nanbox_to_value(v));
        DISPATCH();
    }

    TARGET(jump): {
        uint16_t offset = read_short(frame);
        if (!check_jump_forward(frame, offset)) {
            vm_set_error(vm, "jump out of bounds");
            return VM_ERROR_RUNTIME;
        }
        frame->ip += offset;
        DISPATCH();
    }

    TARGET(jump_if): {
        uint16_t offset = read_short(frame);
        if (nanbox_is_truthy(vm_peek_nan(vm, 0))) {
            if (!check_jump_forward(frame, offset)) {
                vm_set_error(vm, "jump out of bounds");
                return VM_ERROR_RUNTIME;
            }
            frame->ip += offset;
        }
        DISPATCH();
    }

    TARGET(jump_unless): {
        uint16_t offset = read_short(frame);
        if (!nanbox_is_truthy(vm_peek_nan(vm, 0))) {
            if (!check_jump_forward(frame, offset)) {
                vm_set_error(vm, "jump out of bounds");
                return VM_ERROR_RUNTIME;
            }
            frame->ip += offset;
        }
        DISPATCH();
    }

    TARGET(loop): {
        uint16_t offset = read_short(frame);
        if (!check_jump_backward(frame, offset)) {
            vm_set_error(vm, "loop jump out of bounds");
            return VM_ERROR_RUNTIME;
        }
        frame->ip -= offset;
        DISPATCH();
    }

    TARGET(call): {
        uint16_t arg_count = read_short(frame);
        NanValue callee_nan = vm_peek_nan(vm, arg_count);
        Function *fn = NULL;
        Value *callee_val = NULL;

        /* Check if it's an object (function or closure) */
        if (nanbox_is_obj(callee_nan)) {
            callee_val = (Value *)nanbox_as_obj(callee_nan);
            if (callee_val && callee_val->type == VAL_FUNCTION) {
                fn = callee_val->as.function;
            } else if (callee_val && callee_val->type == VAL_CLOSURE) {
                fn = closure_function(callee_val);
            }
        }

        if (!fn) {
            vm_set_error(vm, "can only call functions");
            return VM_ERROR_TYPE;
        }
        if (arg_count != fn->arity) {
            vm_set_error(vm, "wrong number of arguments");
            return VM_ERROR_ARITY;
        }
        /* Grow frames array if needed */
        if ((size_t)vm->frame_count >= vm->frames_capacity) {
            if (!vm_ensure_frames(vm)) {
                vm_set_error(vm, "stack overflow (too many frames)");
                return VM_ERROR_STACK_OVERFLOW;
            }
        }
        CallFrame *new_frame = &vm->frames[vm->frame_count++];
        new_frame->function = fn;
        new_frame->chunk = vm->code->functions[fn->code_offset];
        new_frame->ip = new_frame->chunk->code;
        new_frame->slots = vm->stack_top - arg_count - 1;
        frame = new_frame;
        DISPATCH();
    }

    TARGET(return): {
        NanValue result = vm_pop_nan(vm);
        close_upvalues(vm, frame->slots);
        vm->frame_count--;
        if (vm->frame_count == 0) {
            vm_pop_nan(vm);
            return VM_OK;
        }
        vm->stack_top = frame->slots;
        vm_push_nan(vm, result);
        frame = &vm->frames[vm->frame_count - 1];
        DISPATCH();
    }

    TARGET(halt):
        return VM_HALT;

    /* Inline cache optimized map/struct access */
    TARGET(map_get_ic): {
        uint16_t key_idx = read_short(frame);
        uint16_t ic_slot = read_short(frame);

        NanValue map_val = vm_pop_nan(vm);
        Value *map = nanbox_to_value(map_val);

        if (!map) {
            vm_set_error(vm, "expected map or struct");
            return VM_ERROR_TYPE;
        }

        /* Handle struct field access */
        if (value_is_struct(map)) {
            const char *key = bytecode_get_string(vm->code, key_idx);
            if (!key) {
                vm_set_error(vm, "invalid string index");
                return VM_ERROR_TYPE;
            }
            Value *result = value_struct_get_field(map, key);
            vm_push(vm, result ? result : value_nil());
            DISPATCH();
        }

        if (!value_is_map(map)) {
            vm_set_error(vm, "expected map or struct");
            return VM_ERROR_TYPE;
        }

        const char *key = bytecode_get_string(vm->code, key_idx);
        if (!key) {
            vm_set_error(vm, "invalid string index");
            return VM_ERROR_TYPE;
        }

        /* Get the IC slot */
        Chunk *chunk = frame->chunk;
        if (!chunk || ic_slot >= chunk->ic_count) {
            /* No IC available, fall back to normal lookup */
            Value *result = map_get(map, key);
            vm_push(vm, result ? result : value_nil());
            DISPATCH();
        }

        InlineCache *ic = &chunk->ic_slots[ic_slot];
        Value *result = NULL;

        /* Try cache lookup first */
        if (ic_lookup(ic, map, key, &result)) {
            /* Cache hit - fast path */
            vm_push(vm, result ? result : value_nil());
            DISPATCH();
        }

        /* Cache miss - do normal lookup and update cache */
        Map *m = map->as.map;
        size_t key_len = strlen(key);
        size_t key_hash = agim_hash_string(key, key_len);
        size_t bucket = key_hash % m->capacity;

        result = map_get(map, key);
        if (result) {
            /* Update cache with successful lookup */
            ic_update(ic, map, bucket);
        }

        vm_push(vm, result ? result : value_nil());
        DISPATCH();
    }

    /* Fallback for cold opcodes - rewind IP and use switch */
    op_slow:
        frame->ip--;  /* Rewind to re-read the opcode */
        goto slow_dispatch;

    #undef DISPATCH
    #undef DISPATCH_FAST
    #undef TARGET

#endif /* USE_COMPUTED_GOTO */

    /* Label for slow path dispatch (used by computed goto fallback) */
    slow_dispatch: (void)0;

    /* Switch-based dispatch (portable fallback) */

#if USE_COMPUTED_GOTO
    /* When using computed goto, handle one slow opcode then return to fast path */
    {
        uint8_t instruction = read_byte(frame);
        switch (instruction) {
#else
    for (;;) {
        /* Check reduction limit */
        if (++vm->reductions >= vm->reduction_limit) {
            return VM_YIELD;
        }

#ifdef AGIM_DEBUG
        /* Debug: print stack */
        printf("          ");
        for (NanValue *slot = vm->stack; slot < vm->stack_top; slot++) {
            printf("[ ");
            Value *v = nanbox_to_value(*slot);
            value_print(v);
            printf(" ]");
        }
        printf("\n");

        /* Debug: disassemble current instruction */
        chunk_disassemble_instruction(
            frame->chunk,
            (size_t)(frame->ip - frame->chunk->code));
#endif

        uint8_t instruction = read_byte(frame);

        switch (instruction) {
#endif /* USE_COMPUTED_GOTO */

        case OP_NOP:
            break;

        case OP_POP:
            vm_pop(vm);
            break;

        case OP_DUP: {
            /* Security: check for underflow before duplicating */
            if (vm->stack_top <= vm->stack) {
                vm_set_error(vm, "stack underflow");
                return VM_ERROR_STACK_UNDERFLOW;
            }
            Value *top = vm_peek(vm, 0);
            vm_push(vm, top);
            break;
        }

        case OP_DUP2: {
            /* Duplicate top two stack items: [a, b] -> [a, b, a, b] */
            /* Security: need at least 2 elements */
            if (vm->stack_top - vm->stack < 2) {
                vm_set_error(vm, "stack underflow");
                return VM_ERROR_STACK_UNDERFLOW;
            }
            Value *b = vm_peek(vm, 0);
            Value *a = vm_peek(vm, 1);
            vm_push(vm, a);
            vm_push(vm, b);
            break;
        }

        case OP_SWAP: {
            /* Security: need at least 2 elements */
            if (vm->stack_top - vm->stack < 2) {
                vm_set_error(vm, "stack underflow");
                return VM_ERROR_STACK_UNDERFLOW;
            }
            Value *a = vm_pop(vm);
            Value *b = vm_pop(vm);
            vm_push(vm, a);
            vm_push(vm, b);
            break;
        }

        case OP_CONST: {
            Value *constant = read_constant(frame);
            vm_push(vm, value_copy(constant));
            break;
        }

        case OP_NIL:
            vm_push(vm, value_nil());
            break;

        case OP_TRUE:
            vm_push(vm, value_bool(true));
            break;

        case OP_FALSE:
            vm_push(vm, value_bool(false));
            break;

        case OP_ADD: {
            Value *b = vm_peek(vm, 0);
            Value *a = vm_peek(vm, 1);
            if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;

            /* String concatenation - handle nil as empty string */
            bool a_str = value_is_string(a) || value_is_nil(a);
            bool b_str = value_is_string(b) || value_is_nil(b);
            if (a_str && b_str) {
                vm_pop(vm);
                vm_pop(vm);
                Value *str_a = value_is_nil(a) ? value_string("") : a;
                Value *str_b = value_is_nil(b) ? value_string("") : b;
                vm_push(vm, string_concat(str_a, str_b));
            } else {
                BINARY_OP_NUM(vm, +);
            }
            break;
        }

        case OP_SUB:
            BINARY_OP_NUM(vm, -);
            break;

        case OP_MUL:
            BINARY_OP_NUM(vm, *);
            break;

        case OP_DIV: {
            Value *b = vm_peek(vm, 0);
            if (value_is_int(b) && b->as.integer == 0) {
                vm_set_error(vm, "division by zero");
                return VM_ERROR_DIVISION_BY_ZERO;
            }
            if (value_is_float(b) && b->as.floating == 0.0) {
                vm_set_error(vm, "division by zero");
                return VM_ERROR_DIVISION_BY_ZERO;
            }
            BINARY_OP_NUM(vm, /);
            break;
        }

        case OP_MOD: {
            Value *b = vm_pop(vm);
            Value *a = vm_pop(vm);
            if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;
            if (!value_is_int(a) || !value_is_int(b)) {
                vm_set_error(vm, "modulo requires integers");
                return VM_ERROR_TYPE;
            }
            if (b->as.integer == 0) {
                vm_set_error(vm, "division by zero");
                return VM_ERROR_DIVISION_BY_ZERO;
            }
            vm_push(vm, value_int(a->as.integer % b->as.integer));
            break;
        }

        case OP_NEG: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            if (value_is_int(v)) {
                vm_push(vm, value_int(-v->as.integer));
            } else if (value_is_float(v)) {
                vm_push(vm, value_float(-v->as.floating));
            } else {
                vm_set_error(vm, "operand must be a number");
                return VM_ERROR_TYPE;
            }
            break;
        }

        case OP_EQ: {
            Value *b = vm_pop(vm);
            Value *a = vm_pop(vm);
            if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;
            vm_push(vm, value_bool(value_equals(a, b)));
            break;
        }

        case OP_NE: {
            Value *b = vm_pop(vm);
            Value *a = vm_pop(vm);
            if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;
            vm_push(vm, value_bool(!value_equals(a, b)));
            break;
        }

        case OP_LT:
            BINARY_OP_CMP(vm, <);
            break;

        case OP_LE:
            BINARY_OP_CMP(vm, <=);
            break;

        case OP_GT:
            BINARY_OP_CMP(vm, >);
            break;

        case OP_GE:
            BINARY_OP_CMP(vm, >=);
            break;

        case OP_NOT: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            vm_push(vm, value_bool(!value_is_truthy(v)));
            break;
        }

        case OP_GET_LOCAL: {
            uint16_t slot = read_short(frame);
            vm_push_nan(vm, frame->slots[slot]);
            break;
        }

        case OP_SET_LOCAL: {
            uint16_t slot = read_short(frame);
            frame->slots[slot] = vm_peek_nan(vm, 0);
            break;
        }

        case OP_GET_GLOBAL: {
            uint16_t index = read_short(frame);
            const char *name = bytecode_get_string(vm->code, index);
            Value *value = map_get(vm->globals, name);
            if (!value) {
                vm_set_error(vm, "undefined variable");
                return VM_ERROR_UNDEFINED_VARIABLE;
            }
            vm_push(vm, value);
            break;
        }

        case OP_SET_GLOBAL: {
            uint16_t index = read_short(frame);
            const char *name = bytecode_get_string(vm->code, index);
            map_set(vm->globals, name, vm_peek(vm, 0));
            break;
        }

        case OP_JUMP: {
            uint16_t offset = read_short(frame);
            if (!check_jump_forward(frame, offset)) {
                vm_set_error(vm, "jump out of bounds");
                return VM_ERROR_RUNTIME;
            }
            frame->ip += offset;
            break;
        }

        case OP_JUMP_IF: {
            uint16_t offset = read_short(frame);
            if (value_is_truthy(vm_peek(vm, 0))) {
                if (!check_jump_forward(frame, offset)) {
                    vm_set_error(vm, "jump out of bounds");
                    return VM_ERROR_RUNTIME;
                }
                frame->ip += offset;
            }
            break;
        }

        case OP_JUMP_UNLESS: {
            uint16_t offset = read_short(frame);
            if (!value_is_truthy(vm_peek(vm, 0))) {
                if (!check_jump_forward(frame, offset)) {
                    vm_set_error(vm, "jump out of bounds");
                    return VM_ERROR_RUNTIME;
                }
                frame->ip += offset;
            }
            break;
        }

        case OP_LOOP: {
            uint16_t offset = read_short(frame);
            if (!check_jump_backward(frame, offset)) {
                vm_set_error(vm, "loop jump out of bounds");
                return VM_ERROR_RUNTIME;
            }
            frame->ip -= offset;
            break;
        }

        case OP_CALL: {
            uint16_t arg_count = read_short(frame);
            Value *callee = vm_peek(vm, arg_count);

            Function *fn = NULL;

            if (callee && callee->type == VAL_FUNCTION) {
                fn = callee->as.function;
            } else if (callee && callee->type == VAL_CLOSURE) {
                fn = closure_function(callee);
            } else {
                vm_set_error(vm, "can only call functions");
                return VM_ERROR_TYPE;
            }

            if (!fn) {
                vm_set_error(vm, "invalid function");
                return VM_ERROR_TYPE;
            }

            if (arg_count != fn->arity) {
                vm_set_error(vm, "wrong number of arguments");
                return VM_ERROR_ARITY;
            }

            /* Grow frames array if needed */
            if ((size_t)vm->frame_count >= vm->frames_capacity) {
                if (!vm_ensure_frames(vm)) {
                    vm_set_error(vm, "stack overflow (too many frames)");
                    return VM_ERROR_STACK_OVERFLOW;
                }
            }

            CallFrame *new_frame = &vm->frames[vm->frame_count++];
            new_frame->function = fn;
            new_frame->chunk = vm->code->functions[fn->code_offset];
            new_frame->ip = new_frame->chunk->code;
            new_frame->slots = vm->stack_top - arg_count - 1;
            frame = new_frame;
            break;
        }

        case OP_RETURN: {
            Value *result = vm_pop(vm);

            /* Close any upvalues owned by this frame */
            close_upvalues(vm, frame->slots);

            vm->frame_count--;

            if (vm->frame_count == 0) {
                vm_pop(vm); /* Pop the script function */
                return VM_OK;
            }

            vm->stack_top = frame->slots;
            vm_push(vm, result);
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }

        case OP_CLOSURE: {
            /* Read function index */
            uint16_t func_index = read_short(frame);
            if (func_index >= vm->code->functions_count) {
                vm_set_error(vm, "invalid function index");
                return VM_ERROR_RUNTIME;
            }

            /* Read upvalue count */
            uint8_t upvalue_count = read_byte(frame);

            /* Create function for the closure */
            Function *fn = malloc(sizeof(Function));
            if (!fn) {
                LOG_ERROR("vm: failed to allocate Function for closure");
                vm_set_error(vm, "out of memory");
                return VM_ERROR_RUNTIME;
            }
            fn->name = NULL;
            fn->arity = 0; /* Compiler should encode arity in bytecode */
            fn->code_offset = func_index;
            fn->locals_count = 0;
            fn->parent = NULL;

            /* Create the closure value (allocates both Value and Closure) */
            Value *closure_val = value_closure(fn, upvalue_count);
            if (!closure_val || closure_val->type != VAL_CLOSURE) {
                vm_set_error(vm, "failed to create closure");
                return VM_ERROR_RUNTIME;
            }

            /* Capture upvalues */
            for (size_t i = 0; i < upvalue_count; i++) {
                uint8_t is_local = read_byte(frame);
                uint8_t index = read_byte(frame);

                Upvalue *upvalue;
                if (is_local) {
                    /* Capture local variable from current frame */
                    upvalue = capture_upvalue(vm, frame->slots + index);
                } else {
                    /* Copy upvalue from enclosing closure */
                    /* For nested closures, we need to copy from the enclosing
                     * closure's upvalue array. For now, treat as local. */
                    upvalue = capture_upvalue(vm, frame->slots + index);
                }
                closure_set_upvalue(closure_val, i, upvalue);
            }

            vm_push(vm, closure_val);
            break;
        }

        case OP_ARRAY_NEW:
            vm_push(vm, value_array());
            break;

        case OP_ARRAY_PUSH: {
            Value *item = vm_pop(vm);
            Value *arr = vm_pop(vm);  /* Pop to allow COW replacement */
            if (!arr || !value_is_array(arr)) {
                vm_set_error(vm, "expected array");
                return VM_ERROR_TYPE;
            }
            arr = array_push(arr, item);  /* May return new Value if COW */
            vm_push(vm, arr);  /* Push back (possibly new) array */
            break;
        }

        case OP_ARRAY_GET: {
            Value *index = vm_pop(vm);
            Value *container = vm_pop(vm);
            if (!container) {
                vm_set_error(vm, "expected array or map");
                return VM_ERROR_TYPE;
            }
            if (value_is_array(container)) {
                if (!value_is_int(index)) {
                    vm_set_error(vm, "array index must be integer");
                    return VM_ERROR_TYPE;
                }
                /* Bounds check: reject negative indices */
                int64_t idx = index->as.integer;
                if (idx < 0) {
                    vm_set_error(vm, "array index out of bounds (negative)");
                    return VM_ERROR_OUT_OF_BOUNDS;
                }
                if ((size_t)idx >= container->as.array->length) {
                    vm_set_error(vm, "array index out of bounds");
                    return VM_ERROR_OUT_OF_BOUNDS;
                }
                Value *item = array_get(container, (size_t)idx);
                vm_push(vm, item ? item : value_nil());
            } else if (value_is_map(container)) {
                if (!value_is_string(index)) {
                    vm_set_error(vm, "map key must be string");
                    return VM_ERROR_TYPE;
                }
                Value *item = map_get(container, index->as.string->data);
                vm_push(vm, item ? item : value_nil());
            } else {
                vm_set_error(vm, "expected array or map");
                return VM_ERROR_TYPE;
            }
            break;
        }

        case OP_ARRAY_SET: {
            Value *value = vm_pop(vm);
            Value *index = vm_pop(vm);
            Value *container = vm_pop(vm);  /* Pop to allow COW replacement */
            if (!container) {
                vm_set_error(vm, "expected array or map");
                return VM_ERROR_TYPE;
            }
            if (value_is_array(container)) {
                if (!value_is_int(index)) {
                    vm_set_error(vm, "array index must be integer");
                    return VM_ERROR_TYPE;
                }
                /* Bounds check: reject negative indices */
                int64_t idx = index->as.integer;
                if (idx < 0) {
                    vm_set_error(vm, "array index out of bounds (negative)");
                    return VM_ERROR_OUT_OF_BOUNDS;
                }
                if ((size_t)idx >= container->as.array->length) {
                    vm_set_error(vm, "array index out of bounds");
                    return VM_ERROR_OUT_OF_BOUNDS;
                }
                container = array_set(container, (size_t)idx, value);
            } else if (value_is_map(container)) {
                if (!value_is_string(index)) {
                    vm_set_error(vm, "map key must be string");
                    return VM_ERROR_TYPE;
                }
                container = map_set(container, index->as.string->data, value);
            } else {
                vm_set_error(vm, "expected array or map");
                return VM_ERROR_TYPE;
            }
            vm_push(vm, container);  /* Push back (possibly new) container */
            break;
        }

        case OP_MAP_NEW:
            vm_push(vm, value_map());
            break;

        case OP_MAP_GET: {
            Value *key = vm_pop(vm);
            Value *map = vm_pop(vm);
            if (!map || !value_is_map(map)) {
                vm_set_error(vm, "expected map");
                return VM_ERROR_TYPE;
            }
            if (!value_is_string(key)) {
                vm_set_error(vm, "map key must be string");
                return VM_ERROR_TYPE;
            }
            Value *item = map_get(map, key->as.string->data);
            vm_push(vm, item ? item : value_nil());
            break;
        }

        case OP_MAP_SET: {
            Value *val = vm_pop(vm);
            Value *key = vm_pop(vm);
            Value *map = vm_pop(vm);  /* Pop to allow COW replacement */
            if (!map || !value_is_map(map)) {
                vm_set_error(vm, "expected map");
                return VM_ERROR_TYPE;
            }
            if (!value_is_string(key)) {
                vm_set_error(vm, "map key must be string");
                return VM_ERROR_TYPE;
            }
            map = map_set(map, key->as.string->data, val);
            vm_push(vm, map);  /* Push back (possibly new) map */
            break;
        }

        case OP_CONCAT: {
            Value *b = vm_pop(vm);
            Value *a = vm_pop(vm);
            if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;
            if (!value_is_string(a) || !value_is_string(b)) {
                vm_set_error(vm, "concat requires strings");
                return VM_ERROR_TYPE;
            }
            vm_push(vm, string_concat(a, b));
            break;
        }

        case OP_LEN: {
            Value *v = vm_peek(vm, 0);
            int64_t len = 0;
            if (!v || value_is_nil(v)) {
                len = 0;
            } else if (value_is_array(v)) {
                len = (int64_t)v->as.array->length;
            } else if (value_is_string(v)) {
                len = (int64_t)strlen(v->as.string->data);
            } else if (value_is_map(v)) {
                len = (int64_t)v->as.map->size;
            } else {
                vm_set_error(vm, "len() requires array, string, or map");
                return VM_ERROR_TYPE;
            }
            vm_pop(vm);
            vm_push(vm, value_int(len));
            break;
        }

        case OP_TYPE: {
            Value *v = vm_pop(vm);
            const char *type_name = "nil";
            if (v) {
                switch (v->type) {
                case VAL_NIL: type_name = "nil"; break;
                case VAL_BOOL: type_name = "bool"; break;
                case VAL_INT: type_name = "int"; break;
                case VAL_FLOAT: type_name = "float"; break;
                case VAL_STRING: type_name = "string"; break;
                case VAL_ARRAY: type_name = "array"; break;
                case VAL_MAP: type_name = "map"; break;
                case VAL_FUNCTION: type_name = "function"; break;
                case VAL_CLOSURE: type_name = "closure"; break;
                case VAL_RESULT: type_name = "result"; break;
                default: type_name = "unknown"; break;
                }
            }
            vm_push(vm, value_string(type_name));
            break;
        }

        case OP_KEYS: {
            Value *v = vm_pop(vm);
            if (!v || !value_is_map(v)) {
                vm_set_error(vm, "keys() requires map");
                return VM_ERROR_TYPE;
            }
            Value *arr = value_array();
            for (size_t i = 0; i < v->as.map->capacity; i++) {
                MapEntry *entry = v->as.map->buckets[i];
                while (entry) {
                    array_push(arr, value_string(entry->key->data));
                    entry = entry->next;
                }
            }
            vm_push(vm, arr);
            break;
        }

        case OP_PUSH: {
            Value *val = vm_pop(vm);
            Value *arr = vm_pop(vm);
            if (!arr || !value_is_array(arr)) {
                vm_set_error(vm, "push() requires array");
                return VM_ERROR_TYPE;
            }
            arr = array_push(arr, val);
            vm_push(vm, arr);  /* Push back the (possibly new) array */
            break;
        }

        case OP_POP_ARRAY: {
            Value *arr = vm_pop(vm);
            if (!arr || !value_is_array(arr)) {
                vm_set_error(vm, "pop() requires array");
                return VM_ERROR_TYPE;
            }
            Value *new_arr;
            Value *val = array_pop(arr, &new_arr);
            /* Push both values: popped element first, then modified array on top */
            vm_push(vm, val ? val : value_nil());
            vm_push(vm, new_arr);
            break;
        }

        case OP_SLICE: {
            Value *end_v = vm_pop(vm);
            Value *start_v = vm_pop(vm);
            Value *container = vm_pop(vm);
            if (!value_is_int(start_v) || !value_is_int(end_v)) {
                vm_set_error(vm, "slice indices must be integers");
                return VM_ERROR_TYPE;
            }
            int64_t start = start_v->as.integer;
            int64_t end = end_v->as.integer;
            if (value_is_string(container)) {
                size_t len = strlen(container->as.string->data);
                /* Clamp indices to valid range (no negative indexing) */
                if (start < 0) start = 0;
                if (end < 0) end = 0;
                if (start > (int64_t)len) start = (int64_t)len;
                if (end > (int64_t)len) end = (int64_t)len;
                if (start >= end) {
                    vm_push(vm, value_string(""));
                } else {
                    size_t slice_len = (size_t)(end - start);
                    char *slice = malloc(slice_len + 1);
                    if (!slice) {
                        LOG_ERROR("vm: failed to allocate string slice of %zu bytes", slice_len + 1);
                        vm_set_error(vm, "out of memory");
                        return VM_ERROR_RUNTIME;
                    }
                    memcpy(slice, container->as.string->data + start, slice_len);
                    slice[slice_len] = '\0';
                    vm_push(vm, value_string(slice));
                    free(slice);
                }
            } else if (value_is_array(container)) {
                size_t len = container->as.array->length;
                /* Clamp indices to valid range (no negative indexing) */
                if (start < 0) start = 0;
                if (end < 0) end = 0;
                if (start > (int64_t)len) start = (int64_t)len;
                if (end > (int64_t)len) end = (int64_t)len;
                Value *arr = value_array();
                for (int64_t i = start; i < end; i++) {
                    array_push(arr, container->as.array->items[i]);
                }
                vm_push(vm, arr);
            } else {
                vm_set_error(vm, "slice() requires string or array");
                return VM_ERROR_TYPE;
            }
            break;
        }

        case OP_TO_STRING: {
            Value *v = vm_pop(vm);
            char *str = value_repr(v);
            vm_push(vm, value_string(str));
            free(str);
            break;
        }

        case OP_TO_INT: {
            Value *v = vm_pop(vm);
            int64_t result = 0;
            if (value_is_int(v)) {
                result = v->as.integer;
            } else if (value_is_float(v)) {
                result = (int64_t)v->as.floating;
            } else if (value_is_string(v)) {
                result = strtol(v->as.string->data, NULL, 10);
            } else if (value_is_bool(v)) {
                result = v->as.boolean ? 1 : 0;
            }
            vm_push(vm, value_int(result));
            break;
        }

        case OP_TO_FLOAT: {
            Value *v = vm_pop(vm);
            double result = 0.0;
            if (value_is_float(v)) {
                result = v->as.floating;
            } else if (value_is_int(v)) {
                result = (double)v->as.integer;
            } else if (value_is_string(v)) {
                result = strtod(v->as.string->data, NULL);
            } else if (value_is_bool(v)) {
                result = v->as.boolean ? 1.0 : 0.0;
            }
            vm_push(vm, value_float(result));
            break;
        }

        case OP_FILE_READ: {
            /* Capability check: require CAP_FILE_READ */
            Block *block = (Block *)vm->block;
            if (block && !block_has_cap(block, CAP_FILE_READ)) {
                vm_push(vm, value_result_err(value_string("file read requires CAP_FILE_READ")));
                break;
            }
            Value *path = vm_pop(vm);
            if (!value_is_string(path)) {
                vm_set_error(vm, "file path must be string");
                return VM_ERROR_TYPE;
            }
            /* Sandbox check: validate path */
            Sandbox *sandbox = sandbox_global();
            char *resolved = sandbox_resolve_read(sandbox, path->as.string->data);
            if (!resolved) {
                vm_push(vm, value_result_err(value_string("file read denied by sandbox")));
                break;
            }
            FILE *f = fopen(resolved, "rb");
            if (!f) {
                char errmsg[256];
                snprintf(errmsg, sizeof(errmsg), "cannot open file: %s", resolved);
                free(resolved);
                vm_push(vm, value_result_err(value_string(errmsg)));
            } else {
                free(resolved);
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *content = malloc(size + 1);
                if (!content) {
                    fclose(f);
                    vm_push(vm, value_result_err(value_string("out of memory")));
                    break;
                }
                size_t nread = fread(content, 1, size, f);
                content[nread] = '\0';
                fclose(f);
                vm_push(vm, value_result_ok(value_string(content)));
                free(content);
            }
            break;
        }

        case OP_FILE_WRITE: {
            /* Capability check: require CAP_FILE_WRITE */
            Block *block = (Block *)vm->block;
            if (block && !block_has_cap(block, CAP_FILE_WRITE)) {
                vm_push(vm, value_result_err(value_string("file write requires CAP_FILE_WRITE")));
                break;
            }
            Value *content = vm_pop(vm);
            Value *path = vm_pop(vm);
            if (!value_is_string(path) || !value_is_string(content)) {
                vm_set_error(vm, "file_write requires string path and content");
                return VM_ERROR_TYPE;
            }
            /* Sandbox check: validate path */
            Sandbox *sandbox = sandbox_global();
            char *resolved = sandbox_resolve_write(sandbox, path->as.string->data);
            if (!resolved) {
                vm_push(vm, value_result_err(value_string("file write denied by sandbox")));
                break;
            }
            FILE *f = fopen(resolved, "wb");
            if (!f) {
                char errmsg[256];
                snprintf(errmsg, sizeof(errmsg), "cannot open file for writing: %s", resolved);
                free(resolved);
                vm_push(vm, value_result_err(value_string(errmsg)));
            } else {
                free(resolved);
                size_t len = strlen(content->as.string->data);
                size_t written = fwrite(content->as.string->data, 1, len, f);
                fclose(f);
                if (written == len) {
                    vm_push(vm, value_result_ok(value_bool(true)));
                } else {
                    vm_push(vm, value_result_err(value_string("incomplete write")));
                }
            }
            break;
        }

        case OP_FILE_EXISTS: {
            /* Capability check: require CAP_FILE_READ */
            Block *block = (Block *)vm->block;
            if (block && !block_has_cap(block, CAP_FILE_READ)) {
                vm_push(vm, value_bool(false));
                break;
            }
            Value *path = vm_pop(vm);
            if (!value_is_string(path)) {
                vm_set_error(vm, "file path must be string");
                return VM_ERROR_TYPE;
            }
            /* Sandbox check: validate path (for reading - to check existence) */
            Sandbox *sandbox = sandbox_global();
            if (!sandbox_check_read(sandbox, path->as.string->data)) {
                /* Path not allowed by sandbox - return false (as if doesn't exist) */
                vm_push(vm, value_bool(false));
                break;
            }
            char *resolved = sandbox_resolve_read(sandbox, path->as.string->data);
            if (!resolved) {
                vm_push(vm, value_bool(false));
                break;
            }
            FILE *f = fopen(resolved, "r");
            free(resolved);
            if (f) {
                fclose(f);
                vm_push(vm, value_bool(true));
            } else {
                vm_push(vm, value_bool(false));
            }
            break;
        }

        case OP_FILE_LINES: {
            /* Capability check: require CAP_FILE_READ */
            Block *block = (Block *)vm->block;
            if (block && !block_has_cap(block, CAP_FILE_READ)) {
                vm_push(vm, value_result_err(value_string("file read requires CAP_FILE_READ")));
                break;
            }
            Value *path = vm_pop(vm);
            if (!value_is_string(path)) {
                vm_set_error(vm, "file path must be string");
                return VM_ERROR_TYPE;
            }
            /* Sandbox check: validate path */
            Sandbox *sandbox = sandbox_global();
            char *resolved = sandbox_resolve_read(sandbox, path->as.string->data);
            if (!resolved) {
                vm_push(vm, value_result_err(value_string("file read denied by sandbox")));
                break;
            }
            FILE *f = fopen(resolved, "r");
            if (!f) {
                char errmsg[256];
                snprintf(errmsg, sizeof(errmsg), "cannot open file: %s", resolved);
                free(resolved);
                vm_push(vm, value_result_err(value_string(errmsg)));
            } else {
                free(resolved);
                Value *arr = value_array();
                char line[4096];
                while (fgets(line, sizeof(line), f)) {
                    /* Remove trailing newline */
                    size_t len = strlen(line);
                    if (len > 0 && line[len-1] == '\n') {
                        line[len-1] = '\0';
                        if (len > 1 && line[len-2] == '\r') {
                            line[len-2] = '\0';
                        }
                    }
                    array_push(arr, value_string(line));
                }
                fclose(f);
                vm_push(vm, value_result_ok(arr));
            }
            break;
        }

        case OP_FILE_WRITE_BYTES: {
            /* Capability check: require CAP_FILE_WRITE */
            Block *block = (Block *)vm->block;
            if (block && !block_has_cap(block, CAP_FILE_WRITE)) {
                vm_push(vm, value_result_err(value_string("file write requires CAP_FILE_WRITE")));
                break;
            }
            Value *bytes_val = vm_pop(vm);
            Value *path = vm_pop(vm);

            if (!value_is_string(path)) {
                vm_set_error(vm, "file path must be string");
                return VM_ERROR_TYPE;
            }
            if (!value_is_array(bytes_val)) {
                vm_set_error(vm, "fs.write_bytes requires array of integers");
                return VM_ERROR_TYPE;
            }

            Sandbox *sandbox = sandbox_global();
            char *resolved = sandbox_resolve_write(sandbox, path->as.string->data);
            if (!resolved) {
                vm_push(vm, value_result_err(value_string("file write denied by sandbox")));
                break;
            }

            size_t len = array_length(bytes_val);
            uint8_t *buffer = malloc(len);
            if (!buffer) {
                free(resolved);
                vm_push(vm, value_result_err(value_string("out of memory")));
                break;
            }

            bool valid = true;
            for (size_t i = 0; i < len && valid; i++) {
                Value *elem = array_get(bytes_val, i);
                if (!value_is_int(elem)) {
                    free(buffer); free(resolved);
                    vm_push(vm, value_result_err(value_string("array must contain only integers")));
                    valid = false;
                } else {
                    int64_t val = elem->as.integer;
                    if (val < 0 || val > 255) {
                        free(buffer); free(resolved);
                        vm_push(vm, value_result_err(value_string("byte value out of range (0-255)")));
                        valid = false;
                    } else {
                        buffer[i] = (uint8_t)val;
                    }
                }
            }

            if (valid) {
                FILE *f = fopen(resolved, "wb");
                if (!f) {
                    free(buffer); free(resolved);
                    vm_push(vm, value_result_err(value_string("cannot open file for writing")));
                } else {
                    size_t written = fwrite(buffer, 1, len, f);
                    fclose(f);
                    free(buffer); free(resolved);
                    if (written == len) {
                        vm_push(vm, value_result_ok(value_bool(true)));
                    } else {
                        vm_push(vm, value_result_err(value_string("incomplete write")));
                    }
                }
            }
            break;
        }

        /* HTTP operations removed - net module not available */
        case OP_HTTP_GET:
        case OP_HTTP_POST:
        case OP_HTTP_PUT:
        case OP_HTTP_DELETE:
        case OP_HTTP_PATCH:
        case OP_HTTP_REQUEST: {
            vm_set_error(vm, "HTTP operations not available (net module removed)");
            return VM_ERROR_NOT_IMPLEMENTED;
        }

        case OP_SHELL: {
            /* Check CAP_SHELL capability */
            if (vm->block && !block_check_cap(vm->block, CAP_SHELL)) {
                vm_push(vm, value_result_err(value_string("shell requires CAP_SHELL capability")));
                break;
            }
            Value *cmd_val = vm_pop(vm);
            if (!cmd_val || !value_is_string(cmd_val)) {
                vm_set_error(vm, "command must be string");
                return VM_ERROR_TYPE;
            }
            /*
             * Execute command using popen() which is safer than system()
             * for output capture. The command is still interpreted by shell,
             * but this is intentional for shell() - it's meant to run shell commands.
             * Applications should validate/escape user input before calling shell().
             */
            FILE *pipe = popen(cmd_val->as.string->data, "r");
            if (!pipe) {
                vm_push(vm, value_result_err(value_string("failed to execute command")));
                break;
            }

            /* Read output incrementally */
            char buffer[4096];
            size_t total_len = 0;
            size_t capacity = 4096;
            char *output = malloc(capacity);
            if (!output) {
                pclose(pipe);
                vm_push(vm, value_result_err(value_string("out of memory")));
                break;
            }
            output[0] = '\0';

            while (fgets(buffer, sizeof(buffer), pipe)) {
                size_t chunk_len = strlen(buffer);
                if (total_len + chunk_len + 1 > capacity) {
                    capacity *= 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) {
                        free(output);
                        pclose(pipe);
                        vm_push(vm, value_result_err(value_string("out of memory")));
                        break;
                    }
                    output = new_output;
                }
                memcpy(output + total_len, buffer, chunk_len);
                total_len += chunk_len;
                output[total_len] = '\0';
            }

            int status = pclose(pipe);

            /* Remove trailing newline */
            if (total_len > 0 && output[total_len - 1] == '\n') {
                output[total_len - 1] = '\0';
            }

            /* Check exit status */
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                vm_push(vm, value_result_ok(value_string(output)));
            } else {
                /* Command failed - still return output but as error */
                vm_push(vm, value_result_err(value_string(output)));
            }
            free(output);
            break;
        }

        case OP_JSON_PARSE: {
            Value *str = vm_pop(vm);
            if (!str || !value_is_string(str)) {
                vm_set_error(vm, "json_parse requires string");
                return VM_ERROR_TYPE;
            }
            /* Simple JSON parser - handles basic types */
            const char *s = str->as.string->data;
            while (*s == ' ' || *s == '\t' || *s == '\n') s++;

            if (*s == '\0') {
                vm_push(vm, value_result_err(value_string("empty JSON string")));
                break;
            }

            if (*s == '"') {
                /* String */
                s++;
                char *end = strchr(s, '"');
                if (end) {
                    size_t len = end - s;
                    char *val = malloc(len + 1);
                    if (!val) {
                        vm_push(vm, value_result_err(value_string("out of memory")));
                        break;
                    }
                    memcpy(val, s, len);
                    val[len] = '\0';
                    vm_push(vm, value_result_ok(value_string(val)));
                    free(val);
                } else {
                    vm_push(vm, value_result_err(value_string("unterminated string")));
                }
            } else if (*s == '[') {
                /* Array */
                Value *arr = value_array();
                s++;
                while (*s && *s != ']') {
                    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == ',') s++;
                    if (*s == ']') break;
                    if (*s == '"') {
                        s++;
                        char *end = strchr(s, '"');
                        if (end) {
                            size_t len = end - s;
                            char *val = malloc(len + 1);
                            if (!val) break;  /* OOM - stop parsing */
                            memcpy(val, s, len);
                            val[len] = '\0';
                            array_push(arr, value_string(val));
                            free(val);
                            s = end + 1;
                        }
                    } else if (*s == '-' || (*s >= '0' && *s <= '9')) {
                        char *endp;
                        double d = strtod(s, &endp);
                        if (strchr(s, '.') && strchr(s, '.') < endp) {
                            array_push(arr, value_float(d));
                        } else {
                            array_push(arr, value_int((int64_t)d));
                        }
                        s = endp;
                    } else if (strncmp(s, "true", 4) == 0) {
                        array_push(arr, value_bool(true));
                        s += 4;
                    } else if (strncmp(s, "false", 5) == 0) {
                        array_push(arr, value_bool(false));
                        s += 5;
                    } else if (strncmp(s, "null", 4) == 0) {
                        array_push(arr, value_nil());
                        s += 4;
                    } else {
                        s++;
                    }
                }
                vm_push(vm, value_result_ok(arr));
            } else if (*s == '{') {
                /* Object/Map */
                Value *map = value_map();
                s++;
                while (*s && *s != '}') {
                    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == ',') s++;
                    if (*s == '}') break;
                    if (*s == '"') {
                        s++;
                        char *end = strchr(s, '"');
                        if (end) {
                            size_t keylen = end - s;
                            char *key = malloc(keylen + 1);
                            if (!key) break;  /* OOM - stop parsing */
                            memcpy(key, s, keylen);
                            key[keylen] = '\0';
                            s = end + 1;
                            while (*s == ' ' || *s == ':') s++;

                            Value *val = value_nil();
                            if (*s == '"') {
                                s++;
                                end = strchr(s, '"');
                                if (end) {
                                    size_t vallen = end - s;
                                    char *v = malloc(vallen + 1);
                                    if (v) {
                                        memcpy(v, s, vallen);
                                        v[vallen] = '\0';
                                        val = value_string(v);
                                        free(v);
                                    }
                                    s = end + 1;
                                }
                            } else if (*s == '-' || (*s >= '0' && *s <= '9')) {
                                char *endp;
                                double d = strtod(s, &endp);
                                if (strchr(s, '.') && strchr(s, '.') < endp) {
                                    val = value_float(d);
                                } else {
                                    val = value_int((int64_t)d);
                                }
                                s = endp;
                            } else if (strncmp(s, "true", 4) == 0) {
                                val = value_bool(true);
                                s += 4;
                            } else if (strncmp(s, "false", 5) == 0) {
                                val = value_bool(false);
                                s += 5;
                            } else if (strncmp(s, "null", 4) == 0) {
                                val = value_nil();
                                s += 4;
                            }
                            map_set(map, key, val);
                            free(key);
                        }
                    } else {
                        s++;
                    }
                }
                vm_push(vm, value_result_ok(map));
            } else if (*s == '-' || (*s >= '0' && *s <= '9')) {
                char *endp;
                double d = strtod(s, &endp);
                if (strchr(s, '.')) {
                    vm_push(vm, value_result_ok(value_float(d)));
                } else {
                    vm_push(vm, value_result_ok(value_int((int64_t)d)));
                }
            } else if (strncmp(s, "true", 4) == 0) {
                vm_push(vm, value_result_ok(value_bool(true)));
            } else if (strncmp(s, "false", 5) == 0) {
                vm_push(vm, value_result_ok(value_bool(false)));
            } else if (strncmp(s, "null", 4) == 0) {
                vm_push(vm, value_result_ok(value_nil()));
            } else {
                vm_push(vm, value_result_err(value_string("invalid JSON")));
            }
            break;
        }

        case OP_JSON_ENCODE: {
            Value *v = vm_pop(vm);
            char *json = value_repr(v);
            vm_push(vm, value_string(json));
            free(json);
            break;
        }

        case OP_ENV_GET: {
            Block *block = (Block *)vm->block;
            if (block && !block_has_cap(block, CAP_ENV)) {
                vm_set_error(vm, "env_get requires CAP_ENV capability");
                return VM_ERROR_CAPABILITY;
            }
            Value *name = vm_pop(vm);
            if (!name || !value_is_string(name)) {
                vm_set_error(vm, "env_get requires string");
                return VM_ERROR_TYPE;
            }
            const char *val = getenv(name->as.string->data);
            vm_push(vm, val ? value_string(val) : value_nil());
            break;
        }

        case OP_ENV_SET: {
            Block *block = (Block *)vm->block;
            if (block && !block_has_cap(block, CAP_ENV)) {
                vm_set_error(vm, "env_set requires CAP_ENV capability");
                return VM_ERROR_CAPABILITY;
            }
            Value *val = vm_pop(vm);
            Value *name = vm_pop(vm);
            if (!name || !value_is_string(name) || !val || !value_is_string(val)) {
                vm_set_error(vm, "env_set requires two strings");
                return VM_ERROR_TYPE;
            }
            setenv(name->as.string->data, val->as.string->data, 1);
            vm_push(vm, value_nil());
            break;
        }

        case OP_SLEEP: {
            Value *ms = vm_pop(vm);
            if (!ms || !value_is_int(ms)) {
                vm_set_error(vm, "sleep requires integer milliseconds");
                return VM_ERROR_TYPE;
            }
            usleep((useconds_t)(ms->as.integer * 1000));
            vm_push(vm, value_nil());
            break;
        }

        case OP_TIME: {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            int64_t ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
            vm_push(vm, value_int(ms));
            break;
        }

        case OP_TIME_FORMAT: {
            Value *fmt = vm_pop(vm);
            Value *ts = vm_pop(vm);
            if (!ts || !value_is_int(ts) || !fmt || !value_is_string(fmt)) {
                vm_set_error(vm, "time_format requires timestamp and format string");
                return VM_ERROR_TYPE;
            }
            time_t t = ts->as.integer / 1000;
            struct tm *tm = localtime(&t);
            char buf[256];
            /* Format string comes from user code - intentionally non-literal */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
            strftime(buf, sizeof(buf), fmt->as.string->data, tm);
#pragma GCC diagnostic pop
            vm_push(vm, value_string(buf));
            break;
        }

        case OP_RANDOM: {
            /* Use secure xorshift64 PRNG seeded from /dev/urandom */
            uint64_t rnd = vm_xorshift64(&vm->rng_state);
            /* Convert to double in [0.0, 1.0) range */
            double r = (double)(rnd >> 11) / (double)(1ULL << 53);
            vm_push(vm, value_float(r));
            break;
        }

        case OP_RANDOM_INT: {
            Value *max = vm_pop(vm);
            Value *min = vm_pop(vm);
            if (!min || !max || !value_is_int(min) || !value_is_int(max)) {
                vm_set_error(vm, "random_int requires two integers");
                return VM_ERROR_TYPE;
            }
            int64_t range = max->as.integer - min->as.integer + 1;
            /* Use secure xorshift64 PRNG */
            uint64_t rnd = vm_xorshift64(&vm->rng_state);
            int64_t r = min->as.integer + (int64_t)(rnd % (uint64_t)range);
            vm_push(vm, value_int(r));
            break;
        }

        case OP_SPLIT: {
            Value *delim = vm_pop(vm);
            Value *str = vm_pop(vm);
            /* Handle nil as empty array */
            if (!str || value_is_nil(str)) {
                vm_push(vm, value_array());
                break;
            }
            if (!delim || !value_is_string(str) || !value_is_string(delim)) {
                vm_set_error(vm, "split requires two strings");
                return VM_ERROR_TYPE;
            }
            Value *arr = value_array();
            char *s = strdup(str->as.string->data);
            const char *d = delim->as.string->data;
            size_t dlen = strlen(d);
            char *p = s;
            char *found;
            while ((found = strstr(p, d)) != NULL) {
                *found = '\0';
                array_push(arr, value_string(p));
                p = found + dlen;
            }
            array_push(arr, value_string(p));
            free(s);
            vm_push(vm, arr);
            break;
        }

        case OP_JOIN: {
            Value *delim = vm_pop(vm);
            Value *arr = vm_pop(vm);
            if (!arr || !delim || !value_is_array(arr) || !value_is_string(delim)) {
                vm_set_error(vm, "join requires array and string");
                return VM_ERROR_TYPE;
            }
            /* Calculate total with overflow checking */
            size_t total = 0;
            size_t delim_len = strlen(delim->as.string->data);
            for (size_t i = 0; i < arr->as.array->length; i++) {
                Value *v = arr->as.array->items[i];
                if (v && value_is_string(v)) {
                    size_t item_len = strlen(v->as.string->data);
                    if (item_len > SIZE_MAX - total) {
                        vm_set_error(vm, "string size overflow");
                        return VM_ERROR_RUNTIME;
                    }
                    total += item_len;
                }
                if (i > 0) {
                    if (delim_len > SIZE_MAX - total) {
                        vm_set_error(vm, "string size overflow");
                        return VM_ERROR_RUNTIME;
                    }
                    total += delim_len;
                }
            }
            char *result = malloc(total + 1);
            if (!result) {
                vm_set_error(vm, "out of memory");
                return VM_ERROR_RUNTIME;
            }
            /* Use memcpy with offset tracking instead of strcat */
            size_t offset = 0;
            for (size_t i = 0; i < arr->as.array->length; i++) {
                if (i > 0) {
                    memcpy(result + offset, delim->as.string->data, delim_len);
                    offset += delim_len;
                }
                Value *v = arr->as.array->items[i];
                if (v && value_is_string(v)) {
                    size_t slen = strlen(v->as.string->data);
                    memcpy(result + offset, v->as.string->data, slen);
                    offset += slen;
                }
            }
            result[offset] = '\0';
            vm_push(vm, value_string(result));
            free(result);
            break;
        }

        case OP_TRIM: {
            Value *str = vm_pop(vm);
            /* Handle nil as empty string */
            if (!str || value_is_nil(str)) {
                vm_push(vm, value_string(""));
                break;
            }
            if (!value_is_string(str)) {
                vm_set_error(vm, "trim requires string");
                return VM_ERROR_TYPE;
            }
            const char *s = str->as.string->data;
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            size_t len = strlen(s);
            while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                               s[len-1] == '\n' || s[len-1] == '\r')) len--;
            char *result = malloc(len + 1);
            if (!result) {
                vm_set_error(vm, "out of memory");
                return VM_ERROR_RUNTIME;
            }
            memcpy(result, s, len);
            result[len] = '\0';
            vm_push(vm, value_string(result));
            free(result);
            break;
        }

        case OP_REPLACE: {
            Value *replacement = vm_pop(vm);
            Value *search = vm_pop(vm);
            Value *str = vm_pop(vm);
            /* Handle nil as empty string */
            if (!str || value_is_nil(str)) {
                vm_push(vm, value_string(""));
                break;
            }
            if (!search || !replacement ||
                !value_is_string(str) || !value_is_string(search) || !value_is_string(replacement)) {
                vm_set_error(vm, "replace requires three strings");
                return VM_ERROR_TYPE;
            }
            const char *s = str->as.string->data;
            const char *find = search->as.string->data;
            const char *repl = replacement->as.string->data;
            size_t findlen = strlen(find);
            size_t repllen = strlen(repl);
            size_t slen = strlen(s);

            /* Count occurrences */
            size_t count = 0;
            const char *p = s;
            while ((p = strstr(p, find)) != NULL) {
                count++;
                p += findlen;
            }

            /* Calculate new length safely avoiding unsigned underflow */
            size_t newlen;
            if (repllen >= findlen) {
                newlen = slen + count * (repllen - findlen);
            } else {
                size_t shrink = count * (findlen - repllen);
                newlen = (shrink > slen) ? 0 : slen - shrink;
            }
            char *result = malloc(newlen + 1);
            if (!result) {
                vm_set_error(vm, "out of memory");
                return VM_ERROR_RUNTIME;
            }
            char *r = result;
            p = s;
            const char *found;
            while ((found = strstr(p, find)) != NULL) {
                memcpy(r, p, found - p);
                r += found - p;
                memcpy(r, repl, repllen);
                r += repllen;
                p = found + findlen;
            }
            memcpy(r, p, strlen(p) + 1);  /* +1 for null terminator */
            vm_push(vm, value_string(result));
            free(result);
            break;
        }

        case OP_CONTAINS: {
            Value *needle = vm_pop(vm);
            Value *haystack = vm_pop(vm);
            if (!haystack || !needle || !value_is_string(haystack) || !value_is_string(needle)) {
                vm_set_error(vm, "contains requires two strings");
                return VM_ERROR_TYPE;
            }
            vm_push(vm, value_bool(strstr(haystack->as.string->data,
                                          needle->as.string->data) != NULL));
            break;
        }

        case OP_STARTS_WITH: {
            Value *prefix = vm_pop(vm);
            Value *str = vm_pop(vm);
            if (!str || !prefix || !value_is_string(str) || !value_is_string(prefix)) {
                vm_set_error(vm, "starts_with requires two strings");
                return VM_ERROR_TYPE;
            }
            size_t plen = strlen(prefix->as.string->data);
            vm_push(vm, value_bool(strncmp(str->as.string->data,
                                           prefix->as.string->data, plen) == 0));
            break;
        }

        case OP_ENDS_WITH: {
            Value *suffix = vm_pop(vm);
            Value *str = vm_pop(vm);
            if (!str || !suffix || !value_is_string(str) || !value_is_string(suffix)) {
                vm_set_error(vm, "ends_with requires two strings");
                return VM_ERROR_TYPE;
            }
            size_t slen = strlen(str->as.string->data);
            size_t suflen = strlen(suffix->as.string->data);
            if (suflen > slen) {
                vm_push(vm, value_bool(false));
            } else {
                vm_push(vm, value_bool(strcmp(str->as.string->data + slen - suflen,
                                              suffix->as.string->data) == 0));
            }
            break;
        }

        case OP_UPPER: {
            Value *str = vm_pop(vm);
            /* Handle nil as empty string */
            if (!str || value_is_nil(str)) {
                vm_push(vm, value_string(""));
                break;
            }
            if (!value_is_string(str)) {
                vm_set_error(vm, "upper requires string");
                return VM_ERROR_TYPE;
            }
            char *result = strdup(str->as.string->data);
            for (char *p = result; *p; p++) {
                if (*p >= 'a' && *p <= 'z') *p -= 32;
            }
            vm_push(vm, value_string(result));
            free(result);
            break;
        }

        case OP_LOWER: {
            Value *str = vm_pop(vm);
            /* Handle nil as empty string */
            if (!str || value_is_nil(str)) {
                vm_push(vm, value_string(""));
                break;
            }
            if (!value_is_string(str)) {
                vm_set_error(vm, "lower requires string");
                return VM_ERROR_TYPE;
            }
            char *result = strdup(str->as.string->data);
            for (char *p = result; *p; p++) {
                if (*p >= 'A' && *p <= 'Z') *p += 32;
            }
            vm_push(vm, value_string(result));
            free(result);
            break;
        }

        case OP_CHAR_AT: {
            Value *idx = vm_pop(vm);
            Value *str = vm_pop(vm);
            if (!str || !idx || !value_is_string(str) || !value_is_int(idx)) {
                vm_set_error(vm, "char_at requires string and integer");
                return VM_ERROR_TYPE;
            }
            size_t len = strlen(str->as.string->data);
            int64_t i = idx->as.integer;
            if (i < 0 || (size_t)i >= len) {
                vm_push(vm, value_string(""));
            } else {
                char buf[2] = {str->as.string->data[i], '\0'};
                vm_push(vm, value_string(buf));
            }
            break;
        }

        case OP_INDEX_OF: {
            Value *needle = vm_pop(vm);
            Value *haystack = vm_pop(vm);
            if (!haystack || !needle || !value_is_string(haystack) || !value_is_string(needle)) {
                vm_set_error(vm, "index_of requires two strings");
                return VM_ERROR_TYPE;
            }
            const char *found = strstr(haystack->as.string->data, needle->as.string->data);
            if (found) {
                vm_push(vm, value_int(found - haystack->as.string->data));
            } else {
                vm_push(vm, value_int(-1));
            }
            break;
        }

        case OP_BASE64_ENCODE: {
            Value *str = vm_pop(vm);
            if (!str || !value_is_string(str)) {
                vm_set_error(vm, "base64_encode requires string");
                return VM_ERROR_TYPE;
            }
            static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const unsigned char *in = (unsigned char *)str->as.string->data;
            size_t inlen = strlen(str->as.string->data);
            size_t outlen = ((inlen + 2) / 3) * 4;
            char *out = malloc(outlen + 1);
            if (!out) {
                vm_set_error(vm, "out of memory");
                return VM_ERROR_RUNTIME;
            }
            char *p = out;
            for (size_t i = 0; i < inlen; i += 3) {
                unsigned int n = (unsigned int)in[i] << 16;
                if (i + 1 < inlen) n |= (unsigned int)in[i + 1] << 8;
                if (i + 2 < inlen) n |= in[i + 2];
                *p++ = b64[(n >> 18) & 63];
                *p++ = b64[(n >> 12) & 63];
                *p++ = (i + 1 < inlen) ? b64[(n >> 6) & 63] : '=';
                *p++ = (i + 2 < inlen) ? b64[n & 63] : '=';
            }
            *p = '\0';
            vm_push(vm, value_string(out));
            free(out);
            break;
        }

        case OP_BASE64_DECODE: {
            Value *str = vm_pop(vm);
            if (!str || !value_is_string(str)) {
                vm_set_error(vm, "base64_decode requires string");
                return VM_ERROR_TYPE;
            }
            static const int b64d[] = {
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
                52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
                -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
                15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
                -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
                41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
            };
            const char *in = str->as.string->data;
            size_t inlen = strlen(in);
            size_t outlen = inlen / 4 * 3;
            char *out = malloc(outlen + 1);
            if (!out) {
                vm_set_error(vm, "out of memory");
                return VM_ERROR_RUNTIME;
            }
            char *p = out;
            for (size_t i = 0; i < inlen; i += 4) {
                int n = b64d[(unsigned char)in[i]] << 18;
                n |= b64d[(unsigned char)in[i + 1]] << 12;
                n |= b64d[(unsigned char)in[i + 2]] << 6;
                n |= b64d[(unsigned char)in[i + 3]];
                *p++ = (n >> 16) & 255;
                if (in[i + 2] != '=') *p++ = (n >> 8) & 255;
                if (in[i + 3] != '=') *p++ = n & 255;
            }
            *p = '\0';
            vm_push(vm, value_string(out));
            free(out);
            break;
        }

        case OP_READ_STDIN: {
            char buf[65536];
            size_t total = 0;
            size_t nread;
            while ((nread = fread(buf + total, 1, sizeof(buf) - total - 1, stdin)) > 0) {
                total += nread;
                if (total >= sizeof(buf) - 1) break;
            }
            buf[total] = '\0';
            vm_push(vm, value_string(buf));
            break;
        }

        case OP_PRINT_ERR: {
            Value *v = vm_pop(vm);
            if (v) {
                char *s = value_repr(v);
                fprintf(stderr, "%s\n", s);
                free(s);
            }
            vm_push(vm, value_nil());
            break;
        }

        case OP_FLOOR: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            if (value_is_float(v)) {
                vm_push(vm, value_int((int64_t)floor(v->as.floating)));
            } else if (value_is_int(v)) {
                vm_push(vm, v);
            } else {
                vm_set_error(vm, "floor requires number");
                return VM_ERROR_TYPE;
            }
            break;
        }

        case OP_CEIL: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            if (value_is_float(v)) {
                vm_push(vm, value_int((int64_t)ceil(v->as.floating)));
            } else if (value_is_int(v)) {
                vm_push(vm, v);
            } else {
                vm_set_error(vm, "ceil requires number");
                return VM_ERROR_TYPE;
            }
            break;
        }

        case OP_ROUND: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            if (value_is_float(v)) {
                vm_push(vm, value_int((int64_t)round(v->as.floating)));
            } else if (value_is_int(v)) {
                vm_push(vm, v);
            } else {
                vm_set_error(vm, "round requires number");
                return VM_ERROR_TYPE;
            }
            break;
        }

        case OP_ABS: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            if (value_is_float(v)) {
                vm_push(vm, value_float(fabs(v->as.floating)));
            } else if (value_is_int(v)) {
                vm_push(vm, value_int(v->as.integer < 0 ? -v->as.integer : v->as.integer));
            } else {
                vm_set_error(vm, "abs requires number");
                return VM_ERROR_TYPE;
            }
            break;
        }

        case OP_SQRT: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            double n = value_is_float(v) ? v->as.floating : (double)v->as.integer;
            vm_push(vm, value_float(sqrt(n)));
            break;
        }

        case OP_POW: {
            Value *exp = vm_pop(vm);
            Value *base = vm_pop(vm);
            if (!base || !exp) return VM_ERROR_STACK_UNDERFLOW;
            double b = value_is_float(base) ? base->as.floating : (double)base->as.integer;
            double e = value_is_float(exp) ? exp->as.floating : (double)exp->as.integer;
            vm_push(vm, value_float(pow(b, e)));
            break;
        }

        case OP_MIN: {
            Value *b = vm_pop(vm);
            Value *a = vm_pop(vm);
            if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;
            if (value_is_int(a) && value_is_int(b)) {
                vm_push(vm, value_int(a->as.integer < b->as.integer ? a->as.integer : b->as.integer));
            } else {
                double da = value_is_float(a) ? a->as.floating : (double)a->as.integer;
                double db = value_is_float(b) ? b->as.floating : (double)b->as.integer;
                vm_push(vm, value_float(da < db ? da : db));
            }
            break;
        }

        case OP_MAX: {
            Value *b = vm_pop(vm);
            Value *a = vm_pop(vm);
            if (!a || !b) return VM_ERROR_STACK_UNDERFLOW;
            if (value_is_int(a) && value_is_int(b)) {
                vm_push(vm, value_int(a->as.integer > b->as.integer ? a->as.integer : b->as.integer));
            } else {
                double da = value_is_float(a) ? a->as.floating : (double)a->as.integer;
                double db = value_is_float(b) ? b->as.floating : (double)b->as.integer;
                vm_push(vm, value_float(da > db ? da : db));
            }
            break;
        }

        /* WebSocket operations removed - net module not available */
        case OP_WS_CONNECT:
        case OP_WS_SEND:
        case OP_WS_RECV:
        case OP_WS_CLOSE: {
            vm_set_error(vm, "WebSocket operations not available (net module removed)");
            return VM_ERROR_NOT_IMPLEMENTED;
        }

        /* HTTP Streaming operations removed - net module not available */
        case OP_HTTP_STREAM:
        case OP_STREAM_READ:
        case OP_STREAM_CLOSE: {
            vm_set_error(vm, "HTTP streaming not available (net module removed)");
            return VM_ERROR_NOT_IMPLEMENTED;
        }

        /* Process execution - using fork/exec for safety */
        case OP_EXEC: {
            /* Check CAP_EXEC capability */
            if (vm->block && !block_check_cap(vm->block, CAP_EXEC)) {
                vm_set_error(vm, "exec requires CAP_EXEC capability");
                return VM_ERROR_CAPABILITY;
            }
            Value *input = vm_pop(vm);
            Value *cmd = vm_pop(vm);
            if (!cmd || !value_is_string(cmd)) {
                vm_set_error(vm, "exec requires command string");
                return VM_ERROR_TYPE;
            }
            /*
             * Use popen for simple execution. While still shell-based,
             * this is the intended behavior for exec() - running shell commands.
             * For truly safe execution, use execve with argument array,
             * but that would change the API semantics.
             */
            int stdin_pipe[2];
            int stdout_pipe[2];

            if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
                vm_push(vm, value_nil());
                break;
            }

            pid_t pid = fork();
            if (pid == -1) {
                close(stdin_pipe[0]); close(stdin_pipe[1]);
                close(stdout_pipe[0]); close(stdout_pipe[1]);
                vm_push(vm, value_nil());
                break;
            }

            if (pid == 0) {
                /* Child process */
                close(stdin_pipe[1]);  /* Close write end of stdin pipe */
                close(stdout_pipe[0]); /* Close read end of stdout pipe */
                dup2(stdin_pipe[0], STDIN_FILENO);
                dup2(stdout_pipe[1], STDOUT_FILENO);
                dup2(stdout_pipe[1], STDERR_FILENO);
                close(stdin_pipe[0]);
                close(stdout_pipe[1]);
                /* Execute via shell (intentional - exec() is meant for shell commands) */
                execl("/bin/sh", "sh", "-c", cmd->as.string->data, (char *)NULL);
                _exit(127);
            }

            /* Parent process */
            close(stdin_pipe[0]);  /* Close read end of stdin pipe */
            close(stdout_pipe[1]); /* Close write end of stdout pipe */

            /* Write input if provided */
            if (input && value_is_string(input)) {
                ssize_t written = write(stdin_pipe[1], input->as.string->data,
                                        strlen(input->as.string->data));
                (void)written;
            }
            close(stdin_pipe[1]); /* Signal EOF to child */

            /* Read output */
            char buffer[4096];
            size_t total_len = 0;
            size_t capacity = 4096;
            char *output = malloc(capacity);
            if (!output) {
                close(stdout_pipe[0]);
                waitpid(pid, NULL, 0);
                vm_push(vm, value_nil());
                break;
            }
            output[0] = '\0';

            ssize_t nread;
            while ((nread = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                if (total_len + (size_t)nread + 1 > capacity) {
                    capacity *= 2;
                    char *new_output = realloc(output, capacity);
                    if (!new_output) {
                        free(output);
                        close(stdout_pipe[0]);
                        waitpid(pid, NULL, 0);
                        vm_push(vm, value_nil());
                        break;
                    }
                    output = new_output;
                }
                memcpy(output + total_len, buffer, (size_t)nread);
                total_len += (size_t)nread;
                output[total_len] = '\0';
            }

            close(stdout_pipe[0]);
            waitpid(pid, NULL, 0);

            /* Remove trailing newline */
            if (total_len > 0 && output[total_len - 1] == '\n') {
                output[total_len - 1] = '\0';
            }

            vm_push(vm, value_string(output));
            free(output);
            break;
        }

        case OP_EXEC_ASYNC: {
            /* Check CAP_EXEC capability */
            if (vm->block && !block_check_cap(vm->block, CAP_EXEC)) {
                vm_set_error(vm, "exec_async requires CAP_EXEC capability");
                return VM_ERROR_CAPABILITY;
            }
            Value *cmd = vm_pop(vm);
            if (!cmd || !value_is_string(cmd)) {
                vm_set_error(vm, "exec_async requires command string");
                return VM_ERROR_TYPE;
            }
            /*
             * Create pipes for async process communication.
             * This is a safer approach than using temp files with system().
             */
            int stdin_pipe[2];
            int stdout_pipe[2];

            if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
                vm_push(vm, value_nil());
                break;
            }

            pid_t pid = fork();
            if (pid == -1) {
                close(stdin_pipe[0]); close(stdin_pipe[1]);
                close(stdout_pipe[0]); close(stdout_pipe[1]);
                vm_push(vm, value_nil());
                break;
            }

            if (pid == 0) {
                /* Child process */
                close(stdin_pipe[1]);
                close(stdout_pipe[0]);
                dup2(stdin_pipe[0], STDIN_FILENO);
                dup2(stdout_pipe[1], STDOUT_FILENO);
                dup2(stdout_pipe[1], STDERR_FILENO);
                close(stdin_pipe[0]);
                close(stdout_pipe[1]);
                execl("/bin/sh", "sh", "-c", cmd->as.string->data, (char *)NULL);
                _exit(127);
            }

            /* Parent process - close unused pipe ends */
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);

            Value *handle = value_map();
            map_set(handle, "cmd", cmd);
            map_set(handle, "_pid", value_int((int64_t)pid));
            map_set(handle, "_stdin_fd", value_int((int64_t)stdin_pipe[1]));
            map_set(handle, "_stdout_fd", value_int((int64_t)stdout_pipe[0]));
            map_set(handle, "running", value_bool(true));
            vm_push(vm, handle);
            break;
        }

        case OP_PROC_WRITE: {
            Value *data = vm_pop(vm);
            Value *handle = vm_pop(vm);
            if (!handle || !value_is_map(handle) || !data || !value_is_string(data)) {
                vm_set_error(vm, "proc_write requires handle and string");
                return VM_ERROR_TYPE;
            }
            Value *stdin_fd_val = map_get(handle, "_stdin_fd");
            if (!stdin_fd_val || !value_is_int(stdin_fd_val)) {
                vm_push(vm, value_bool(false));
                break;
            }
            int stdin_fd = (int)stdin_fd_val->as.integer;
            if (stdin_fd < 0) {
                vm_push(vm, value_bool(false));
                break;
            }
            const char *str = data->as.string->data;
            size_t len = strlen(str);
            ssize_t written = write(stdin_fd, str, len);
            /* Also write newline */
            if (written > 0) {
                written = write(stdin_fd, "\n", 1);
            }
            vm_push(vm, value_bool(written > 0));
            break;
        }

        case OP_PROC_READ: {
            Value *handle = vm_pop(vm);
            if (!handle || !value_is_map(handle)) {
                vm_set_error(vm, "proc_read requires handle");
                return VM_ERROR_TYPE;
            }
            Value *stdout_fd_val = map_get(handle, "_stdout_fd");
            if (!stdout_fd_val || !value_is_int(stdout_fd_val)) {
                vm_push(vm, value_nil());
                break;
            }
            int stdout_fd = (int)stdout_fd_val->as.integer;
            if (stdout_fd < 0) {
                vm_push(vm, value_nil());
                break;
            }
            /* Non-blocking read */
            char buffer[4096];
            ssize_t nread = read(stdout_fd, buffer, sizeof(buffer) - 1);
            if (nread > 0) {
                buffer[nread] = '\0';
                vm_push(vm, value_string(buffer));
            } else {
                vm_push(vm, value_string(""));
            }
            break;
        }

        case OP_PROC_CLOSE: {
            Value *handle = vm_pop(vm);
            if (handle && value_is_map(handle)) {
                Value *stdin_fd_val = map_get(handle, "_stdin_fd");
                Value *stdout_fd_val = map_get(handle, "_stdout_fd");
                Value *pid_val = map_get(handle, "_pid");

                if (stdin_fd_val && value_is_int(stdin_fd_val)) {
                    int fd = (int)stdin_fd_val->as.integer;
                    if (fd >= 0) close(fd);
                    map_set(handle, "_stdin_fd", value_int(-1));
                }
                if (stdout_fd_val && value_is_int(stdout_fd_val)) {
                    int fd = (int)stdout_fd_val->as.integer;
                    if (fd >= 0) close(fd);
                    map_set(handle, "_stdout_fd", value_int(-1));
                }
                if (pid_val && value_is_int(pid_val)) {
                    pid_t pid = (pid_t)pid_val->as.integer;
                    if (pid > 0) {
                        waitpid(pid, NULL, WNOHANG);
                    }
                }
                map_set(handle, "running", value_bool(false));
            }
            vm_push(vm, value_nil());
            break;
        }

        /* UUID generation */
        case OP_UUID: {
            /* Try /proc first, fall back to /dev/urandom, then secure PRNG */
            char uuid[37];
            FILE *f = fopen("/proc/sys/kernel/random/uuid", "r");
            if (f) {
                if (fgets(uuid, sizeof(uuid), f)) {
                    size_t len = strlen(uuid);
                    if (len > 0 && uuid[len-1] == '\n') uuid[len-1] = '\0';
                }
                fclose(f);
            } else {
                /* Generate UUID v4 using secure random bytes */
                uint8_t bytes[16];
                bool got_random = false;

                /* Try /dev/urandom for cryptographic randomness */
                FILE *urand = fopen("/dev/urandom", "rb");
                if (urand) {
                    got_random = (fread(bytes, 1, 16, urand) == 16);
                    fclose(urand);
                }

                /* Fallback to secure xorshift64 PRNG */
                if (!got_random) {
                    uint64_t r1 = vm_xorshift64(&vm->rng_state);
                    uint64_t r2 = vm_xorshift64(&vm->rng_state);
                    memcpy(bytes, &r1, 8);
                    memcpy(bytes + 8, &r2, 8);
                }

                /* Set UUID version 4 and variant bits */
                bytes[6] = (bytes[6] & 0x0F) | 0x40;  /* Version 4 */
                bytes[8] = (bytes[8] & 0x3F) | 0x80;  /* Variant 1 */

                snprintf(uuid, sizeof(uuid),
                    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                    bytes[0], bytes[1], bytes[2], bytes[3],
                    bytes[4], bytes[5], bytes[6], bytes[7],
                    bytes[8], bytes[9], bytes[10], bytes[11],
                    bytes[12], bytes[13], bytes[14], bytes[15]);
            }
            vm_push(vm, value_string(uuid));
            break;
        }

        /* Hashing - using fork/exec to avoid shell command injection */
        case OP_HASH_MD5: {
            Value *str = vm_pop(vm);
            if (!str || !value_is_string(str)) {
                vm_set_error(vm, "hash_md5 requires string");
                return VM_ERROR_TYPE;
            }
            /*
             * Use fork/exec with pipes to safely run md5sum.
             * Input is written to stdin, avoiding any shell interpretation.
             */
            int stdin_pipe[2];
            int stdout_pipe[2];
            if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
                vm_push(vm, value_nil());
                break;
            }

            pid_t pid = fork();
            if (pid == -1) {
                close(stdin_pipe[0]); close(stdin_pipe[1]);
                close(stdout_pipe[0]); close(stdout_pipe[1]);
                vm_push(vm, value_nil());
                break;
            }

            if (pid == 0) {
                /* Child: run md5sum reading from stdin */
                close(stdin_pipe[1]);
                close(stdout_pipe[0]);
                dup2(stdin_pipe[0], STDIN_FILENO);
                dup2(stdout_pipe[1], STDOUT_FILENO);
                close(stdin_pipe[0]);
                close(stdout_pipe[1]);
                execlp("md5sum", "md5sum", (char *)NULL);
                _exit(127);
            }

            /* Parent */
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);

            /* Write input to md5sum */
            ssize_t written = write(stdin_pipe[1], str->as.string->data,
                                    strlen(str->as.string->data));
            (void)written;
            close(stdin_pipe[1]);

            /* Read hash output */
            char result[256];
            ssize_t nread = read(stdout_pipe[0], result, sizeof(result) - 1);
            close(stdout_pipe[0]);
            waitpid(pid, NULL, 0);

            if (nread > 0) {
                result[nread] = '\0';
                /* md5sum output: "hash  -", extract just the hash */
                char *space = strchr(result, ' ');
                if (space) *space = '\0';
                /* Remove any newline */
                char *nl = strchr(result, '\n');
                if (nl) *nl = '\0';
                vm_push(vm, value_string(result));
            } else {
                vm_push(vm, value_nil());
            }
            break;
        }

        case OP_HASH_SHA256: {
            Value *str = vm_pop(vm);
            if (!str || !value_is_string(str)) {
                vm_set_error(vm, "hash_sha256 requires string");
                return VM_ERROR_TYPE;
            }
            /*
             * Use fork/exec with pipes to safely run sha256sum.
             * Input is written to stdin, avoiding any shell interpretation.
             */
            int stdin_pipe[2];
            int stdout_pipe[2];
            if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
                vm_push(vm, value_nil());
                break;
            }

            pid_t pid = fork();
            if (pid == -1) {
                close(stdin_pipe[0]); close(stdin_pipe[1]);
                close(stdout_pipe[0]); close(stdout_pipe[1]);
                vm_push(vm, value_nil());
                break;
            }

            if (pid == 0) {
                /* Child: run sha256sum reading from stdin */
                close(stdin_pipe[1]);
                close(stdout_pipe[0]);
                dup2(stdin_pipe[0], STDIN_FILENO);
                dup2(stdout_pipe[1], STDOUT_FILENO);
                close(stdin_pipe[0]);
                close(stdout_pipe[1]);
                execlp("sha256sum", "sha256sum", (char *)NULL);
                _exit(127);
            }

            /* Parent */
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);

            /* Write input to sha256sum */
            ssize_t written = write(stdin_pipe[1], str->as.string->data,
                                    strlen(str->as.string->data));
            (void)written;
            close(stdin_pipe[1]);

            /* Read hash output */
            char result[256];
            ssize_t nread = read(stdout_pipe[0], result, sizeof(result) - 1);
            close(stdout_pipe[0]);
            waitpid(pid, NULL, 0);

            if (nread > 0) {
                result[nread] = '\0';
                /* sha256sum output: "hash  -", extract just the hash */
                char *space = strchr(result, ' ');
                if (space) *space = '\0';
                /* Remove any newline */
                char *nl = strchr(result, '\n');
                if (nl) *nl = '\0';
                vm_push(vm, value_string(result));
            } else {
                vm_push(vm, value_nil());
            }
            break;
        }

        case OP_PRINT: {
            Value *v = vm_pop(vm);
            if (v) {
                value_print(v);
                printf("\n");
            }
            break;
        }

        /* Result operations (error handling) */
        case OP_RESULT_OK: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            vm_push(vm, value_result_ok(v));
            break;
        }

        case OP_RESULT_ERR: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            vm_push(vm, value_result_err(v));
            break;
        }

        case OP_RESULT_IS_OK: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            bool is_ok = value_result_is_ok(v);
            vm_push(vm, value_bool(is_ok));
            break;
        }

        case OP_RESULT_IS_ERR: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            bool is_err = value_result_is_err(v);
            vm_push(vm, value_bool(is_err));
            break;
        }

        case OP_RESULT_UNWRAP: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            /* Handle both Result and Option types */
            if (value_is_option(v)) {
                Value *inner = value_option_unwrap(v);
                if (!inner) {
                    vm_set_error(vm, "unwrap on None value");
                    return VM_ERROR_RUNTIME;
                }
                vm_push(vm, inner);
            } else if (value_is_result(v)) {
                Value *inner = value_result_unwrap(v);
                if (!inner) {
                    /* It was an Err, get the error value instead */
                    inner = value_result_unwrap_err(v);
                    if (inner) {
                        vm_push(vm, inner);
                    } else {
                        vm_push(vm, value_nil());
                    }
                } else {
                    vm_push(vm, inner);
                }
            } else {
                vm_set_error(vm, "unwrap on non-Result/Option value");
                return VM_ERROR_TYPE;
            }
            break;
        }

        case OP_RESULT_UNWRAP_OR: {
            Value *default_val = vm_pop(vm);
            Value *result = vm_pop(vm);
            if (!result || !default_val) return VM_ERROR_STACK_UNDERFLOW;
            /* Handle both Result and Option types */
            Value *unwrapped;
            if (value_is_option(result)) {
                unwrapped = value_option_unwrap_or(result, default_val);
            } else {
                unwrapped = value_result_unwrap_or(result, default_val);
            }
            vm_push(vm, unwrapped);
            break;
        }

        case OP_RESULT_MATCH: {
            /* This opcode isn't actually used - match is compiled to jumps */
            vm_set_error(vm, "OP_RESULT_MATCH not implemented");
            return VM_ERROR_RUNTIME;
        }

        /* Option operations */
        case OP_SOME: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            vm_push(vm, value_some(v));
            break;
        }

        case OP_NONE: {
            vm_push(vm, value_none());
            break;
        }

        case OP_IS_SOME: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            bool is_some = value_option_is_some(v);
            vm_push(vm, value_bool(is_some));
            break;
        }

        case OP_IS_NONE: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            bool is_none = value_option_is_none(v);
            vm_push(vm, value_bool(is_none));
            break;
        }

        case OP_UNWRAP_OPTION: {
            Value *v = vm_pop(vm);
            if (!v) return VM_ERROR_STACK_UNDERFLOW;
            if (!value_is_option(v)) {
                vm_set_error(vm, "unwrap on non-Option value");
                return VM_ERROR_TYPE;
            }
            Value *inner = value_option_unwrap(v);
            if (!inner) {
                vm_set_error(vm, "unwrap on None value");
                return VM_ERROR_RUNTIME;
            }
            vm_push(vm, inner);
            break;
        }

        case OP_UNWRAP_OPTION_OR: {
            Value *default_val = vm_pop(vm);
            Value *option = vm_pop(vm);
            if (!option || !default_val) return VM_ERROR_STACK_UNDERFLOW;
            Value *unwrapped = value_option_unwrap_or(option, default_val);
            vm_push(vm, unwrapped);
            break;
        }

        /* Struct operations */
        case OP_STRUCT_NEW: {
            uint16_t type_name_idx = read_short(frame);
            uint8_t field_count = read_byte(frame);
            const char *type_name = bytecode_get_string(vm->code, type_name_idx);
            if (!type_name) {
                vm_set_error(vm, "invalid struct type name index");
                return VM_ERROR_RUNTIME;
            }
            Value *s = value_struct_new(type_name, field_count);
            /* Pop field values from stack (in reverse order) */
            for (int i = field_count - 1; i >= 0; i--) {
                uint16_t field_name_idx = read_short(frame);
                const char *field_name = bytecode_get_string(vm->code, field_name_idx);
                Value *field_val = vm_pop(vm);
                if (!field_val) return VM_ERROR_STACK_UNDERFLOW;
                value_struct_set_field(s, i, field_name, field_val);
            }
            vm_push(vm, s);
            break;
        }

        case OP_STRUCT_GET: {
            uint16_t field_name_idx = read_short(frame);
            const char *field_name = bytecode_get_string(vm->code, field_name_idx);
            Value *s = vm_pop(vm);
            if (!s) return VM_ERROR_STACK_UNDERFLOW;
            if (!value_is_struct(s)) {
                vm_set_error(vm, "field access on non-struct value");
                return VM_ERROR_TYPE;
            }
            Value *field = value_struct_get_field(s, field_name);
            if (!field) {
                vm_set_error(vm, "unknown field");
                return VM_ERROR_RUNTIME;
            }
            vm_push(vm, field);
            break;
        }

        case OP_STRUCT_SET: {
            uint16_t field_name_idx = read_short(frame);
            const char *field_name = bytecode_get_string(vm->code, field_name_idx);
            Value *new_val = vm_pop(vm);
            Value *s = vm_pop(vm);
            if (!s || !new_val) return VM_ERROR_STACK_UNDERFLOW;
            if (!value_is_struct(s)) {
                vm_set_error(vm, "field assignment on non-struct value");
                return VM_ERROR_TYPE;
            }
            /* Find field by name and set */
            StructInstance *si = s->as.struct_val;
            bool found = false;
            for (size_t i = 0; i < si->field_count; i++) {
                if (strcmp(si->field_names[i], field_name) == 0) {
                    si->fields[i] = new_val;
                    found = true;
                    break;
                }
            }
            if (!found) {
                vm_set_error(vm, "unknown field in assignment");
                return VM_ERROR_RUNTIME;
            }
            vm_push(vm, s);  /* Push struct back for chaining */
            break;
        }

        case OP_STRUCT_GET_INDEX: {
            uint8_t index = read_byte(frame);
            Value *s = vm_pop(vm);
            if (!s) return VM_ERROR_STACK_UNDERFLOW;
            if (!value_is_struct(s)) {
                vm_set_error(vm, "indexed field access on non-struct value");
                return VM_ERROR_TYPE;
            }
            Value *field = value_struct_get_field_index(s, index);
            if (!field) {
                vm_set_error(vm, "field index out of bounds");
                return VM_ERROR_RUNTIME;
            }
            vm_push(vm, field);
            break;
        }

        /* Enum operations */
        case OP_ENUM_NEW: {
            uint16_t type_idx = read_short(frame);
            uint16_t variant_idx = read_short(frame);
            uint8_t has_payload = read_byte(frame);
            const char *type_name = bytecode_get_string(vm->code, type_idx);
            const char *variant_name = bytecode_get_string(vm->code, variant_idx);
            if (!type_name || !variant_name) {
                vm_set_error(vm, "invalid enum type/variant index");
                return VM_ERROR_RUNTIME;
            }
            Value *e;
            if (has_payload) {
                Value *payload = vm_pop(vm);
                if (!payload) return VM_ERROR_STACK_UNDERFLOW;
                e = value_enum_with_payload(type_name, variant_name, payload);
            } else {
                e = value_enum_unit(type_name, variant_name);
            }
            vm_push(vm, e);
            break;
        }

        case OP_ENUM_IS: {
            uint16_t variant_idx = read_short(frame);
            const char *variant_name = bytecode_get_string(vm->code, variant_idx);
            Value *e = vm_pop(vm);
            if (!e) return VM_ERROR_STACK_UNDERFLOW;
            if (!value_is_enum(e)) {
                vm_push(vm, value_bool(false));
            } else {
                bool matches = value_enum_is_variant(e, variant_name);
                vm_push(vm, value_bool(matches));
            }
            break;
        }

        case OP_ENUM_PAYLOAD: {
            Value *e = vm_pop(vm);
            if (!e) return VM_ERROR_STACK_UNDERFLOW;
            if (!value_is_enum(e)) {
                vm_set_error(vm, "payload access on non-enum value");
                return VM_ERROR_TYPE;
            }
            Value *payload = value_enum_payload(e);
            vm_push(vm, payload ? payload : value_nil());
            break;
        }

        case OP_YIELD:
            return VM_YIELD;

        case OP_HALT:
            return VM_HALT;

        /* Process operations */
        case OP_SELF: {
            Block *block = (Block *)vm->block;
            if (!block) {
                vm_set_error(vm, "no block context");
                return VM_ERROR_RUNTIME;
            }
            vm_push(vm, value_pid(block->pid));
            break;
        }

        case OP_SEND: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_SEND)) {
                vm_set_error(vm, "send capability denied");
                return VM_ERROR_CAPABILITY;
            }

            /* Pop target pid and message value */
            Value *msg_value = vm_pop(vm);
            Value *pid_value = vm_pop(vm);
            if (!msg_value || !pid_value) return VM_ERROR_STACK_UNDERFLOW;

            if (pid_value->type != VAL_PID) {
                vm_set_error(vm, "send target must be pid");
                return VM_ERROR_TYPE;
            }

            Pid target_pid = pid_value->as.pid;
            Block *target = scheduler_get_block(sched, target_pid);

            if (!target || !block_is_alive(target)) {
                vm_set_error(vm, "send to dead or invalid block");
                return VM_ERROR_SEND_FAILED;
            }

            /* Send message (deep copy happens inside) */
            if (!block_send(target, block->pid, msg_value)) {
                vm_set_error(vm, "mailbox full or send failed");
                return VM_ERROR_SEND_FAILED;
            }

            /* Update sender counters */
            block->counters.messages_sent++;

            /* Wake target if waiting */
            if (target->state == BLOCK_WAITING) {
                target->state = BLOCK_RUNNABLE;
                scheduler_enqueue(sched, target);
            }

            /* Push nil as result */
            vm_push(vm, value_nil());
            break;
        }

        case OP_RECEIVE: {
            Block *block = (Block *)vm->block;
            if (!block) {
                vm_set_error(vm, "no block context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_RECEIVE)) {
                vm_set_error(vm, "receive capability denied");
                return VM_ERROR_CAPABILITY;
            }

            /* Check mailbox */
            Message *msg = block_receive(block);
            if (msg) {
                /* Create a result map with sender and value */
                Value *result = value_map();
                map_set(result, "sender", value_pid(msg->sender));
                map_set(result, "value", msg->value);

                /* Don't free the value since it's now owned by the map */
                msg->value = NULL;
                message_free(msg);

                vm_push(vm, result);
            } else {
                /* No message available, block should wait.
                 * Back up IP so we retry this instruction when resumed. */
                frame->ip--;
                block->state = BLOCK_WAITING;
                return VM_WAITING;
            }
            break;
        }

        case OP_SPAWN: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context for spawn");
                return VM_ERROR_RUNTIME;
            }

            /* Check spawn capability */
            if (!block_has_cap(block, CAP_SPAWN)) {
                vm_set_error(vm, "spawn capability denied");
                return VM_ERROR_CAPABILITY;
            }

            /* Pop function to spawn */
            Value *func_val = vm_pop(vm);
            if (!func_val) return VM_ERROR_STACK_UNDERFLOW;

            Function *fn = NULL;
            if (func_val->type == VAL_FUNCTION) {
                fn = func_val->as.function;
            } else if (func_val->type == VAL_CLOSURE) {
                fn = closure_function(func_val);
            } else {
                vm_set_error(vm, "spawn requires function");
                return VM_ERROR_TYPE;
            }

            if (!fn) {
                vm_set_error(vm, "invalid function for spawn");
                return VM_ERROR_TYPE;
            }

            /* Create new bytecode for spawned block with just this function */
            Bytecode *spawn_code = bytecode_new();

            /* Copy the function chunk as the main chunk */
            Chunk *fn_chunk = vm->code->functions[fn->code_offset];

            /* Copy bytecode */
            for (size_t i = 0; i < fn_chunk->code_size; i++) {
                chunk_write_byte(spawn_code->main, fn_chunk->code[i],
                               fn_chunk->lines[i]);
            }

            /* Copy constants */
            for (size_t i = 0; i < fn_chunk->constants_size; i++) {
                chunk_add_constant(spawn_code->main,
                                 value_copy(fn_chunk->constants[i]));
            }

            /* Copy functions from parent bytecode */
            for (size_t i = 0; i < vm->code->functions_count; i++) {
                Chunk *src = vm->code->functions[i];
                Chunk *dst = chunk_new();
                for (size_t j = 0; j < src->code_size; j++) {
                    chunk_write_byte(dst, src->code[j], src->lines[j]);
                }
                for (size_t j = 0; j < src->constants_size; j++) {
                    chunk_add_constant(dst, value_copy(src->constants[j]));
                }
                bytecode_add_function(spawn_code, dst);
            }

            /* Copy string table */
            for (size_t i = 0; i < vm->code->strings_count; i++) {
                bytecode_add_string(spawn_code, vm->code->strings[i]);
            }

            /* Spawn the new block */
            char spawn_name[64];
            snprintf(spawn_name, sizeof(spawn_name), "spawn_%lu",
                    (unsigned long)sched->next_pid);

            /* Inherit parent's capabilities minus spawn (prevent fork bomb) */
            CapabilitySet child_caps = block->capabilities & ~CAP_SPAWN;

            Pid child_pid = scheduler_spawn_ex(sched, spawn_code, spawn_name,
                                               child_caps, &block->limits);

            if (child_pid == PID_INVALID) {
                bytecode_free(spawn_code);
                vm_set_error(vm, "failed to spawn block");
                return VM_ERROR_RUNTIME;
            }

            /* Link child to parent */
            Block *child = scheduler_get_block(sched, child_pid);
            if (child) {
                child->parent = block->pid;
            }

            /* Push child PID */
            vm_push(vm, value_pid(child_pid));
            break;
        }

        /* Built-in primitives */
        case OP_INFER: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_INFER)) {
                vm_set_error(vm, "infer capability denied");
                return VM_ERROR_CAPABILITY;
            }

            PrimitivesRuntime *rt = scheduler_get_primitives(sched);
            if (!rt) {
                vm_set_error(vm, "no primitives runtime");
                return VM_ERROR_RUNTIME;
            }

            /* Pop prompt from stack */
            Value *prompt = vm_pop(vm);
            if (!prompt) return VM_ERROR_STACK_UNDERFLOW;

            /* Call inference */
            Value *result = primitives_infer(rt, block, prompt);
            if (!result) {
                vm_set_error(vm, "inference failed");
                return VM_ERROR_RUNTIME;
            }

            vm_push(vm, result);
            break;
        }

        case OP_TOOL_CALL: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            PrimitivesRuntime *rt = scheduler_get_primitives(sched);
            if (!rt) {
                vm_set_error(vm, "no primitives runtime");
                return VM_ERROR_RUNTIME;
            }

            /* Pop tool name and argument count */
            Value *arg_count_val = vm_pop(vm);
            Value *tool_name_val = vm_pop(vm);
            if (!arg_count_val || !tool_name_val) return VM_ERROR_STACK_UNDERFLOW;

            if (!value_is_string(tool_name_val) || !value_is_int(arg_count_val)) {
                vm_set_error(vm, "invalid tool call arguments");
                return VM_ERROR_TYPE;
            }

            const char *tool_name = tool_name_val->as.string->data;
            size_t arg_count = (size_t)arg_count_val->as.integer;

            /* Pop arguments */
            Value **args = NULL;
            if (arg_count > 0) {
                args = malloc(sizeof(Value *) * arg_count);
                for (size_t i = 0; i < arg_count; i++) {
                    args[arg_count - 1 - i] = vm_pop(vm);
                }
            }

            /* Call tool */
            Value *result = primitives_call_tool(rt, block, tool_name, args, arg_count);
            free(args);

            if (!result) {
                vm_set_error(vm, "tool call failed");
                return VM_ERROR_RUNTIME;
            }

            vm_push(vm, result);
            break;
        }

        case OP_LIST_TOOLS: {
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!sched) {
                /* No runtime - return empty array */
                vm_push(vm, value_array());
                break;
            }

            PrimitivesRuntime *rt = scheduler_get_primitives(sched);
            if (!rt) {
                vm_push(vm, value_array());
                break;
            }

            /* Get tools list as Value array */
            Value *tools_list = tools_list_as_value(&rt->tools);
            vm_push(vm, tools_list);
            break;
        }

        case OP_TOOL_SCHEMA: {
            Scheduler *sched = (Scheduler *)vm->scheduler;
            Value *name_val = vm_pop(vm);
            if (!name_val) return VM_ERROR_STACK_UNDERFLOW;

            if (!value_is_string(name_val)) {
                vm_set_error(vm, "tool_schema argument must be string");
                return VM_ERROR_TYPE;
            }

            if (!sched) {
                vm_push(vm, value_nil());
                break;
            }

            PrimitivesRuntime *rt = scheduler_get_primitives(sched);
            if (!rt) {
                vm_push(vm, value_nil());
                break;
            }

            /* Find tool and get schema */
            Tool *tool = tools_find(&rt->tools, name_val->as.string->data);
            if (!tool) {
                vm_push(vm, value_nil());
                break;
            }

            char *schema = tools_get_schema_json(tool);
            if (schema) {
                vm_push(vm, value_string(schema));
                free(schema);
            } else {
                vm_push(vm, value_nil());
            }
            break;
        }

        case OP_MEMORY_GET: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_MEMORY)) {
                vm_set_error(vm, "memory capability denied");
                return VM_ERROR_CAPABILITY;
            }

            PrimitivesRuntime *rt = scheduler_get_primitives(sched);
            if (!rt) {
                vm_set_error(vm, "no primitives runtime");
                return VM_ERROR_RUNTIME;
            }

            /* Pop key from stack */
            Value *key = vm_pop(vm);
            if (!key) return VM_ERROR_STACK_UNDERFLOW;

            if (!value_is_string(key)) {
                vm_set_error(vm, "memory key must be string");
                return VM_ERROR_TYPE;
            }

            /* Get value */
            Value *result = primitives_memory_get(rt, key->as.string->data);
            vm_push(vm, result ? result : value_nil());
            break;
        }

        case OP_MEMORY_SET: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_MEMORY)) {
                vm_set_error(vm, "memory capability denied");
                return VM_ERROR_CAPABILITY;
            }

            PrimitivesRuntime *rt = scheduler_get_primitives(sched);
            if (!rt) {
                vm_set_error(vm, "no primitives runtime");
                return VM_ERROR_RUNTIME;
            }

            /* Pop value and key from stack */
            Value *val = vm_pop(vm);
            Value *key = vm_pop(vm);
            if (!key || !val) return VM_ERROR_STACK_UNDERFLOW;

            if (!value_is_string(key)) {
                vm_set_error(vm, "memory key must be string");
                return VM_ERROR_TYPE;
            }

            /* Set value */
            primitives_memory_set(rt, key->as.string->data, val);
            vm_push(vm, value_nil());
            break;
        }

        /* Linking operations */
        case OP_LINK: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_LINK)) {
                vm_set_error(vm, "link capability denied");
                return VM_ERROR_CAPABILITY;
            }

            /* Pop target PID */
            Value *pid_val = vm_pop(vm);
            if (!pid_val) return VM_ERROR_STACK_UNDERFLOW;

            if (pid_val->type != VAL_PID) {
                vm_set_error(vm, "link target must be pid");
                return VM_ERROR_TYPE;
            }

            Pid target_pid = pid_val->as.pid;
            Block *target = scheduler_get_block(sched, target_pid);

            if (!target || !block_is_alive(target)) {
                vm_set_error(vm, "cannot link to dead or invalid block");
                return VM_ERROR_RUNTIME;
            }

            /* Create bidirectional link */
            block_link(block, target_pid);
            block_link(target, block->pid);

            vm_push(vm, value_bool(true));
            break;
        }

        case OP_UNLINK: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Pop target PID */
            Value *pid_val = vm_pop(vm);
            if (!pid_val) return VM_ERROR_STACK_UNDERFLOW;

            if (pid_val->type != VAL_PID) {
                vm_set_error(vm, "unlink target must be pid");
                return VM_ERROR_TYPE;
            }

            Pid target_pid = pid_val->as.pid;
            Block *target = scheduler_get_block(sched, target_pid);

            /* Remove bidirectional link */
            block_unlink(block, target_pid);
            if (target) {
                block_unlink(target, block->pid);
            }

            vm_push(vm, value_bool(true));
            break;
        }

        case OP_MONITOR: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_MONITOR)) {
                vm_set_error(vm, "monitor capability denied");
                return VM_ERROR_CAPABILITY;
            }

            /* Pop target PID */
            Value *pid_val = vm_pop(vm);
            if (!pid_val) return VM_ERROR_STACK_UNDERFLOW;

            if (pid_val->type != VAL_PID) {
                vm_set_error(vm, "monitor target must be pid");
                return VM_ERROR_TYPE;
            }

            Pid target_pid = pid_val->as.pid;
            Block *target = scheduler_get_block(sched, target_pid);

            if (!target || !block_is_alive(target)) {
                /* Target already dead - send immediate DOWN message */
                Value *down_msg = value_map();
                down_msg = map_set(down_msg, "type", value_string("down"));
                down_msg = map_set(down_msg, "pid", value_pid(target_pid));
                down_msg = map_set(down_msg, "reason", value_string("noproc"));
                down_msg = map_set(down_msg, "code", value_int(-1));
                block_send(block, target_pid, down_msg);
            } else {
                /* Set up monitoring */
                block_monitor(block, target_pid);
                block_add_monitored_by(target, block->pid);
            }

            vm_push(vm, value_bool(true));
            break;
        }

        case OP_DEMONITOR: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Pop target PID */
            Value *pid_val = vm_pop(vm);
            if (!pid_val) return VM_ERROR_STACK_UNDERFLOW;

            if (pid_val->type != VAL_PID) {
                vm_set_error(vm, "demonitor target must be pid");
                return VM_ERROR_TYPE;
            }

            Pid target_pid = pid_val->as.pid;
            Block *target = scheduler_get_block(sched, target_pid);

            /* Remove monitoring */
            block_demonitor(block, target_pid);
            if (target) {
                block_remove_monitored_by(target, block->pid);
            }

            vm_push(vm, value_bool(true));
            break;
        }

        /* Supervisor operations */
        case OP_SUP_START: {
            Block *block = (Block *)vm->block;
            if (!block) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_SUPERVISE)) {
                vm_set_error(vm, "supervise capability denied");
                return VM_ERROR_CAPABILITY;
            }

            /* Pop strategy from stack */
            Value *strategy_val = vm_pop(vm);
            if (!strategy_val) return VM_ERROR_STACK_UNDERFLOW;

            SupervisorStrategy strategy = SUP_ONE_FOR_ONE;
            if (value_is_string(strategy_val)) {
                const char *s = strategy_val->as.string->data;
                if (strcmp(s, "one_for_all") == 0) {
                    strategy = SUP_ONE_FOR_ALL;
                } else if (strcmp(s, "rest_for_one") == 0) {
                    strategy = SUP_REST_FOR_ONE;
                }
            } else if (value_is_int(strategy_val)) {
                strategy = (SupervisorStrategy)strategy_val->as.integer;
            }

            /* Initialize block as supervisor */
            if (!supervisor_init_block(block, strategy)) {
                vm_set_error(vm, "failed to initialize supervisor");
                return VM_ERROR_RUNTIME;
            }

            vm_push(vm, value_bool(true));
            break;
        }

        case OP_SUP_ADD_CHILD: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            if (!block->supervisor) {
                vm_set_error(vm, "block is not a supervisor");
                return VM_ERROR_RUNTIME;
            }

            /* Pop restart strategy, code function, and name */
            Value *restart_val = vm_pop(vm);
            Value *func_val = vm_pop(vm);
            Value *name_val = vm_pop(vm);
            if (!restart_val || !func_val || !name_val) return VM_ERROR_STACK_UNDERFLOW;

            /* Parse restart strategy */
            RestartStrategy restart = RESTART_PERMANENT;
            if (value_is_string(restart_val)) {
                const char *s = restart_val->as.string->data;
                if (strcmp(s, "transient") == 0) {
                    restart = RESTART_TRANSIENT;
                } else if (strcmp(s, "temporary") == 0) {
                    restart = RESTART_TEMPORARY;
                }
            } else if (value_is_int(restart_val)) {
                restart = (RestartStrategy)restart_val->as.integer;
            }

            /* Get child name */
            const char *child_name = NULL;
            if (value_is_string(name_val)) {
                child_name = name_val->as.string->data;
            }

            /* Get function to spawn */
            Function *fn = NULL;
            if (func_val->type == VAL_FUNCTION) {
                fn = func_val->as.function;
            } else if (func_val->type == VAL_CLOSURE) {
                fn = closure_function(func_val);
            } else {
                vm_set_error(vm, "child must be a function");
                return VM_ERROR_TYPE;
            }

            /* Create bytecode for child (similar to OP_SPAWN) */
            Bytecode *spawn_code = bytecode_new();
            Chunk *fn_chunk = vm->code->functions[fn->code_offset];

            for (size_t i = 0; i < fn_chunk->code_size; i++) {
                chunk_write_byte(spawn_code->main, fn_chunk->code[i], fn_chunk->lines[i]);
            }
            for (size_t i = 0; i < fn_chunk->constants_size; i++) {
                chunk_add_constant(spawn_code->main, value_copy(fn_chunk->constants[i]));
            }
            for (size_t i = 0; i < vm->code->functions_count; i++) {
                Chunk *src = vm->code->functions[i];
                Chunk *dst = chunk_new();
                for (size_t j = 0; j < src->code_size; j++) {
                    chunk_write_byte(dst, src->code[j], src->lines[j]);
                }
                for (size_t j = 0; j < src->constants_size; j++) {
                    chunk_add_constant(dst, value_copy(src->constants[j]));
                }
                bytecode_add_function(spawn_code, dst);
            }
            for (size_t i = 0; i < vm->code->strings_count; i++) {
                bytecode_add_string(spawn_code, vm->code->strings[i]);
            }

            /* Add child to supervisor */
            if (!supervisor_add_child(block->supervisor, sched, block,
                                      child_name, spawn_code, restart)) {
                bytecode_free(spawn_code);
                vm_set_error(vm, "failed to add supervised child");
                return VM_ERROR_RUNTIME;
            }

            /* Get the child PID */
            ChildSpec *spec = supervisor_get_child(block->supervisor, child_name);
            Pid child_pid = spec ? spec->child_pid : PID_INVALID;

            vm_push(vm, value_pid(child_pid));
            break;
        }

        case OP_SUP_REMOVE_CHILD: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            if (!block->supervisor) {
                vm_set_error(vm, "block is not a supervisor");
                return VM_ERROR_RUNTIME;
            }

            /* Pop child name */
            Value *name_val = vm_pop(vm);
            if (!name_val) return VM_ERROR_STACK_UNDERFLOW;

            if (!value_is_string(name_val)) {
                vm_set_error(vm, "child name must be string");
                return VM_ERROR_TYPE;
            }

            bool ok = supervisor_remove_child(block->supervisor, sched, name_val->as.string->data);
            vm_push(vm, value_bool(ok));
            break;
        }

        case OP_SUP_WHICH_CHILDREN: {
            Block *block = (Block *)vm->block;
            if (!block) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            if (!block->supervisor) {
                vm_push(vm, value_array());
                break;
            }

            /* Build array of child info */
            Value *result = value_array();
            size_t count;
            const ChildSpec *children = supervisor_which_children(block->supervisor, &count);

            for (size_t i = 0; i < count; i++) {
                Value *child_info = value_map();
                child_info = map_set(child_info, "name",
                    children[i].name ? value_string(children[i].name) : value_nil());
                child_info = map_set(child_info, "pid", value_pid(children[i].child_pid));
                child_info = map_set(child_info, "restart_count", value_int(children[i].restart_count));
                array_push(result, child_info);
            }

            vm_push(vm, result);
            break;
        }

        case OP_SUP_SHUTDOWN: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            if (!block->supervisor) {
                vm_set_error(vm, "block is not a supervisor");
                return VM_ERROR_RUNTIME;
            }

            supervisor_shutdown(block->supervisor, sched);
            vm_push(vm, value_bool(true));
            break;
        }

        case OP_RECEIVE_TIMEOUT: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_RECEIVE)) {
                vm_set_error(vm, "receive capability denied");
                return VM_ERROR_CAPABILITY;
            }

            /* Check if timeout already fired */
            if (block->timeout_fired) {
                /* Timeout fired - return Err("timeout") */
                block->timeout_fired = false;
                block->pending_timer = NULL;
                vm_push(vm, value_result_err(value_string("timeout")));
                break;
            }

            /* Try to receive a message first */
            Message *msg = block_receive(block);
            if (msg) {
                /* Got a message - cancel any pending timer */
                if (block->pending_timer) {
                    /* Free the timer entry (malloc'd in this opcode) */
                    free(block->pending_timer);
                    block->pending_timer = NULL;
                }

                /* Create result map */
                Value *result = value_map();
                map_set(result, "sender", value_pid(msg->sender));
                map_set(result, "value", msg->value);
                msg->value = NULL;
                message_free(msg);

                /* Return Ok(message) */
                vm_push(vm, value_result_ok(result));
                break;
            }

            /* No message available - set up timeout if not already pending */
            if (!block->pending_timer) {
                /* Pop timeout value from stack */
                Value *timeout_val = vm_pop(vm);
                if (!timeout_val || timeout_val->type != VAL_INT) {
                    vm_set_error(vm, "receive_timeout requires integer timeout");
                    return VM_ERROR_TYPE;
                }
                int64_t timeout_ms = timeout_val->as.integer;

                if (timeout_ms <= 0) {
                    /* Zero or negative timeout - immediate timeout */
                    vm_push(vm, value_result_err(value_string("timeout")));
                    break;
                }

                /* Store deadline in pending_timer (using timestamp for simplicity) */
                /* In a full implementation, we'd use a timer wheel */
                TimerEntry *timer = malloc(sizeof(TimerEntry));
                if (timer) {
                    timer->block_pid = block->pid;
                    timer->deadline_ms = timer_current_time_ms() + (uint64_t)timeout_ms;
                    timer->callback = NULL;
                    timer->callback_ctx = NULL;
                    timer->next = NULL;
                    timer->prev = NULL;
                    timer->cancelled = false;
                    block->pending_timer = timer;
                }
            } else {
                /* Check if deadline has passed */
                if (timer_current_time_ms() >= block->pending_timer->deadline_ms) {
                    /* Timer expired */
                    free(block->pending_timer);
                    block->pending_timer = NULL;
                    vm_push(vm, value_result_err(value_string("timeout")));
                    break;
                }
            }

            /* No message and timer not expired - yield and retry later */
            frame->ip--;  /* Backup IP to retry this instruction */
            block->state = BLOCK_WAITING;
            return VM_YIELD;
        }

        /* Process Groups */

        case OP_GROUP_JOIN: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            Value *name_val = vm_pop(vm);
            if (!name_val || name_val->type != VAL_STRING) {
                vm_set_error(vm, "group_join requires string name");
                return VM_ERROR_TYPE;
            }

            ProcessGroupRegistry *groups = scheduler_get_groups(sched);
            if (!groups) {
                vm_set_error(vm, "process groups not available");
                return VM_ERROR_RUNTIME;
            }

            bool ok = procgroup_join(groups, name_val->as.string->data, block->pid);
            vm_push(vm, value_bool(ok));
            break;
        }

        case OP_GROUP_LEAVE: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            Value *name_val = vm_pop(vm);
            if (!name_val || name_val->type != VAL_STRING) {
                vm_set_error(vm, "group_leave requires string name");
                return VM_ERROR_TYPE;
            }

            ProcessGroupRegistry *groups = scheduler_get_groups(sched);
            if (groups) {
                procgroup_leave(groups, name_val->as.string->data, block->pid);
            }
            vm_push(vm, value_bool(true));
            break;
        }

        case OP_GROUP_SEND: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            Value *message = vm_pop(vm);
            Value *name_val = vm_pop(vm);
            if (!name_val || name_val->type != VAL_STRING) {
                vm_set_error(vm, "group_send requires string name");
                return VM_ERROR_TYPE;
            }

            ProcessGroupRegistry *groups = scheduler_get_groups(sched);
            size_t sent = 0;
            if (groups) {
                sent = procgroup_broadcast(groups, sched, name_val->as.string->data,
                                           block->pid, message);
            }
            vm_push(vm, value_int((int64_t)sent));
            break;
        }

        case OP_GROUP_SEND_OTHERS: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            Value *message = vm_pop(vm);
            Value *name_val = vm_pop(vm);
            if (!name_val || name_val->type != VAL_STRING) {
                vm_set_error(vm, "group_send_others requires string name");
                return VM_ERROR_TYPE;
            }

            ProcessGroupRegistry *groups = scheduler_get_groups(sched);
            size_t sent = 0;
            if (groups) {
                sent = procgroup_broadcast_others(groups, sched, name_val->as.string->data,
                                                  block->pid, message);
            }
            vm_push(vm, value_int((int64_t)sent));
            break;
        }

        case OP_GROUP_MEMBERS: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            Value *name_val = vm_pop(vm);
            if (!name_val || name_val->type != VAL_STRING) {
                vm_set_error(vm, "group_members requires string name");
                return VM_ERROR_TYPE;
            }

            ProcessGroupRegistry *groups = scheduler_get_groups(sched);
            Value *result = value_array();

            if (groups) {
                size_t count;
                Pid *members = procgroup_members(groups, name_val->as.string->data, &count);
                if (members) {
                    for (size_t i = 0; i < count; i++) {
                        array_push(result, value_pid(members[i]));
                    }
                    free(members);
                }
            }
            vm_push(vm, result);
            break;
        }

        case OP_GROUP_LIST: {
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            ProcessGroupRegistry *groups = scheduler_get_groups(sched);
            Value *result = value_array();

            if (groups) {
                size_t count;
                const char **names = procgroup_list(groups, &count);
                if (names) {
                    for (size_t i = 0; i < count; i++) {
                        array_push(result, value_string(names[i]));
                    }
                    free((void *)names);
                }
            }
            vm_push(vm, result);
            break;
        }

        /* Telemetry & Introspection */

        case OP_GET_STATS: {
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            Value *pid_val = vm_pop(vm);
            Pid target_pid = block->pid;  /* Default to self */
            if (pid_val && pid_val->type == VAL_PID) {
                target_pid = pid_val->as.pid;
            }

            Block *target = scheduler_get_block(sched, target_pid);
            if (!target) {
                vm_push(vm, value_nil());
                break;
            }

            /* Build stats map from block counters */
            Value *stats = value_map();
            map_set(stats, "pid", value_pid(target->pid));
            map_set(stats, "messages_sent", value_int((int64_t)target->counters.messages_sent));
            map_set(stats, "messages_received", value_int((int64_t)target->counters.messages_received));
            map_set(stats, "reductions", value_int((int64_t)target->counters.reductions));
            map_set(stats, "gc_collections", value_int((int64_t)target->counters.gc_collections));
            map_set(stats, "gc_bytes_collected", value_int((int64_t)target->counters.gc_bytes_collected));
            map_set(stats, "state", value_string(block_state_name(block_state(target))));
            map_set(stats, "mailbox_count", value_int((int64_t)mailbox_count(&target->mailbox)));

            vm_push(vm, stats);
            break;
        }

        case OP_TRACE: {
            /* Enable tracing on a block */
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            Value *flags_val = vm_pop(vm);
            Value *pid_val = vm_pop(vm);

            /* Get target PID (default to self) */
            Pid target_pid = block->pid;
            if (pid_val && pid_val->type == VAL_PID) {
                target_pid = pid_val->as.pid;
            }

            /* Get trace flags (default to all) */
            TraceFlags flags = TRACE_ALL;
            if (flags_val && flags_val->type == VAL_INT) {
                flags = (TraceFlags)flags_val->as.integer;
            }

            /* Get target block */
            Block *target = scheduler_get_block(sched, target_pid);
            if (!target) {
                vm_push(vm, value_bool(false));
                break;
            }

            /* Create or update tracer on target block */
            if (!target->tracer) {
                target->tracer = tracer_new(flags, 1024);  /* Buffer for 1024 events */
                if (!target->tracer) {
                    vm_push(vm, value_bool(false));
                    break;
                }
            } else {
                tracer_set_flags(target->tracer, flags);
            }

            tracer_set_enabled(target->tracer, true);
            tracer_set_target(target->tracer, block->pid);  /* Set tracer PID (who receives traces) */

            vm_push(vm, value_bool(true));
            break;
        }

        case OP_TRACE_OFF: {
            /* Disable tracing on a block */
            Block *block = (Block *)vm->block;
            Scheduler *sched = (Scheduler *)vm->scheduler;
            if (!block || !sched) {
                vm_set_error(vm, "no runtime context");
                return VM_ERROR_RUNTIME;
            }

            Value *pid_val = vm_pop(vm);

            /* Get target PID (default to self) */
            Pid target_pid = block->pid;
            if (pid_val && pid_val->type == VAL_PID) {
                target_pid = pid_val->as.pid;
            }

            /* Get target block */
            Block *target = scheduler_get_block(sched, target_pid);
            if (!target) {
                vm_push(vm, value_bool(false));
                break;
            }

            /* Disable tracer on target block */
            if (target->tracer) {
                tracer_set_enabled(target->tracer, false);
            }

            vm_push(vm, value_bool(true));
            break;
        }

        /* Selective Receive */

        case OP_RECEIVE_MATCH: {
            Block *block = (Block *)vm->block;
            if (!block) {
                vm_set_error(vm, "no block context");
                return VM_ERROR_RUNTIME;
            }

            /* Check capability */
            if (!block_has_cap(block, CAP_RECEIVE)) {
                vm_set_error(vm, "receive capability denied");
                return VM_ERROR_CAPABILITY;
            }

            Value *pattern = vm_pop(vm);

            /* Helper function to check if a message matches the pattern */
            bool matched = false;
            Message *matched_msg = NULL;

            /* First, scan the save queue for a matching message */
            Message *prev = NULL;
            Message *scan = block->save_queue_head;
            while (scan && !matched) {
                bool msg_matches = true;

                /* Pattern matching logic */
                if (pattern && pattern->type == VAL_MAP && scan->value && scan->value->type == VAL_MAP) {
                    Value *keys_val = map_keys(pattern);
                    if (keys_val && keys_val->type == VAL_ARRAY) {
                        Array *keys = keys_val->as.array;
                        for (size_t i = 0; i < keys->length && msg_matches; i++) {
                            Value *key = keys->items[i];
                            if (key && key->type == VAL_STRING) {
                                const char *key_str = key->as.string->data;
                                Value *pattern_val = map_get(pattern, key_str);
                                Value *msg_val = map_get(scan->value, key_str);

                                if (!msg_val) {
                                    msg_matches = false;
                                } else if (pattern_val && pattern_val->type != VAL_NIL) {
                                    if (!value_equals(pattern_val, msg_val)) {
                                        msg_matches = false;
                                    }
                                }
                            }
                        }
                    }
                }

                if (msg_matches) {
                    /* Found a match in save queue - remove it */
                    matched = true;
                    matched_msg = scan;

                    if (prev) {
                        prev->next = scan->next;
                    } else {
                        block->save_queue_head = scan->next;
                    }
                    if (scan == block->save_queue_tail) {
                        block->save_queue_tail = prev;
                    }
                } else {
                    prev = scan;
                    scan = scan->next;
                }
            }

            /* If not found in save queue, scan the mailbox */
            while (!matched) {
                Message *msg = block_receive(block);
                if (!msg) break;  /* No more messages */

                bool msg_matches = true;

                /* Pattern matching logic */
                if (pattern && pattern->type == VAL_MAP && msg->value && msg->value->type == VAL_MAP) {
                    Value *keys_val = map_keys(pattern);
                    if (keys_val && keys_val->type == VAL_ARRAY) {
                        Array *keys = keys_val->as.array;
                        for (size_t i = 0; i < keys->length && msg_matches; i++) {
                            Value *key = keys->items[i];
                            if (key && key->type == VAL_STRING) {
                                const char *key_str = key->as.string->data;
                                Value *pattern_val = map_get(pattern, key_str);
                                Value *msg_val = map_get(msg->value, key_str);

                                if (!msg_val) {
                                    msg_matches = false;
                                } else if (pattern_val && pattern_val->type != VAL_NIL) {
                                    if (!value_equals(pattern_val, msg_val)) {
                                        msg_matches = false;
                                    }
                                }
                            }
                        }
                    }
                }

                if (msg_matches) {
                    matched = true;
                    matched_msg = msg;
                } else {
                    /* Non-matching message - add to save queue tail */
                    msg->next = NULL;
                    if (block->save_queue_tail) {
                        block->save_queue_tail->next = msg;
                    } else {
                        block->save_queue_head = msg;
                    }
                    block->save_queue_tail = msg;
                }
            }

            if (matched && matched_msg) {
                /* Create result map with sender and value */
                Value *result = value_map();
                map_set(result, "sender", value_pid(matched_msg->sender));
                map_set(result, "value", matched_msg->value);
                matched_msg->value = NULL;
                message_free(matched_msg);
                vm_push(vm, result);
            } else {
                /* No matching message available - yield and retry */
                frame->ip--;
                block->state = BLOCK_WAITING;
                return VM_YIELD;
            }
            break;
        }

        default:
            vm_set_error(vm, "unknown opcode");
            return VM_ERROR_RUNTIME;
        } /* end switch */

#if USE_COMPUTED_GOTO
        /* After handling cold opcode, return to fast dispatch */
        if (++vm->reductions >= vm->reduction_limit)
            return VM_YIELD;
        goto *dispatch_table[read_byte(frame)];
    } /* end computed goto slow block */
#else
    } /* end for loop */
#endif
}

VMResult vm_step(VM *vm) {
    size_t old_limit = vm->reduction_limit;
    vm->reduction_limit = 1;
    VMResult result = vm_run(vm);
    vm->reduction_limit = old_limit;
    return result;
}

VMResult vm_resume(VM *vm) {
    return vm_run(vm);
}

/* Error Handling */

const char *vm_error(VM *vm) {
    return vm->error;
}

int vm_error_line(VM *vm) {
    return vm->error_line;
}

void vm_set_error(VM *vm, const char *message) {
    vm->error = message;

    if (vm->frame_count > 0) {
        CallFrame *frame = &vm->frames[vm->frame_count - 1];
        size_t offset = (size_t)(frame->ip - frame->chunk->code - 1);
        if (offset < frame->chunk->code_size) {
            vm->error_line = frame->chunk->lines[offset];
        }
    }
}

/* Debugging functions are in debug/trace.c */
