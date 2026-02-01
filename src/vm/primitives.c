/*
 * Agim - Built-in Primitives
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "vm/primitives.h"
#include "util/alloc.h"
#include "debug/log.h"

/* Primitives Runtime */

PrimitivesRuntime *primitives_new(void) {
    PrimitivesRuntime *rt = agim_alloc(sizeof(PrimitivesRuntime));
    if (!rt) {
        LOG_ERROR("primitives: failed to allocate PrimitivesRuntime");
        return NULL;
    }

    inference_init(&rt->inference);
    tools_init(&rt->tools);
    rt->memory = memory_store_new();

    return rt;
}

void primitives_free(PrimitivesRuntime *rt) {
    if (!rt) return;

    tools_free(&rt->tools);
    memory_store_free(rt->memory);

    agim_free(rt);
}

/* Inference API */

void primitives_set_infer(PrimitivesRuntime *rt, InferCallback callback,
                          void *context) {
    if (!rt) return;
    inference_set_callback(&rt->inference, callback, context);
}

Value *primitives_infer(PrimitivesRuntime *rt, Block *block, Value *prompt) {
    if (!rt) return NULL;
    return inference_call(&rt->inference, block, prompt);
}

/* Tool API */

bool primitives_register_tool(PrimitivesRuntime *rt, const char *name,
                              ToolFunction func, size_t min_args,
                              size_t max_args, uint32_t required_caps,
                              void *context) {
    if (!rt) return false;
    return tools_register(&rt->tools, name, func, min_args, max_args,
                          required_caps, context);
}

void primitives_unregister_tool(PrimitivesRuntime *rt, const char *name) {
    if (!rt) return;
    tools_unregister(&rt->tools, name);
}

Value *primitives_call_tool(PrimitivesRuntime *rt, Block *block,
                            const char *name, Value **args, size_t arg_count) {
    if (!rt) return NULL;
    return tools_call(&rt->tools, block, name, args, arg_count);
}

const Tool *primitives_get_tools(PrimitivesRuntime *rt) {
    if (!rt) return NULL;
    return tools_list(&rt->tools);
}

/* Memory API */

Value *primitives_memory_get(PrimitivesRuntime *rt, const char *key) {
    if (!rt) return NULL;
    return memory_get(rt->memory, key);
}

bool primitives_memory_set(PrimitivesRuntime *rt, const char *key, Value *value) {
    if (!rt) return false;
    return memory_set(rt->memory, key, value);
}

bool primitives_memory_delete(PrimitivesRuntime *rt, const char *key) {
    if (!rt) return false;
    return memory_delete(rt->memory, key);
}

bool primitives_memory_has(PrimitivesRuntime *rt, const char *key) {
    if (!rt) return false;
    return memory_has(rt->memory, key);
}

void primitives_memory_clear(PrimitivesRuntime *rt) {
    if (!rt) return;
    memory_clear(rt->memory);
}

/* Built-in Tools */

void primitives_register_builtins(PrimitivesRuntime *rt) {
    if (!rt) return;
    tools_register_builtins(&rt->tools);
}
