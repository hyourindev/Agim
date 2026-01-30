/*
 * Agim - Public API
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_H
#define AGIM_LANG_H

#include "vm/bytecode.h"
#include <stdbool.h>

/**
 * Enable or disable strict type checking.
 * When enabled, type errors will cause compilation to fail.
 *
 * @param strict  true to enable strict mode, false for gradual typing
 */
void agim_set_strict_types(bool strict);

/**
 * Compile Agim source code to bytecode.
 *
 * @param source  Source code string (null-terminated)
 * @param error   Output parameter for error message (caller must free)
 * @return        Bytecode on success, NULL on error
 */
Bytecode *agim_compile(const char *source, const char **error);

/**
 * Compile Agim source file to bytecode.
 *
 * @param path    Path to source file
 * @param error   Output parameter for error message (caller must free)
 * @return        Bytecode on success, NULL on error
 */
Bytecode *agim_compile_file(const char *path, const char **error);

/**
 * Free error message allocated by agim_compile.
 */
void agim_error_free(const char *error);

/**
 * Result codes for agim_run.
 */
typedef enum AgimResult {
    AGIM_OK,
    AGIM_ERROR_COMPILE,
    AGIM_ERROR_TYPE,
    AGIM_ERROR_RUNTIME,
} AgimResult;

/**
 * Compile and run Agim source code.
 *
 * @param source  Source code string (null-terminated)
 * @return        AGIM_OK on success, error code otherwise
 */
AgimResult agim_run(const char *source);

/**
 * Compile and run Agim source code, returning the result value.
 *
 * @param source  Source code string (null-terminated)
 * @param result  Output parameter for result value (may be NULL)
 * @param error   Output parameter for error message (caller must free)
 * @return        AGIM_OK on success, error code otherwise
 */
AgimResult agim_run_with_result(const char *source, Value **result, const char **error);

#endif /* AGIM_LANG_H */
