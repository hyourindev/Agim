/*
 * Agim - Built-in Tools
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "builtin/tools.h"
#include "runtime/block.h"
#include "vm/value.h"
#include "util/alloc.h"
#include "debug/log.h"

#include <stdio.h>
#include <string.h>

/* Tool Registry */

void tools_init(ToolRegistry *registry) {
    if (!registry) return;
    registry->tools = NULL;
    registry->count = 0;
}

void tools_free(ToolRegistry *registry) {
    if (!registry) return;

    Tool *tool = registry->tools;
    while (tool) {
        Tool *next = tool->next;
        agim_free((void *)tool->name);
        agim_free((void *)tool->description);
        /* Note: params are typically statically allocated, so don't free them */
        agim_free(tool);
        tool = next;
    }

    registry->tools = NULL;
    registry->count = 0;
}

bool tools_register(ToolRegistry *registry, const char *name,
                    ToolFunction func, size_t min_args, size_t max_args,
                    uint32_t required_caps, void *context) {
    return tools_register_with_schema(registry, name, NULL, func,
                                      min_args, max_args, required_caps,
                                      context, NULL, 0);
}

bool tools_register_with_schema(ToolRegistry *registry, const char *name,
                                const char *description,
                                ToolFunction func, size_t min_args, size_t max_args,
                                uint32_t required_caps, void *context,
                                ToolParam *params, size_t param_count) {
    if (!registry || !name || !func) return false;

    /* Check if already registered */
    Tool *existing = registry->tools;
    while (existing) {
        if (strcmp(existing->name, name) == 0) {
            LOG_WARN("tools: tool '%s' already registered", name);
            return false;
        }
        existing = existing->next;
    }

    /* Create new tool */
    Tool *tool = agim_alloc(sizeof(Tool));
    if (!tool) {
        LOG_ERROR("tools: failed to allocate Tool for '%s'", name);
        return false;
    }
    tool->name = strdup(name);
    tool->description = description ? strdup(description) : NULL;
    tool->func = func;
    tool->min_args = min_args;
    tool->max_args = max_args;
    tool->required_caps = required_caps;
    tool->context = context;
    tool->params = params;
    tool->param_count = param_count;

    /* Add to list */
    tool->next = registry->tools;
    registry->tools = tool;
    registry->count++;

    return true;
}

void tools_unregister(ToolRegistry *registry, const char *name) {
    if (!registry || !name) return;

    Tool *prev = NULL;
    Tool *tool = registry->tools;

    while (tool) {
        if (strcmp(tool->name, name) == 0) {
            if (prev) {
                prev->next = tool->next;
            } else {
                registry->tools = tool->next;
            }
            agim_free((void *)tool->name);
            agim_free(tool);
            registry->count--;
            return;
        }
        prev = tool;
        tool = tool->next;
    }
}

Value *tools_call(ToolRegistry *registry, Block *block,
                  const char *name, Value **args, size_t arg_count) {
    if (!registry || !name) return NULL;

    /* Find tool */
    Tool *tool = registry->tools;
    while (tool) {
        if (strcmp(tool->name, name) == 0) {
            break;
        }
        tool = tool->next;
    }

    if (!tool) {
        LOG_WARN("tools: tool '%s' not found", name);
        return NULL;
    }

    /* Check arity */
    if (arg_count < tool->min_args || arg_count > tool->max_args) {
        LOG_ERROR("tools: tool '%s' arity mismatch (got %zu, expected %zu-%zu)",
                  name, arg_count, tool->min_args, tool->max_args);
        return NULL;
    }

    /* Check capabilities */
    if (block && tool->required_caps) {
        if (!block_has_cap(block, tool->required_caps)) {
            LOG_WARN("tools: block lacks required capabilities for tool '%s'", name);
            return NULL;
        }
    }

    /* Call tool */
    return tool->func(block, args, arg_count, tool->context);
}

const Tool *tools_list(ToolRegistry *registry) {
    return registry ? registry->tools : NULL;
}

size_t tools_count(ToolRegistry *registry) {
    return registry ? registry->count : 0;
}

Tool *tools_find(ToolRegistry *registry, const char *name) {
    if (!registry || !name) return NULL;

    Tool *tool = registry->tools;
    while (tool) {
        if (strcmp(tool->name, name) == 0) {
            return tool;
        }
        tool = tool->next;
    }
    return NULL;
}

