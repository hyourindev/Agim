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

/* Module Types */

typedef struct Module {
    char *path;
    char *source;
    AstNode *ast;
    char **exports;
    size_t export_count;
    bool is_compiled;
} Module;

typedef struct ModuleCache {
    Module **modules;
    size_t count;
    size_t capacity;
    char **loading;
    size_t loading_count;
    size_t loading_capacity;
} ModuleCache;

/* Module Cache API */

ModuleCache *module_cache_new(void);
void module_cache_free(ModuleCache *cache);
Module *module_cache_find(ModuleCache *cache, const char *path);
void module_cache_add(ModuleCache *cache, Module *mod);

/* Module Loading API */

Module *module_load(const char *path, const char *base_path, ModuleCache *cache, char **error);
void module_free(Module *mod);
char *module_resolve_path(const char *path, const char *base_path);
bool module_is_loading(ModuleCache *cache, const char *path);
void module_loading_push(ModuleCache *cache, const char *path);
void module_loading_pop(ModuleCache *cache);

#endif /* AGIM_LANG_MODULE_H */
