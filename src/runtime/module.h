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

/* Module Version */

typedef struct ModuleVersion {
    char *name;
    uint32_t version;
    Bytecode *code;
    _Atomic(size_t) ref_count;
    uint64_t loaded_at;
    size_t migrate_func_index;
    struct ModuleVersion *prev_version;
} ModuleVersion;

/* Module */

typedef struct Module {
    char *name;
    ModuleVersion *current;
    ModuleVersion *old;
    struct ModuleBlock *blocks;
    size_t block_count;
    pthread_mutex_t lock;
} Module;

typedef struct ModuleBlock {
    uint64_t block_pid;
    ModuleVersion *version;
    bool pending_upgrade;
    struct ModuleBlock *next;
} ModuleBlock;

/* Module Registry */

typedef struct ModuleRegistry {
    Module **modules;
    size_t count;
    size_t capacity;
    pthread_rwlock_t lock;
} ModuleRegistry;

/* Lifecycle */

ModuleRegistry *module_registry_new(void);
void module_registry_free(ModuleRegistry *reg);
ModuleVersion *module_load(ModuleRegistry *reg, const char *name, Bytecode *code);
ModuleVersion *module_load_file(ModuleRegistry *reg, const char *name, const char *path);
ModuleVersion *module_get(ModuleRegistry *reg, const char *name);
ModuleVersion *module_get_version(ModuleRegistry *reg, const char *name, uint32_t version);
const Module **module_list(ModuleRegistry *reg, size_t *count);
bool module_unload(ModuleRegistry *reg, const char *name);

/* Hot Reload */

typedef struct UpgradeConfig {
    bool require_migrate;
    bool rollback_on_error;
    uint32_t timeout_ms;
} UpgradeConfig;

UpgradeConfig upgrade_config_default(void);
bool module_trigger_upgrade(ModuleRegistry *reg, const char *name, const UpgradeConfig *config);
bool module_register_block(ModuleRegistry *reg, const char *name, uint64_t block_pid);
void module_unregister_block(ModuleRegistry *reg, const char *name, uint64_t block_pid);
bool module_has_pending_upgrade(ModuleRegistry *reg, const char *name, uint64_t block_pid);
bool module_apply_upgrade(ModuleRegistry *reg, const char *name, uint64_t block_pid,
                          struct Value *old_state, struct Value **new_state);
bool module_rollback(ModuleRegistry *reg, const char *name);

/* Module Version API */

ModuleVersion *module_version_retain(ModuleVersion *ver);
void module_version_release(ModuleVersion *ver);
bool module_version_has_migrate(ModuleVersion *ver);
struct Value *module_version_migrate(ModuleVersion *ver, struct Value *old_state,
                                      uint32_t from_version);

/* Safe Point Macro */

#define MODULE_UPGRADE_POINT(block) do { \
    if ((block)->pending_upgrade) { \
        module_apply_upgrade_block(block); \
    } \
} while(0)

struct Block;
void module_registry_set_global(ModuleRegistry *reg);
ModuleRegistry *module_registry_get_global(void);
void module_apply_upgrade_block(struct Block *block);

#endif /* AGIM_RUNTIME_MODULE_H */