static const char *tool_param_type_name(ToolParamType type) {
    switch (type) {
    case TOOL_PARAM_STRING: return "string";
    case TOOL_PARAM_INT:    return "integer";
    case TOOL_PARAM_FLOAT:  return "number";
    case TOOL_PARAM_BOOL:   return "boolean";
    case TOOL_PARAM_ARRAY:  return "array";
    case TOOL_PARAM_MAP:    return "object";
    case TOOL_PARAM_ANY:    return "any";
    default:                return "unknown";
    }
}

char *tools_get_schema_json(const Tool *tool) {
    if (!tool) return NULL;

    /* Build OpenAI function calling compatible schema */
    size_t capacity = 1024;
    char *buf = agim_alloc(capacity);
    if (!buf) {
        LOG_ERROR("tools: failed to allocate schema buffer for tool '%s'", tool->name);
        return NULL;
    }
    size_t len = 0;

    len += snprintf(buf + len, capacity - len,
        "{\n"
        "  \"type\": \"function\",\n"
        "  \"function\": {\n"
        "    \"name\": \"%s\",\n",
        tool->name);

    if (tool->description) {
        len += snprintf(buf + len, capacity - len,
            "    \"description\": \"%s\",\n",
            tool->description);
    }

    len += snprintf(buf + len, capacity - len,
        "    \"parameters\": {\n"
        "      \"type\": \"object\",\n"
        "      \"properties\": {");

    /* Add parameters */
    bool first = true;
    for (size_t i = 0; i < tool->param_count; i++) {
        ToolParam *p = &tool->params[i];
        if (!first) {
            len += snprintf(buf + len, capacity - len, ",");
        }
        first = false;

        len += snprintf(buf + len, capacity - len,
            "\n        \"%s\": {\n"
            "          \"type\": \"%s\"",
            p->name, tool_param_type_name(p->type));

        if (p->description) {
            len += snprintf(buf + len, capacity - len,
                ",\n          \"description\": \"%s\"",
                p->description);
        }

        len += snprintf(buf + len, capacity - len, "\n        }");
    }

    len += snprintf(buf + len, capacity - len, "\n      },\n      \"required\": [");

    /* Add required parameters */
    first = true;
    for (size_t i = 0; i < tool->param_count; i++) {
        if (tool->params[i].required) {
            if (!first) {
                len += snprintf(buf + len, capacity - len, ", ");
            }
            first = false;
            len += snprintf(buf + len, capacity - len, "\"%s\"", tool->params[i].name);
        }
    }

    len += snprintf(buf + len, capacity - len,
        "]\n"
        "    }\n"
        "  }\n"
        "}");

    return buf;
}

char *tools_get_all_schemas_json(ToolRegistry *registry) {
    if (!registry) return NULL;

    size_t capacity = 4096;
    char *buf = agim_alloc(capacity);
    if (!buf) {
        LOG_ERROR("tools: failed to allocate all-schemas buffer");
        return NULL;
    }
    size_t len = 0;

    len += snprintf(buf + len, capacity - len, "[");

    Tool *tool = registry->tools;
    bool first = true;
    while (tool) {
        if (!first) {
            len += snprintf(buf + len, capacity - len, ",");
        }
        first = false;

        char *schema = tools_get_schema_json(tool);
        if (schema) {
            /* Ensure buffer has enough space */
            size_t schema_len = strlen(schema);
            while (len + schema_len + 10 > capacity) {
                capacity *= 2;
                char *new_buf = agim_realloc(buf, capacity);
                if (!new_buf) {
                    LOG_ERROR("tools: failed to grow all-schemas buffer to %zu bytes", capacity);
                    agim_free(schema);
                    agim_free(buf);
                    return NULL;
                }
                buf = new_buf;
            }
            len += snprintf(buf + len, capacity - len, "\n%s", schema);
            agim_free(schema);
        }
        tool = tool->next;
    }

    len += snprintf(buf + len, capacity - len, "\n]");
    return buf;
}

Value *tools_list_as_value(ToolRegistry *registry) {
    if (!registry) return value_array();

    Value *arr = value_array();
    Tool *tool = registry->tools;

    while (tool) {
        Value *info = value_map();
        info = map_set(info, "name", value_string(tool->name));
        if (tool->description) {
            info = map_set(info, "description", value_string(tool->description));
        }
        info = map_set(info, "min_args", value_int((int64_t)tool->min_args));
        info = map_set(info, "max_args", value_int((int64_t)tool->max_args));

        /* Add parameters info */
        if (tool->param_count > 0) {
            Value *params = value_array();
            for (size_t i = 0; i < tool->param_count; i++) {
                ToolParam *p = &tool->params[i];
                Value *param_info = value_map();
                param_info = map_set(param_info, "name", value_string(p->name));
                param_info = map_set(param_info, "type", value_string(tool_param_type_name(p->type)));
                param_info = map_set(param_info, "required", value_bool(p->required));
                if (p->description) {
                    param_info = map_set(param_info, "description", value_string(p->description));
                }
                params = array_push(params, param_info);
            }
            info = map_set(info, "params", params);
        }

        arr = array_push(arr, info);
        tool = tool->next;
    }

    return arr;
}

