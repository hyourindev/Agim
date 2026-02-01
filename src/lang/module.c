/*
 * Agim - Module Loader Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/module.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "util/alloc.h"
#include "debug/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <libgen.h>
#define PATH_SEP '/'
#endif

/* Module Cache Implementation */

ModuleCache *module_cache_new(void) {
    ModuleCache *cache = agim_alloc(sizeof(ModuleCache));
    if (!cache) {
        LOG_ERROR("module: failed to allocate ModuleCache");
        return NULL;
    }
    cache->modules = NULL;
    cache->count = 0;
    cache->capacity = 0;
    cache->loading = NULL;
    cache->loading_count = 0;
    cache->loading_capacity = 0;
    return cache;
}

void module_cache_free(ModuleCache *cache) {
    if (!cache) return;

    for (size_t i = 0; i < cache->count; i++) {
        module_free(cache->modules[i]);
    }
    agim_free(cache->modules);

    for (size_t i = 0; i < cache->loading_count; i++) {
        agim_free(cache->loading[i]);
    }
    agim_free(cache->loading);

    agim_free(cache);
}

Module *module_cache_find(ModuleCache *cache, const char *path) {
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->modules[i]->path, path) == 0) {
            return cache->modules[i];
        }
    }
    return NULL;
}

void module_cache_add(ModuleCache *cache, Module *mod) {
    if (cache->count >= cache->capacity) {
        size_t new_cap = cache->capacity == 0 ? 8 : cache->capacity * 2;
        cache->modules = agim_realloc(cache->modules, sizeof(Module *) * new_cap);
        cache->capacity = new_cap;
    }
    cache->modules[cache->count++] = mod;
}

bool module_is_loading(ModuleCache *cache, const char *path) {
    for (size_t i = 0; i < cache->loading_count; i++) {
        if (strcmp(cache->loading[i], path) == 0) {
            return true;
        }
    }
    return false;
}

void module_loading_push(ModuleCache *cache, const char *path) {
    if (cache->loading_count >= cache->loading_capacity) {
        size_t new_cap = cache->loading_capacity == 0 ? 8 : cache->loading_capacity * 2;
        cache->loading = agim_realloc(cache->loading, sizeof(char *) * new_cap);
        cache->loading_capacity = new_cap;
    }
    cache->loading[cache->loading_count++] = agim_strdup(path);
}

void module_loading_pop(ModuleCache *cache) {
    if (cache->loading_count > 0) {
        cache->loading_count--;
        agim_free(cache->loading[cache->loading_count]);
    }
}

/* Path Resolution */

char *module_resolve_path(const char *path, const char *base_path) {
    /* Security: Reject paths containing ".." to prevent path traversal attacks */
    if (strstr(path, "..") != NULL) {
        LOG_WARN("module: rejecting path with '..': %s", path);
        return NULL;
    }

    /* If path is absolute, reject it for sandboxing unless explicitly allowed */
    if (path[0] == '/' || (strlen(path) > 1 && path[1] == ':')) {
        /* For now, reject absolute paths in sandboxed context */
        LOG_WARN("module: rejecting absolute path: %s", path);
        return NULL;
    }

    /* Relative path - resolve against base_path */
    if (!base_path || base_path[0] == '\0') {
        /* No base path, use current directory */
        char cwd[1024];
#ifdef _WIN32
        if (_getcwd(cwd, sizeof(cwd)) == NULL) {
            return agim_strdup(path);
        }
#else
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            return agim_strdup(path);
        }
#endif
        size_t len = strlen(cwd) + 1 + strlen(path) + 1;
        char *result = agim_alloc(len);
        snprintf(result, len, "%s%c%s", cwd, PATH_SEP, path);
        return result;
    }

    /* Combine base_path directory with relative path */
    char *base_copy = agim_strdup(base_path);

    /* Find last separator to get directory */
    char *last_sep = strrchr(base_copy, PATH_SEP);
#ifndef _WIN32
    /* Also check for forward slash on non-Windows */
    char *last_fwd = strrchr(base_copy, '/');
    if (last_fwd && (!last_sep || last_fwd > last_sep)) {
        last_sep = last_fwd;
    }
#endif

    if (last_sep) {
        last_sep[1] = '\0';  /* Keep the separator */
    } else {
        /* No directory component, use current directory */
        agim_free(base_copy);
        return agim_strdup(path);
    }

    size_t len = strlen(base_copy) + strlen(path) + 1;
    char *result = agim_alloc(len);
    snprintf(result, len, "%s%s", base_copy, path);
    agim_free(base_copy);

    return result;
}

/* File Reading */

