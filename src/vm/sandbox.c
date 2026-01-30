/*
 * Agim - Sandbox Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "vm/sandbox.h"
#include "util/alloc.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*============================================================================
 * Global Sandbox
 *============================================================================*/

static Sandbox *g_sandbox = NULL;

Sandbox *sandbox_global(void) {
    if (!g_sandbox) {
        /* Create a permissive sandbox for AI agent workloads */
        g_sandbox = sandbox_new_permissive();
    }
    return g_sandbox;
}

void sandbox_set_global(Sandbox *sandbox) {
    if (g_sandbox) {
        sandbox_free(g_sandbox);
    }
    g_sandbox = sandbox;
}

/*============================================================================
 * Sandbox Lifecycle
 *============================================================================*/

Sandbox *sandbox_new(void) {
    Sandbox *sandbox = agim_alloc(sizeof(Sandbox));
    if (!sandbox) return NULL;

    sandbox->allowed_read_dirs = NULL;
    sandbox->read_count = 0;
    sandbox->read_capacity = 0;

    sandbox->allowed_write_dirs = NULL;
    sandbox->write_count = 0;
    sandbox->write_capacity = 0;

    sandbox->allow_all = false;
    sandbox->allow_cwd_read = false;
    sandbox->allow_cwd_write = false;

    return sandbox;
}

Sandbox *sandbox_new_permissive(void) {
    Sandbox *sandbox = sandbox_new();
    if (sandbox) {
        sandbox->allow_all = true;
    }
    return sandbox;
}

void sandbox_free(Sandbox *sandbox) {
    if (!sandbox) return;

    for (size_t i = 0; i < sandbox->read_count; i++) {
        agim_free(sandbox->allowed_read_dirs[i]);
    }
    agim_free(sandbox->allowed_read_dirs);

    for (size_t i = 0; i < sandbox->write_count; i++) {
        agim_free(sandbox->allowed_write_dirs[i]);
    }
    agim_free(sandbox->allowed_write_dirs);

    agim_free(sandbox);
}

/*============================================================================
 * Sandbox Configuration
 *============================================================================*/

static bool add_to_list(char ***list, size_t *count, size_t *capacity, const char *path) {
    /* Canonicalize the path first */
    char *canonical = sandbox_canonicalize(path);
    if (!canonical) return false;

    /* Grow array if needed */
    if (*count >= *capacity) {
        size_t new_capacity = *capacity == 0 ? 8 : *capacity * 2;
        char **new_list = agim_realloc(*list, sizeof(char *) * new_capacity);
        if (!new_list) {
            free(canonical);
            return false;
        }
        *list = new_list;
        *capacity = new_capacity;
    }

    (*list)[(*count)++] = canonical;
    return true;
}

bool sandbox_allow_read(Sandbox *sandbox, const char *path) {
    if (!sandbox || !path) return false;
    return add_to_list(&sandbox->allowed_read_dirs, &sandbox->read_count,
                       &sandbox->read_capacity, path);
}

bool sandbox_allow_write(Sandbox *sandbox, const char *path) {
    if (!sandbox || !path) return false;
    return add_to_list(&sandbox->allowed_write_dirs, &sandbox->write_count,
                       &sandbox->write_capacity, path);
}

void sandbox_allow_cwd(Sandbox *sandbox, bool read, bool write) {
    if (!sandbox) return;
    sandbox->allow_cwd_read = read;
    sandbox->allow_cwd_write = write;
}

void sandbox_disable(Sandbox *sandbox) {
    if (sandbox) sandbox->allow_all = true;
}

void sandbox_enable(Sandbox *sandbox) {
    if (sandbox) sandbox->allow_all = false;
}

/*============================================================================
 * Path Utilities
 *============================================================================*/

char *sandbox_canonicalize(const char *path) {
    if (!path) return NULL;

    /* Use realpath for existing paths */
    char *resolved = realpath(path, NULL);
    if (resolved) return resolved;

    /* For non-existing paths (e.g., files to create), resolve the parent */
    /* Find the last slash */
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        /* No slash - path is relative to CWD */
        char *cwd = sandbox_getcwd();
        if (!cwd) return NULL;

        size_t cwd_len = strlen(cwd);
        size_t path_len = strlen(path);
        char *result = agim_alloc(cwd_len + 1 + path_len + 1);
        if (!result) {
            free(cwd);
            return NULL;
        }
        snprintf(result, cwd_len + 1 + path_len + 1, "%s/%s", cwd, path);
        free(cwd);
        return result;
    }

    /* Extract parent directory */
    size_t parent_len = (size_t)(last_slash - path);
    if (parent_len == 0) {
        /* Root directory */
        parent_len = 1;
    }

    char *parent = agim_alloc(parent_len + 1);
    if (!parent) return NULL;
    memcpy(parent, path, parent_len);
    parent[parent_len] = '\0';

    /* Resolve the parent */
    char *resolved_parent = realpath(parent, NULL);
    agim_free(parent);
    if (!resolved_parent) return NULL;

    /* Combine resolved parent with filename */
    const char *filename = last_slash + 1;
    size_t resolved_len = strlen(resolved_parent);
    size_t filename_len = strlen(filename);
    char *result = agim_alloc(resolved_len + 1 + filename_len + 1);
    if (!result) {
        free(resolved_parent);
        return NULL;
    }
    snprintf(result, resolved_len + 1 + filename_len + 1, "%s/%s", resolved_parent, filename);
    free(resolved_parent);
    return result;
}

