/*
 * Agim - Module System for Hot Code Reloading
 *
 * Versioned module management enabling live code updates without restart.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_MODULE_H
#define AGIM_RUNTIME_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

#include "vm/bytecode.h"
#include "vm/value.h"

/*============================================================================
 * Module Version
 *============================================================================*/

/**
 * A specific version of a module's code.
 */
typedef struct ModuleVersion {
    char *name;                 /* Module name */
    uint32_t version;           /* Version number */
    Bytecode *code;             /* Compiled bytecode */
    _Atomic(size_t) ref_count;  /* Number of blocks using this version */
    uint64_t loaded_at;         /* Timestamp when loaded */

    /* Migration function for state upgrade */
    size_t migrate_func_index;  /* Index of migrate function (or SIZE_MAX if none) */

    /* Linked list for old versions */
    struct ModuleVersion *prev_version;
} ModuleVersion;

/*============================================================================
 * Module
 *============================================================================*/

/**
 * A module with potentially multiple loaded versions.
 */
typedef struct Module {
    char *name;                 /* Module name */
    ModuleVersion *current;     /* Current (newest) version */
    ModuleVersion *old;         /* Previous version (for rollback/migration) */

    /* Blocks using this module (for upgrade notification) */
    struct ModuleBlock *blocks;
    size_t block_count;

    pthread_mutex_t lock;
} Module;

/**
 * Association between a block and a module version.
 */
typedef struct ModuleBlock {
    uint64_t block_pid;         /* Block PID */
    ModuleVersion *version;     /* Version in use */
    bool pending_upgrade;       /* True if upgrade is pending */
    struct ModuleBlock *next;
} ModuleBlock;

/*============================================================================
 * Module Registry
 *============================================================================*/

/**
 * Global registry of all loaded modules.
 */
typedef struct ModuleRegistry {
    Module **modules;           /* Array of modules */
    size_t count;               /* Number of modules */
    size_t capacity;            /* Allocated capacity */
    pthread_rwlock_t lock;      /* Read-write lock for thread safety */
} ModuleRegistry;

/*============================================================================
 * Module API - Lifecycle
 *============================================================================*/

/**
 * Create a new module registry.
 */
ModuleRegistry *module_registry_new(void);

/**
 * Free the module registry and all modules.
 */
void module_registry_free(ModuleRegistry *reg);

/**
 * Load a new version of a module.
 * If the module already exists, adds a new version.
 * Returns the module version, or NULL on failure.
 */
ModuleVersion *module_load(ModuleRegistry *reg, const char *name, Bytecode *code);

/**
 * Load from a file.
 */
ModuleVersion *module_load_file(ModuleRegistry *reg, const char *name, const char *path);

/**
 * Get the current version of a module.
 */
ModuleVersion *module_get(ModuleRegistry *reg, const char *name);

/**
 * Get a specific version of a module.
 */
ModuleVersion *module_get_version(ModuleRegistry *reg, const char *name, uint32_t version);

/**
 * List all loaded modules.
 */
const Module **module_list(ModuleRegistry *reg, size_t *count);

/**
 * Unload a module (marks for removal when ref_count reaches 0).
 */
bool module_unload(ModuleRegistry *reg, const char *name);

/*============================================================================
 * Module API - Hot Reload
 *============================================================================*/

/**
 * Upgrade configuration.
 */
typedef struct UpgradeConfig {
    bool require_migrate;       /* Fail if no migrate function */
    bool rollback_on_error;     /* Rollback on migration error */
    uint32_t timeout_ms;        /* Upgrade timeout (0 = no timeout) */
} UpgradeConfig;

/**
 * Get default upgrade configuration.
 */
UpgradeConfig upgrade_config_default(void);

/**
 * Trigger upgrade for all blocks using a module.
 * Blocks will be upgraded at the next safe point.
 */
bool module_trigger_upgrade(ModuleRegistry *reg, const char *name, const UpgradeConfig *config);

/**
 * Register a block as using a module version.
 */
bool module_register_block(ModuleRegistry *reg, const char *name, uint64_t block_pid);

/**
 * Unregister a block from using a module.
 */
void module_unregister_block(ModuleRegistry *reg, const char *name, uint64_t block_pid);

/**
 * Check if a block has a pending upgrade.
 */
bool module_has_pending_upgrade(ModuleRegistry *reg, const char *name, uint64_t block_pid);

/**
 * Apply code upgrade to a block.
 * Called at safe points (function calls, loop headers, receive).
 * Returns true if upgrade was applied.
 */
bool module_apply_upgrade(ModuleRegistry *reg, const char *name, uint64_t block_pid,
                          struct Value *old_state, struct Value **new_state);

/**
 * Rollback to previous version.
 */
bool module_rollback(ModuleRegistry *reg, const char *name);

/*============================================================================
 * Module Version API
 *============================================================================*/

/**
 * Increment reference count.
 */
ModuleVersion *module_version_retain(ModuleVersion *ver);

/**
 * Decrement reference count. Frees when zero.
 */
void module_version_release(ModuleVersion *ver);

/**
 * Check if a version has a migration function.
 */
bool module_version_has_migrate(ModuleVersion *ver);

/**
 * Call the migration function.
 */
struct Value *module_version_migrate(ModuleVersion *ver, struct Value *old_state,
                                      uint32_t from_version);

/*============================================================================
 * Safe Point Macros
 *============================================================================*/

/**
 * Macro for inserting upgrade check at safe points.
 * Used in VM at function calls, loop headers, and receive.
 */
#define MODULE_UPGRADE_POINT(block) do { \
    if ((block)->pending_upgrade) { \
        module_apply_upgrade_block(block); \
    } \
} while(0)

/* Forward declaration - actual function in hotreload.c */
struct Block;

/**
 * Apply pending upgrade to a block.
 */
void module_apply_upgrade_block(struct Block *block);

#endif /* AGIM_RUNTIME_MODULE_H */
