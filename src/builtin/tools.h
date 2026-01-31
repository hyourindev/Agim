/*
 * Agim - Built-in Tools
 *
 * Tool registration and execution framework.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_BUILTIN_TOOLS_H
#define AGIM_BUILTIN_TOOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Block Block;
typedef struct Value Value;

/* Tool Function Type */

typedef Value *(*ToolFunction)(Block *block, Value **args, size_t arg_count,
                               void *context);

/* Tool Parameter Schema */

typedef enum ToolParamType {
    TOOL_PARAM_STRING,
    TOOL_PARAM_INT,
    TOOL_PARAM_FLOAT,
    TOOL_PARAM_BOOL,
    TOOL_PARAM_ARRAY,
    TOOL_PARAM_MAP,
    TOOL_PARAM_ANY,
} ToolParamType;

typedef struct ToolParam {
    const char *name;
    const char *description;
    ToolParamType type;
    bool required;
    Value *default_value;
} ToolParam;

/* Tool Registration Entry */

typedef struct Tool {
    const char *name;
    const char *description;
    ToolFunction func;
    size_t min_args;
    size_t max_args;
    uint32_t required_caps;
    void *context;
    ToolParam *params;
    size_t param_count;
    struct Tool *next;
} Tool;

/* Tool Registry */

typedef struct ToolRegistry {
    Tool *tools;
    size_t count;
} ToolRegistry;

/* Tool API */

void tools_init(ToolRegistry *registry);
void tools_free(ToolRegistry *registry);
bool tools_register(ToolRegistry *registry, const char *name,
                    ToolFunction func, size_t min_args, size_t max_args,
                    uint32_t required_caps, void *context);
bool tools_register_with_schema(ToolRegistry *registry, const char *name,
                                const char *description,
                                ToolFunction func, size_t min_args, size_t max_args,
                                uint32_t required_caps, void *context,
                                ToolParam *params, size_t param_count);
void tools_unregister(ToolRegistry *registry, const char *name);
Value *tools_call(ToolRegistry *registry, Block *block,
                  const char *name, Value **args, size_t arg_count);
const Tool *tools_list(ToolRegistry *registry);
size_t tools_count(ToolRegistry *registry);
Tool *tools_find(ToolRegistry *registry, const char *name);
char *tools_get_schema_json(const Tool *tool);
char *tools_get_all_schemas_json(ToolRegistry *registry);
Value *tools_list_as_value(ToolRegistry *registry);

/* Built-in Tools */

void tools_register_builtins(ToolRegistry *registry);

/* Bytecode Tool Registration */

typedef struct VM VM;
typedef struct Bytecode Bytecode;

typedef struct BytecodeToolContext {
    VM *vm;
    size_t func_index;
    Bytecode *code;
} BytecodeToolContext;

void tools_register_from_bytecode(ToolRegistry *registry, Bytecode *code, VM *vm);

#endif /* AGIM_BUILTIN_TOOLS_H */
