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

/* Forward declarations */
typedef struct Block Block;
typedef struct Value Value;

/*============================================================================
 * Inference Callback
 *============================================================================*/

/**
 * Callback type for LLM inference.
 *
 * @param block     The calling block
 * @param prompt    The prompt value (usually string or map)
 * @param context   User-provided context
 * @return          The inference result, or NULL on error
 */
typedef Value *(*InferCallback)(Block *block, Value *prompt, void *context);

/*============================================================================
 * Inference State
 *============================================================================*/

typedef struct InferenceState {
    InferCallback callback;
    void *context;
} InferenceState;

/*============================================================================
 * Inference API
 *============================================================================*/

/**
 * Initialize inference state.
 */
void inference_init(InferenceState *state);

/**
 * Set the inference callback.
 */
void inference_set_callback(InferenceState *state, InferCallback callback, void *context);

/**
 * Execute inference.
 * Returns NULL if no callback is set.
 */
Value *inference_call(InferenceState *state, Block *block, Value *prompt);

#endif /* AGIM_BUILTIN_INFERENCE_H */