/* Built-in Tools */

static Value *tool_print(Block *block, Value **args, size_t arg_count,
                         void *context) {
    (void)block;
    (void)context;

    for (size_t i = 0; i < arg_count; i++) {
        if (i > 0) printf(" ");
        value_print(args[i]);
    }
    printf("\n");

    return value_nil();
}

static Value *tool_type(Block *block, Value **args, size_t arg_count,
                        void *context) {
    (void)block;
    (void)context;

    if (arg_count < 1) return value_nil();

    const char *type_name;
    switch (args[0]->type) {
    case VAL_NIL:      type_name = "nil";      break;
    case VAL_BOOL:     type_name = "bool";     break;
    case VAL_INT:      type_name = "int";      break;
    case VAL_FLOAT:    type_name = "float";    break;
    case VAL_STRING:   type_name = "string";   break;
    case VAL_ARRAY:    type_name = "array";    break;
    case VAL_MAP:      type_name = "map";      break;
    case VAL_PID:      type_name = "pid";      break;
    case VAL_FUNCTION: type_name = "function"; break;
    case VAL_BYTES:    type_name = "bytes";    break;
    case VAL_VECTOR:   type_name = "vector";   break;
    case VAL_CLOSURE:  type_name = "closure";  break;
    default:           type_name = "unknown";  break;
    }

    return value_string(type_name);
}

static Value *tool_len(Block *block, Value **args, size_t arg_count,
                       void *context) {
    (void)block;
    (void)context;

    if (arg_count < 1) return value_int(0);

    switch (args[0]->type) {
    case VAL_STRING:
        return value_int((int64_t)args[0]->as.string->length);
    case VAL_ARRAY:
        return value_int((int64_t)args[0]->as.array->length);
    case VAL_MAP:
        return value_int((int64_t)args[0]->as.map->size);
    case VAL_BYTES:
        return value_int((int64_t)args[0]->as.bytes->length);
    case VAL_VECTOR:
        return value_int((int64_t)vector_dim(args[0]));
    default:
        return value_int(0);
    }
}

static Value *tool_keys(Block *block, Value **args, size_t arg_count,
                        void *context) {
    (void)block;
    (void)context;

    if (arg_count < 1 || args[0]->type != VAL_MAP) {
        return value_array();
    }

    return map_keys(args[0]);
}

static Value *tool_str(Block *block, Value **args, size_t arg_count,
                       void *context) {
    (void)block;
    (void)context;

    if (arg_count < 1) return value_string("");

    char *repr = value_repr(args[0]);
    Value *result = value_string(repr);
    agim_free(repr);
    return result;
}

static Value *tool_int(Block *block, Value **args, size_t arg_count,
                       void *context) {
    (void)block;
    (void)context;

    if (arg_count < 1) return value_int(0);

    return value_int(value_to_int(args[0]));
}

static Value *tool_float(Block *block, Value **args, size_t arg_count,
                         void *context) {
    (void)block;
    (void)context;

    if (arg_count < 1) return value_float(0.0);

    return value_float(value_to_float(args[0]));
}

void tools_register_builtins(ToolRegistry *registry) {
    if (!registry) return;

    tools_register(registry, "print", tool_print, 0, 10, CAP_NONE, NULL);
    tools_register(registry, "type", tool_type, 1, 1, CAP_NONE, NULL);
    tools_register(registry, "len", tool_len, 1, 1, CAP_NONE, NULL);
    tools_register(registry, "keys", tool_keys, 1, 1, CAP_NONE, NULL);
    tools_register(registry, "str", tool_str, 1, 1, CAP_NONE, NULL);
    tools_register(registry, "int", tool_int, 1, 1, CAP_NONE, NULL);
    tools_register(registry, "float", tool_float, 1, 1, CAP_NONE, NULL);
}

/* Bytecode Tool Registration */

#include "vm/vm.h"
#include "vm/bytecode.h"
#include "types/closure.h"

