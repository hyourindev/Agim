/*
 * Agim - Execution Tracing
 *
 * Stack and call frame debugging utilities.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_DEBUG_TRACE_H
#define AGIM_DEBUG_TRACE_H

/* Forward declaration */
typedef struct VM VM;

/**
 * Print the current stack contents.
 */
void vm_print_stack(VM *vm);

/**
 * Print a stack trace (call frames).
 */
void vm_print_trace(VM *vm);

#endif /* AGIM_DEBUG_TRACE_H */
