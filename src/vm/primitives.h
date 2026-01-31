/*
 * Agim - Built-in Primitives
 *
 * Interface for AI-specific operations (INFER, TOOL_CALL, MEMORY).
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_PRIMITIVES_H
#define AGIM_VM_PRIMITIVES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm/value.h"
#include "builtin/inference.h"
#include "builtin/tools.h"
#include "builtin/memory.h"

typedef struct Block Block;
typedef struct Scheduler Scheduler;

/* Primitives Runtime */

typedef struct PrimitivesRuntime {
    InferenceState inference;
    ToolRegistry tools;
    MemoryStore *memory;
} PrimitivesRuntime;

/* Primitives Runtime API */

PrimitivesRuntime *primitives_new(void);
void primitives_free(PrimitivesRuntime *rt);

/* Inference API */

void primitives_set_infer(PrimitivesRuntime *rt, InferCallback callback,
                          void *context);
Value *primitives_infer(PrimitivesRuntime *rt, Block *block, Value *prompt);

/* Tool API */

bool primitives_register_tool(PrimitivesRuntime *rt, const char *name,
                              ToolFunction func, size_t min_args,
                              size_t max_args, uint32_t required_caps,
                              void *context);
void primitives_unregister_tool(PrimitivesRuntime *rt, const char *name);
Value *primitives_call_tool(PrimitivesRuntime *rt, Block *block,
                            const char *name, Value **args, size_t arg_count);
const Tool *primitives_get_tools(PrimitivesRuntime *rt);

/* Memory API */

Value *primitives_memory_get(PrimitivesRuntime *rt, const char *key);
bool primitives_memory_set(PrimitivesRuntime *rt, const char *key, Value *value);
bool primitives_memory_delete(PrimitivesRuntime *rt, const char *key);
bool primitives_memory_has(PrimitivesRuntime *rt, const char *key);
void primitives_memory_clear(PrimitivesRuntime *rt);

/* Built-in Tools */

void primitives_register_builtins(PrimitivesRuntime *rt);

#endif /* AGIM_VM_PRIMITIVES_H */