bool sandbox_path_within(const char *parent_path, const char *child_path) {
    if (!parent_path || !child_path) return false;

    size_t parent_len = strlen(parent_path);
    size_t child_len = strlen(child_path);

    /* Child must be at least as long as parent */
    if (child_len < parent_len) return false;

    /* Check prefix match */
    if (strncmp(parent_path, child_path, parent_len) != 0) return false;

    /* Child must either be exactly parent or have a slash after parent */
    if (child_len == parent_len) return true;
    if (child_path[parent_len] == '/') return true;

    /* Handle case where parent doesn't end with slash but child continues */
    /* e.g., parent="/foo" should not match child="/foobar" */
    return false;
}

char *sandbox_getcwd(void) {
    char *cwd = getcwd(NULL, 0);
    if (cwd) return cwd;

    /* Fallback for systems where getcwd(NULL, 0) doesn't work */
    char buffer[PATH_MAX];
    if (getcwd(buffer, PATH_MAX)) {
        return strdup(buffer);
    }
    return NULL;
}

/*============================================================================
 * Path Validation
 *============================================================================*/

static bool check_against_list(char **list, size_t count, const char *canonical_path) {
    for (size_t i = 0; i < count; i++) {
        if (sandbox_path_within(list[i], canonical_path)) {
            return true;
        }
    }
    return false;
}

bool sandbox_check_read(Sandbox *sandbox, const char *path) {
    if (!sandbox) return false;
    if (sandbox->allow_all) return true;
    if (!path) return false;

    char *canonical = sandbox_canonicalize(path);
    if (!canonical) return false;

    bool allowed = false;

    /* Check explicit allow list */
    if (check_against_list(sandbox->allowed_read_dirs, sandbox->read_count, canonical)) {
        allowed = true;
    }

    /* Check CWD if enabled */
    if (!allowed && sandbox->allow_cwd_read) {
        char *cwd = sandbox_getcwd();
        if (cwd) {
            allowed = sandbox_path_within(cwd, canonical);
            free(cwd);
        }
    }

    free(canonical);
    return allowed;
}

bool sandbox_check_write(Sandbox *sandbox, const char *path) {
    if (!sandbox) return false;
    if (sandbox->allow_all) return true;
    if (!path) return false;

    char *canonical = sandbox_canonicalize(path);
    if (!canonical) return false;

    bool allowed = false;

    /* Check explicit allow list */
    if (check_against_list(sandbox->allowed_write_dirs, sandbox->write_count, canonical)) {
        allowed = true;
    }

    /* Check CWD if enabled */
    if (!allowed && sandbox->allow_cwd_write) {
        char *cwd = sandbox_getcwd();
        if (cwd) {
            allowed = sandbox_path_within(cwd, canonical);
            free(cwd);
        }
    }

    free(canonical);
    return allowed;
}

char *sandbox_resolve_read(Sandbox *sandbox, const char *path) {
    if (!sandbox) return NULL;
    if (!path) return NULL;

    char *canonical = sandbox_canonicalize(path);
    if (!canonical) return NULL;

    if (sandbox->allow_all) {
        return canonical;
    }

    /* Check explicit allow list */
    if (check_against_list(sandbox->allowed_read_dirs, sandbox->read_count, canonical)) {
        return canonical;
    }

    /* Check CWD if enabled */
    if (sandbox->allow_cwd_read) {
        char *cwd = sandbox_getcwd();
        if (cwd) {
            if (sandbox_path_within(cwd, canonical)) {
                free(cwd);
                return canonical;
            }
            free(cwd);
        }
    }

    free(canonical);
    return NULL;
}

char *sandbox_resolve_write(Sandbox *sandbox, const char *path) {
    if (!sandbox) return NULL;
    if (!path) return NULL;

    char *canonical = sandbox_canonicalize(path);
    if (!canonical) return NULL;

    if (sandbox->allow_all) {
        return canonical;
    }

    /* Check explicit allow list */
    if (check_against_list(sandbox->allowed_write_dirs, sandbox->write_count, canonical)) {
        return canonical;
    }

    /* Check CWD if enabled */
    if (sandbox->allow_cwd_write) {
        char *cwd = sandbox_getcwd();
        if (cwd) {
            if (sandbox_path_within(cwd, canonical)) {
                free(cwd);
                return canonical;
            }
            free(cwd);
        }
    }

    free(canonical);
    return NULL;
}
