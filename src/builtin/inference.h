/*
 * Agim - LLM Inference Interface
 *
 * Provides the callback mechanism for LLM inference calls.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_BUILTIN_INFERENCE_H
#define AGIM_BUILTIN_INFERENCE_H

typedef struct Block Block;
typedef struct Value Value;

/* Inference Callback */

typedef Value *(*InferCallback)(Block *block, Value *prompt, void *context);

/* Inference State */

typedef struct InferenceState {
    InferCallback callback;
    void *context;
} InferenceState;

/* Inference API */

void inference_init(InferenceState *state);
void inference_set_callback(InferenceState *state, InferCallback callback, void *context);
Value *inference_call(InferenceState *state, Block *block, Value *prompt);

#endif /* AGIM_BUILTIN_INFERENCE_H */
