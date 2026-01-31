/*
 * Agim - Sandbox for File System Access
 *
 * Path validation to prevent path traversal attacks.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_SANDBOX_H
#define AGIM_VM_SANDBOX_H

#include <stdbool.h>
#include <stddef.h>

/* Sandbox Configuration */

typedef struct Sandbox {
    char **allowed_read_dirs;
    size_t read_count;
    size_t read_capacity;

    char **allowed_write_dirs;
    size_t write_count;
    size_t write_capacity;

    bool allow_all;
    bool allow_cwd_read;
    bool allow_cwd_write;
} Sandbox;

/* Sandbox Lifecycle */

Sandbox *sandbox_new(void);
Sandbox *sandbox_new_permissive(void);
void sandbox_free(Sandbox *sandbox);

/* Sandbox Configuration */

bool sandbox_allow_read(Sandbox *sandbox, const char *path);
bool sandbox_allow_write(Sandbox *sandbox, const char *path);
void sandbox_allow_cwd(Sandbox *sandbox, bool read, bool write);
void sandbox_disable(Sandbox *sandbox);
void sandbox_enable(Sandbox *sandbox);

/* Path Validation */

bool sandbox_check_read(Sandbox *sandbox, const char *path);
bool sandbox_check_write(Sandbox *sandbox, const char *path);
char *sandbox_resolve_read(Sandbox *sandbox, const char *path);
char *sandbox_resolve_write(Sandbox *sandbox, const char *path);

/* Path Utilities */

char *sandbox_canonicalize(const char *path);
bool sandbox_path_within(const char *parent_path, const char *child_path);
char *sandbox_getcwd(void);

/* Global Sandbox */

Sandbox *sandbox_global(void);
void sandbox_set_global(Sandbox *sandbox);

#endif /* AGIM_VM_SANDBOX_H */