static char *read_file(const char *path, char **error) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        size_t len = strlen(path) + 50;
        *error = agim_alloc(len);
        snprintf(*error, len, "cannot open module file: %s", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        size_t len = strlen(path) + 50;
        *error = agim_alloc(len);
        snprintf(*error, len, "cannot read module file size: %s", path);
        return NULL;
    }

    char *source = agim_alloc((size_t)size + 1);
    size_t read = fread(source, 1, (size_t)size, file);
    fclose(file);

    if (read != (size_t)size) {
        agim_free(source);
        size_t len = strlen(path) + 50;
        *error = agim_alloc(len);
        snprintf(*error, len, "cannot read module file: %s", path);
        return NULL;
    }

    source[size] = '\0';
    return source;
}

/* Export Collection */

static void collect_exports(Module *mod) {
    if (!mod->ast || mod->ast->type != NODE_PROGRAM) return;

    /* First pass: count exports */
    size_t count = 0;
    for (size_t i = 0; i < mod->ast->as.program.count; i++) {
        AstNode *decl = mod->ast->as.program.decls[i];
        if (decl->type == NODE_EXPORT) {
            count++;
        }
    }

    if (count == 0) {
        /* No explicit exports - export all top-level fn/tool declarations */
        for (size_t i = 0; i < mod->ast->as.program.count; i++) {
            AstNode *decl = mod->ast->as.program.decls[i];
            if (decl->type == NODE_FN_DECL || decl->type == NODE_TOOL_DECL) {
                count++;
            }
        }

        if (count == 0) return;

        mod->exports = agim_alloc(sizeof(char *) * count);
        mod->export_count = 0;

        for (size_t i = 0; i < mod->ast->as.program.count; i++) {
            AstNode *decl = mod->ast->as.program.decls[i];
            if (decl->type == NODE_FN_DECL || decl->type == NODE_TOOL_DECL) {
                mod->exports[mod->export_count++] = agim_strdup(decl->as.fn_decl.name);
            }
        }
        return;
    }

    /* Has explicit exports */
    mod->exports = agim_alloc(sizeof(char *) * count);
    mod->export_count = 0;

    for (size_t i = 0; i < mod->ast->as.program.count; i++) {
        AstNode *decl = mod->ast->as.program.decls[i];
        if (decl->type == NODE_EXPORT) {
            AstNode *inner = decl->as.export_stmt.decl;
            const char *name = NULL;

            if (inner->type == NODE_FN_DECL || inner->type == NODE_TOOL_DECL) {
                name = inner->as.fn_decl.name;
            } else if (inner->type == NODE_LET || inner->type == NODE_CONST) {
                name = inner->as.var_decl.name;
            }

            if (name) {
                mod->exports[mod->export_count++] = agim_strdup(name);
            }
        }
    }
}

/* Module Loading */

Module *module_load(const char *path, const char *base_path, ModuleCache *cache, char **error) {
    *error = NULL;

    /* Resolve path */
    char *resolved = module_resolve_path(path, base_path);

    /* Check cache first */
    Module *cached = module_cache_find(cache, resolved);
    if (cached) {
        agim_free(resolved);
        return cached;
    }

    /* Check for circular import */
    if (module_is_loading(cache, resolved)) {
        size_t len = strlen(resolved) + 50;
        *error = agim_alloc(len);
        snprintf(*error, len, "circular import detected: %s", resolved);
        agim_free(resolved);
        return NULL;
    }

    /* Mark as loading */
    module_loading_push(cache, resolved);

    /* Read file */
    char *source = read_file(resolved, error);
    if (!source) {
        module_loading_pop(cache);
        agim_free(resolved);
        return NULL;
    }

    /* Parse */
    Lexer *lexer = lexer_new(source);
    Parser *parser = parser_new(lexer);
    AstNode *ast = parser_parse(parser);

    if (!ast) {
        const char *parse_error = parser_error(parser);
        size_t len = strlen(resolved) + strlen(parse_error) + 20;
        *error = agim_alloc(len);
        snprintf(*error, len, "%s: %s", resolved, parse_error);

        parser_free(parser);
        lexer_free(lexer);
        agim_free(source);
        module_loading_pop(cache);
        agim_free(resolved);
        return NULL;
    }

    parser_free(parser);
    lexer_free(lexer);

    /* Create module */
    Module *mod = agim_alloc(sizeof(Module));
    mod->path = resolved;
    mod->source = source;
    mod->ast = ast;
    mod->exports = NULL;
    mod->export_count = 0;
    mod->is_compiled = false;

    /* Collect exports */
    collect_exports(mod);

    /* Add to cache */
    module_cache_add(cache, mod);

    /* Pop from loading stack */
    module_loading_pop(cache);

    return mod;
}

void module_free(Module *mod) {
    if (!mod) return;

    agim_free(mod->path);
    agim_free(mod->source);
    ast_free(mod->ast);

    for (size_t i = 0; i < mod->export_count; i++) {
        agim_free(mod->exports[i]);
    }
    agim_free(mod->exports);

    agim_free(mod);
}
