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

void agim_set_strict_types(bool strict);
Bytecode *agim_compile(const char *source, const char **error);
Bytecode *agim_compile_file(const char *path, const char **error);
void agim_error_free(const char *error);

typedef enum AgimResult {
    AGIM_OK,
    AGIM_ERROR_COMPILE,
    AGIM_ERROR_TYPE,
    AGIM_ERROR_RUNTIME,
} AgimResult;

AgimResult agim_run(const char *source);
AgimResult agim_run_with_result(const char *source, Value **result, const char **error);

#endif /* AGIM_LANG_H */
