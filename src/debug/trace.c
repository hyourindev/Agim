/*
 * Agim - Execution Tracing
 *
 * Stack and call frame debugging utilities.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "debug/trace.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "vm/nanbox.h"

#include <stdio.h>
#include <inttypes.h>

/* Stack Printing */

void vm_print_stack(VM *vm) {
    printf("Stack: ");
    for (NanValue *slot = vm->stack; slot < vm->stack_top; slot++) {
        printf("[ ");
        if (nanbox_is_obj(*slot)) {
            value_print((Value *)nanbox_as_obj(*slot));
        } else if (nanbox_is_int(*slot)) {
            printf("%" PRId64, nanbox_as_int(*slot));
        } else if (nanbox_is_double(*slot)) {
            printf("%g", nanbox_as_double(*slot));
        } else if (nanbox_is_nil(*slot)) {
            printf("nil");
        } else if (nanbox_is_bool(*slot)) {
            printf("%s", nanbox_as_bool(*slot) ? "true" : "false");
        } else {
            printf("?");
        }
        printf(" ]");
    }
    printf("\n");
}

/* Stack Trace */

void vm_print_trace(VM *vm) {
    printf("Stack trace:\n");
    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];
        size_t offset = (size_t)(frame->ip - frame->chunk->code - 1);
        int line = frame->chunk->lines[offset];

        printf("  [line %d] in ", line);
        if (frame->function && frame->function->name) {
            printf("%s()\n", frame->function->name);
        } else {
            printf("<script>\n");
        }
    }
}
