/*
 * Agim - Built-in Primitives
 *
 * Provides the interface for AI-specific operations:
 * - INFER: LLM inference calls
 * - TOOL_CALL: Built-in tool execution
 * - MEMORY_GET/SET: Persistent memory storage
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

/*============================================================================
 * Forward Declarations
 *============================================================================*/

typedef struct Block Block;
typedef struct Scheduler Scheduler;

/*============================================================================
 * Primitives Runtime
 *============================================================================*/

typedef struct PrimitivesRuntime {
    InferenceState inference;
    ToolRegistry tools;
    MemoryStore *memory;
} PrimitivesRuntime;

/*============================================================================
 * Primitives Runtime API
 *============================================================================*/

/**
 * Create a new primitives runtime.
 */
PrimitivesRuntime *primitives_new(void);

/**
 * Free the primitives runtime.
 */
void primitives_free(PrimitivesRuntime *rt);

/*============================================================================
 * Inference API
 *============================================================================*/

/**
 * Set the inference callback.
 */
void primitives_set_infer(PrimitivesRuntime *rt, InferCallback callback,
                          void *context);

/**
 * Execute inference.
 * Returns NULL if no callback is set.
 */
Value *primitives_infer(PrimitivesRuntime *rt, Block *block, Value *prompt);

/*============================================================================
 * Tool API
 *============================================================================*/

/**
 * Register a tool.
 */
bool primitives_register_tool(PrimitivesRuntime *rt, const char *name,
                              ToolFunction func, size_t min_args,
                              size_t max_args, uint32_t required_caps,
                              void *context);

/**
 * Unregister a tool.
 */
void primitives_unregister_tool(PrimitivesRuntime *rt, const char *name);

/**
 * Call a tool by name.
 */
Value *primitives_call_tool(PrimitivesRuntime *rt, Block *block,
                            const char *name, Value **args, size_t arg_count);

/**
 * Get list of registered tools.
 */
const Tool *primitives_get_tools(PrimitivesRuntime *rt);

/*============================================================================
 * Memory API
 *============================================================================*/

/**
 * Get a value from persistent memory.
 */
Value *primitives_memory_get(PrimitivesRuntime *rt, const char *key);

/**
 * Set a value in persistent memory.
 */
bool primitives_memory_set(PrimitivesRuntime *rt, const char *key, Value *value);

/**
 * Delete a key from persistent memory.
 */
bool primitives_memory_delete(PrimitivesRuntime *rt, const char *key);

/**
 * Check if a key exists in persistent memory.
 */
bool primitives_memory_has(PrimitivesRuntime *rt, const char *key);

/**
 * Clear all persistent memory.
 */
void primitives_memory_clear(PrimitivesRuntime *rt);

/*============================================================================
 * Built-in Tools
 *============================================================================*/

/**
 * Register default built-in tools (print, type, len, etc.)
 */
void primitives_register_builtins(PrimitivesRuntime *rt);

#endif /* AGIM_VM_PRIMITIVES_H */
