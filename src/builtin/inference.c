/*
 * Agim - LLM Inference Interface
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "builtin/inference.h"
#include "debug/log.h"

#include <stddef.h>

void inference_init(InferenceState *state) {
    if (!state) return;
    state->callback = NULL;
    state->context = NULL;
}

void inference_set_callback(InferenceState *state, InferCallback callback, void *context) {
    if (!state) return;
    state->callback = callback;
    state->context = context;
}

Value *inference_call(InferenceState *state, Block *block, Value *prompt) {
    if (!state || !state->callback) {
        LOG_WARN("inference: call attempted with no callback configured");
        return NULL;
    }
    return state->callback(block, prompt, state->context);
}
