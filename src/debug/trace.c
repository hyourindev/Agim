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

#include <stdio.h>

/* Stack Printing */

void vm_print_stack(VM *vm) {
    printf("Stack: ");
    for (Value **slot = vm->stack; slot < vm->stack_top; slot++) {
        printf("[ ");
        value_print(*slot);
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
