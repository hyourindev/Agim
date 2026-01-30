/*
 * Agim - Module Loader
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_MODULE_H
#define AGIM_LANG_MODULE_H

#include "lang/ast.h"
#include <stdbool.h>
#include <stddef.h>

/*============================================================================
 * Module Types
 *============================================================================*/

/**
 * Represents a loaded module.
 */
typedef struct Module {
    char *path;              /* Absolute path to the module file */
    char *source;            /* Source code (owned) */
    AstNode *ast;            /* Parsed AST (owned) */
    char **exports;          /* Names of exported symbols */
    size_t export_count;
    bool is_compiled;        /* Whether compilation has been done */
} Module;

/**
 * Module cache for preventing circular imports and reloading.
 */
typedef struct ModuleCache {
    Module **modules;
    size_t count;
    size_t capacity;
    char **loading;          /* Stack of modules currently being loaded (for cycle detection) */
    size_t loading_count;
    size_t loading_capacity;
} ModuleCache;

/*============================================================================
 * Module Cache API
 *============================================================================*/

/**
 * Create a new module cache.
 */
ModuleCache *module_cache_new(void);

/**
 * Free module cache and all loaded modules.
 */
void module_cache_free(ModuleCache *cache);

/**
 * Find a module by path in the cache.
 * Returns NULL if not found.
 */
Module *module_cache_find(ModuleCache *cache, const char *path);

/**
 * Add a module to the cache.
 */
void module_cache_add(ModuleCache *cache, Module *mod);

/*============================================================================
 * Module Loading API
 *============================================================================*/

/**
 * Load a module from a file path.
 *
 * @param path      Path to the module file (relative or absolute)
 * @param base_path Base path for resolving relative imports (directory of importing file)
 * @param cache     Module cache for deduplication
 * @param error     Output: error message if loading fails (must be freed by caller)
 * @return          Loaded module or NULL on error
 */
Module *module_load(const char *path, const char *base_path, ModuleCache *cache, char **error);

/**
 * Free a module and its resources.
 */
void module_free(Module *mod);

/**
 * Resolve a module path relative to a base path.
 * Returns an allocated string (must be freed by caller).
 */
char *module_resolve_path(const char *path, const char *base_path);

/**
 * Check if a module is currently being loaded (cycle detection).
 */
bool module_is_loading(ModuleCache *cache, const char *path);

/**
 * Push a module path onto the loading stack.
 */
void module_loading_push(ModuleCache *cache, const char *path);

/**
 * Pop a module path from the loading stack.
 */
void module_loading_pop(ModuleCache *cache);

#endif /* AGIM_LANG_MODULE_H */