static Value *bytecode_tool_call(Block *block, Value **args, size_t arg_count,
                                  void *context) {
    BytecodeToolContext *ctx = (BytecodeToolContext *)context;
    if (!ctx || !ctx->vm || !ctx->code) {
        LOG_ERROR("tools: bytecode tool called with invalid context");
        return value_nil();
    }

    VM *vm = ctx->vm;
    size_t func_index = ctx->func_index;

    /* Validate function index */
    if (func_index >= ctx->code->functions_count) {
        LOG_ERROR("tools: bytecode tool function index %zu out of bounds (max %zu)",
                  func_index, ctx->code->functions_count);
        return value_nil();
    }

    /* Get the function chunk */
    Chunk *func_chunk = ctx->code->functions[func_index];
    if (!func_chunk) {
        LOG_ERROR("tools: bytecode tool function chunk at index %zu is NULL", func_index);
        return value_nil();
    }

    /* Create a function value to call */
    Function *fn = agim_alloc(sizeof(Function));
    if (!fn) {
        LOG_ERROR("tools: failed to allocate Function for bytecode tool");
        return value_nil();
    }
    fn->name = NULL;
    fn->arity = arg_count;
    fn->code_offset = func_index;
    fn->locals_count = 0;
    fn->parent = NULL;

    Value *func_val = agim_alloc(sizeof(Value));
    if (!func_val) {
        LOG_ERROR("tools: failed to allocate Value for bytecode tool function");
        agim_free(fn);
        return value_nil();
    }
    func_val->type = VAL_FUNCTION;
    func_val->refcount = 1;
    func_val->flags = 0;
    func_val->gc_state = 0;
    func_val->as.function = fn;
    func_val->next = NULL;

    /* Push function and arguments onto stack */
    vm_push(vm, func_val);
    for (size_t i = 0; i < arg_count; i++) {
        vm_push(vm, args[i]);
    }

    /* Set up call frame */
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->function = fn;
    frame->chunk = func_chunk;
    frame->ip = func_chunk->code;
    frame->slots = vm->stack_top - arg_count - 1;

    (void)block;

    /* Run until function returns */
    int initial_frame_count = vm->frame_count;
    size_t saved_limit = vm->reduction_limit;
    vm->reduction_limit = 1000000;

    VMResult result;
    do {
        result = vm_run(vm);
    } while (result == VM_YIELD && vm->frame_count >= initial_frame_count);

    vm->reduction_limit = saved_limit;

    /* Get return value */
    Value *ret = value_nil();
    if (result == VM_OK || result == VM_HALT || vm->frame_count < initial_frame_count) {
        ret = vm_pop(vm);
        if (!ret) ret = value_nil();
    }

    return ret;
}

static ToolParamType type_string_to_enum(const char *type) {
    if (!type) return TOOL_PARAM_ANY;
    if (strcmp(type, "string") == 0) return TOOL_PARAM_STRING;
    if (strcmp(type, "int") == 0) return TOOL_PARAM_INT;
    if (strcmp(type, "float") == 0) return TOOL_PARAM_FLOAT;
    if (strcmp(type, "bool") == 0) return TOOL_PARAM_BOOL;
    if (strcmp(type, "array") == 0) return TOOL_PARAM_ARRAY;
    if (strcmp(type, "map") == 0) return TOOL_PARAM_MAP;
    return TOOL_PARAM_ANY;
}

void tools_register_from_bytecode(ToolRegistry *registry, Bytecode *code, VM *vm) {
    if (!registry || !code || !vm) return;

    size_t tool_count;
    const ToolInfo *tools = bytecode_get_tools(code, &tool_count);

    for (size_t i = 0; i < tool_count; i++) {
        const ToolInfo *info = &tools[i];

        /* Skip if already registered */
        if (tools_find(registry, info->name)) {
            continue;
        }

        /* Create context for this tool */
        BytecodeToolContext *ctx = agim_alloc(sizeof(BytecodeToolContext));
        if (!ctx) {
            LOG_ERROR("tools: failed to allocate BytecodeToolContext for tool '%s'", info->name);
            continue;
        }

        ctx->vm = vm;
        ctx->func_index = info->func_index;
        ctx->code = code;

        /* Convert bytecode params to runtime params */
        ToolParam *params = NULL;
        if (info->param_count > 0) {
            params = agim_alloc(sizeof(ToolParam) * info->param_count);
            for (size_t j = 0; j < info->param_count; j++) {
                params[j].name = info->params[j].name;
                params[j].description = info->params[j].description;
                params[j].type = type_string_to_enum(info->params[j].type);
                params[j].required = true;
                params[j].default_value = NULL;
            }
        }

        /* Register the tool */
        tools_register_with_schema(registry, info->name, info->description,
                                   bytecode_tool_call, info->param_count,
                                   info->param_count, 0, ctx, params,
                                   info->param_count);
    }
}
