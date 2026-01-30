/*
 * Agim - Sandbox for File System Access
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 *
 * Provides path validation to prevent path traversal attacks.
 * All file operations should go through sandbox checks.
 */

#ifndef AGIM_VM_SANDBOX_H
#define AGIM_VM_SANDBOX_H

#include <stdbool.h>
#include <stddef.h>

/*============================================================================
 * Sandbox Configuration
 *============================================================================*/

typedef struct Sandbox {
    /* Directories allowed for reading */
    char **allowed_read_dirs;
    size_t read_count;
    size_t read_capacity;

    /* Directories allowed for writing */
    char **allowed_write_dirs;
    size_t write_count;
    size_t write_capacity;

    /* If true, bypass all sandbox checks (for trusted code) */
    bool allow_all;

    /* If true, allow reading from current working directory */
    bool allow_cwd_read;

    /* If true, allow writing to current working directory */
    bool allow_cwd_write;
} Sandbox;

/*============================================================================
 * Sandbox Lifecycle
 *============================================================================*/

/**
 * Create a new sandbox with default restrictions.
 * By default, no paths are allowed.
 */
Sandbox *sandbox_new(void);

/**
 * Create a sandbox that allows all operations (for trusted code).
 */
Sandbox *sandbox_new_permissive(void);

/**
 * Free a sandbox and all associated resources.
 */
void sandbox_free(Sandbox *sandbox);

/*============================================================================
 * Sandbox Configuration
 *============================================================================*/

/**
 * Add a directory that can be read from.
 * The path is canonicalized and stored.
 * Returns true on success.
 */
bool sandbox_allow_read(Sandbox *sandbox, const char *path);

/**
 * Add a directory that can be written to.
 * The path is canonicalized and stored.
 * Returns true on success.
 */
bool sandbox_allow_write(Sandbox *sandbox, const char *path);

/**
 * Allow reading/writing from/to the current working directory.
 */
void sandbox_allow_cwd(Sandbox *sandbox, bool read, bool write);

/**
 * Disable all sandbox checks (for trusted code).
 */
void sandbox_disable(Sandbox *sandbox);

/**
 * Enable sandbox checks.
 */
void sandbox_enable(Sandbox *sandbox);

/*============================================================================
 * Path Validation
 *============================================================================*/

/**
 * Check if a path can be read according to sandbox rules.
 * This resolves the path, follows symlinks, and checks against allowed dirs.
 * Returns true if the path is allowed.
 */
bool sandbox_check_read(Sandbox *sandbox, const char *path);

/**
 * Check if a path can be written according to sandbox rules.
 * This resolves the path, follows symlinks, and checks against allowed dirs.
 * Returns true if the path is allowed.
 */
bool sandbox_check_write(Sandbox *sandbox, const char *path);

/**
 * Resolve a path and validate it for reading.
 * Returns the canonicalized path if allowed, NULL otherwise.
 * The returned string must be freed by the caller.
 */
char *sandbox_resolve_read(Sandbox *sandbox, const char *path);

/**
 * Resolve a path and validate it for writing.
 * Returns the canonicalized path if allowed, NULL otherwise.
 * The returned string must be freed by the caller.
 */
char *sandbox_resolve_write(Sandbox *sandbox, const char *path);

/*============================================================================
 * Path Utilities
 *============================================================================*/

/**
 * Canonicalize a path (resolve symlinks, remove . and ..).
 * Returns NULL on error. The returned string must be freed.
 */
char *sandbox_canonicalize(const char *path);

/**
 * Check if child_path is within parent_path.
 * Both paths should already be canonicalized.
 */
bool sandbox_path_within(const char *parent_path, const char *child_path);

/**
 * Get the current working directory.
 * Returns NULL on error. The returned string must be freed.
 */
char *sandbox_getcwd(void);

/*============================================================================
 * Global Sandbox
 *============================================================================*/

/**
 * Get the global sandbox instance (lazily initialized).
 * The global sandbox is used by VM file operations.
 */
Sandbox *sandbox_global(void);

/**
 * Set the global sandbox instance.
 * Takes ownership of the sandbox.
 */
void sandbox_set_global(Sandbox *sandbox);

#endif /* AGIM_VM_SANDBOX_H */
