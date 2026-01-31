/*
 * Agim - Hot Code Reloading Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/module.h"
#include "runtime/block.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/*============================================================================
 * Time Helper
 *============================================================================*/

static uint64_t current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/*============================================================================
 * Module Version
 *============================================================================*/

static ModuleVersion *module_version_new(const char *name, Bytecode *code, uint32_t version) {
    ModuleVersion *ver = calloc(1, sizeof(ModuleVersion));
    if (!ver) return NULL;

    ver->name = name ? strdup(name) : NULL;
    ver->version = version;
    ver->code = code ? bytecode_retain(code) : NULL;
    atomic_store(&ver->ref_count, 1);
    ver->loaded_at = current_time_ms();
    ver->migrate_func_index = SIZE_MAX;
    ver->prev_version = NULL;

    return ver;
}

static void module_version_free(ModuleVersion *ver) {
    if (!ver) return;

    free(ver->name);
    if (ver->code) {
        bytecode_release(ver->code);
    }
    free(ver);
}

ModuleVersion *module_version_retain(ModuleVersion *ver) {
    if (ver) {
        atomic_fetch_add(&ver->ref_count, 1);
    }
    return ver;
}

void module_version_release(ModuleVersion *ver) {
    if (!ver) return;

    if (atomic_fetch_sub(&ver->ref_count, 1) == 1) {
        /* Last reference - free the version */
        module_version_free(ver);
    }
}

bool module_version_has_migrate(ModuleVersion *ver) {
    return ver && ver->migrate_func_index != SIZE_MAX;
}

Value *module_version_migrate(ModuleVersion *ver, Value *old_state, uint32_t from_version) {
    if (!ver || !module_version_has_migrate(ver)) {
        return old_state;  /* No migration - keep old state */
    }

    /* TODO: Actually call the migration function
     * This requires creating a VM, loading the code, and calling the function.
     * For now, return old state unchanged. */
    (void)from_version;
    return old_state;
}

/*============================================================================
 * Module
 *============================================================================*/

static Module *module_new(const char *name) {
    Module *mod = calloc(1, sizeof(Module));
    if (!mod) return NULL;

    mod->name = name ? strdup(name) : NULL;
    mod->current = NULL;
    mod->old = NULL;
    mod->blocks = NULL;
    mod->block_count = 0;

    pthread_mutex_init(&mod->lock, NULL);

    return mod;
}

static void module_free(Module *mod) {
    if (!mod) return;

    pthread_mutex_lock(&mod->lock);

    /* Free all versions */
    if (mod->current) {
        module_version_release(mod->current);
    }

    ModuleVersion *old = mod->old;
    while (old) {
        ModuleVersion *prev = old->prev_version;
        module_version_release(old);
        old = prev;
    }

    /* Free block associations */
    ModuleBlock *mb = mod->blocks;
    while (mb) {
        ModuleBlock *next = mb->next;
        free(mb);
        mb = next;
    }

    free(mod->name);
    pthread_mutex_unlock(&mod->lock);
    pthread_mutex_destroy(&mod->lock);
    free(mod);
}

/*============================================================================
 * Module Registry
 *============================================================================*/

ModuleRegistry *module_registry_new(void) {
    ModuleRegistry *reg = calloc(1, sizeof(ModuleRegistry));
    if (!reg) return NULL;

    reg->modules = NULL;
    reg->count = 0;
    reg->capacity = 0;
    pthread_rwlock_init(&reg->lock, NULL);

    return reg;
}

void module_registry_free(ModuleRegistry *reg) {
    if (!reg) return;

    pthread_rwlock_wrlock(&reg->lock);

    for (size_t i = 0; i < reg->count; i++) {
        module_free(reg->modules[i]);
    }
    free(reg->modules);

    pthread_rwlock_unlock(&reg->lock);
    pthread_rwlock_destroy(&reg->lock);
    free(reg);
}

static Module *registry_find(ModuleRegistry *reg, const char *name) {
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->modules[i]->name, name) == 0) {
            return reg->modules[i];
        }
    }
    return NULL;
}

static bool registry_grow(ModuleRegistry *reg) {
    size_t new_capacity = reg->capacity ? reg->capacity * 2 : 8;
    Module **new_modules = realloc(reg->modules, sizeof(Module *) * new_capacity);
    if (!new_modules) return false;

    reg->modules = new_modules;
    reg->capacity = new_capacity;
    return true;
}

ModuleVersion *module_load(ModuleRegistry *reg, const char *name, Bytecode *code) {
    if (!reg || !name || !code) return NULL;

    pthread_rwlock_wrlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    if (!mod) {
        /* Create new module */
        mod = module_new(name);
        if (!mod) {
            pthread_rwlock_unlock(&reg->lock);
            return NULL;
        }

        /* Add to registry */
        if (reg->count >= reg->capacity) {
            if (!registry_grow(reg)) {
                module_free(mod);
                pthread_rwlock_unlock(&reg->lock);
                return NULL;
            }
        }
        reg->modules[reg->count++] = mod;
    }

    pthread_mutex_lock(&mod->lock);
    pthread_rwlock_unlock(&reg->lock);

    /* Determine version number */
    uint32_t version = mod->current ? mod->current->version + 1 : 1;

    /* Create new version */
    ModuleVersion *ver = module_version_new(name, code, version);
    if (!ver) {
        pthread_mutex_unlock(&mod->lock);
        return NULL;
    }

    /* Check for migrate function in bytecode */
    const ToolInfo *tools;
    size_t tool_count;
    tools = bytecode_get_tools(code, &tool_count);
    for (size_t i = 0; i < tool_count; i++) {
        if (strcmp(tools[i].name, "migrate") == 0) {
            ver->migrate_func_index = tools[i].func_index;
            break;
        }
    }

    /* Push old version */
    if (mod->current) {
        mod->current->prev_version = mod->old;
        mod->old = mod->current;
    }

    mod->current = ver;

    pthread_mutex_unlock(&mod->lock);
    return ver;
}

ModuleVersion *module_load_file(ModuleRegistry *reg, const char *name, const char *path) {
    /* TODO: Implement file loading
     * This requires parsing and compiling the source file. */
    (void)reg;
    (void)name;
    (void)path;
    return NULL;
}

ModuleVersion *module_get(ModuleRegistry *reg, const char *name) {
    if (!reg || !name) return NULL;

    pthread_rwlock_rdlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    ModuleVersion *ver = mod ? mod->current : NULL;

    if (ver) {
        module_version_retain(ver);
    }

    pthread_rwlock_unlock(&reg->lock);
    return ver;
}

ModuleVersion *module_get_version(ModuleRegistry *reg, const char *name, uint32_t version) {
    if (!reg || !name) return NULL;

    pthread_rwlock_rdlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    ModuleVersion *ver = NULL;

    if (mod) {
        pthread_mutex_lock(&mod->lock);

        /* Search current and old versions */
        if (mod->current && mod->current->version == version) {
            ver = mod->current;
        } else {
            ModuleVersion *old = mod->old;
            while (old) {
                if (old->version == version) {
                    ver = old;
                    break;
                }
                old = old->prev_version;
            }
        }

        if (ver) {
            module_version_retain(ver);
        }

        pthread_mutex_unlock(&mod->lock);
    }

    pthread_rwlock_unlock(&reg->lock);
    return ver;
}

const Module **module_list(ModuleRegistry *reg, size_t *count) {
    if (!reg || !count) {
        if (count) *count = 0;
        return NULL;
    }

    pthread_rwlock_rdlock(&reg->lock);
    *count = reg->count;
    /* Note: Caller should not free the returned array */
    pthread_rwlock_unlock(&reg->lock);

    return (const Module **)reg->modules;
}

bool module_unload(ModuleRegistry *reg, const char *name) {
    if (!reg || !name) return false;

    pthread_rwlock_wrlock(&reg->lock);

    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->modules[i]->name, name) == 0) {
            Module *mod = reg->modules[i];

            /* Check if any blocks are still using it */
            pthread_mutex_lock(&mod->lock);
            if (mod->block_count > 0) {
                pthread_mutex_unlock(&mod->lock);
                pthread_rwlock_unlock(&reg->lock);
                return false;  /* Can't unload - still in use */
            }
            pthread_mutex_unlock(&mod->lock);

            /* Remove from registry */
            reg->modules[i] = reg->modules[--reg->count];

            module_free(mod);

            pthread_rwlock_unlock(&reg->lock);
            return true;
        }
    }

    pthread_rwlock_unlock(&reg->lock);
    return false;
}

/*============================================================================
 * Hot Reload
 *============================================================================*/

UpgradeConfig upgrade_config_default(void) {
    return (UpgradeConfig){
        .require_migrate = false,
        .rollback_on_error = true,
        .timeout_ms = 5000,
    };
}

bool module_trigger_upgrade(ModuleRegistry *reg, const char *name, const UpgradeConfig *config) {
    if (!reg || !name) return false;

    UpgradeConfig cfg = config ? *config : upgrade_config_default();

    pthread_rwlock_rdlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    if (!mod) {
        pthread_rwlock_unlock(&reg->lock);
        return false;
    }

    pthread_mutex_lock(&mod->lock);
    pthread_rwlock_unlock(&reg->lock);

    /* Check if migration is required but not available */
    if (cfg.require_migrate && mod->current && !module_version_has_migrate(mod->current)) {
        pthread_mutex_unlock(&mod->lock);
        return false;
    }

    /* Mark all blocks for upgrade */
    ModuleBlock *mb = mod->blocks;
    while (mb) {
        mb->pending_upgrade = true;
        mb = mb->next;
    }

    pthread_mutex_unlock(&mod->lock);
    return true;
}

bool module_register_block(ModuleRegistry *reg, const char *name, uint64_t block_pid) {
    if (!reg || !name) return false;

    pthread_rwlock_rdlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    if (!mod) {
        pthread_rwlock_unlock(&reg->lock);
        return false;
    }

    pthread_mutex_lock(&mod->lock);
    pthread_rwlock_unlock(&reg->lock);

    /* Check if already registered */
    ModuleBlock *mb = mod->blocks;
    while (mb) {
        if (mb->block_pid == block_pid) {
            pthread_mutex_unlock(&mod->lock);
            return true;
        }
        mb = mb->next;
    }

    /* Add new registration */
    mb = malloc(sizeof(ModuleBlock));
    if (!mb) {
        pthread_mutex_unlock(&mod->lock);
        return false;
    }

    mb->block_pid = block_pid;
    mb->version = mod->current;
    mb->pending_upgrade = false;
    mb->next = mod->blocks;
    mod->blocks = mb;
    mod->block_count++;

    if (mod->current) {
        atomic_fetch_add(&mod->current->ref_count, 1);
    }

    pthread_mutex_unlock(&mod->lock);
    return true;
}

void module_unregister_block(ModuleRegistry *reg, const char *name, uint64_t block_pid) {
    if (!reg || !name) return;

    pthread_rwlock_rdlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    if (!mod) {
        pthread_rwlock_unlock(&reg->lock);
        return;
    }

    pthread_mutex_lock(&mod->lock);
    pthread_rwlock_unlock(&reg->lock);

    ModuleBlock **pp = &mod->blocks;
    while (*pp) {
        ModuleBlock *mb = *pp;
        if (mb->block_pid == block_pid) {
            *pp = mb->next;
            mod->block_count--;

            if (mb->version) {
                module_version_release(mb->version);
            }

            free(mb);
            pthread_mutex_unlock(&mod->lock);
            return;
        }
        pp = &mb->next;
    }

    pthread_mutex_unlock(&mod->lock);
}

bool module_has_pending_upgrade(ModuleRegistry *reg, const char *name, uint64_t block_pid) {
    if (!reg || !name) return false;

    pthread_rwlock_rdlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    if (!mod) {
        pthread_rwlock_unlock(&reg->lock);
        return false;
    }

    pthread_mutex_lock(&mod->lock);
    pthread_rwlock_unlock(&reg->lock);

    ModuleBlock *mb = mod->blocks;
    while (mb) {
        if (mb->block_pid == block_pid) {
            bool pending = mb->pending_upgrade;
            pthread_mutex_unlock(&mod->lock);
            return pending;
        }
        mb = mb->next;
    }

    pthread_mutex_unlock(&mod->lock);
    return false;
}

bool module_apply_upgrade(ModuleRegistry *reg, const char *name, uint64_t block_pid,
                          Value *old_state, Value **new_state) {
    if (!reg || !name || !new_state) return false;

    pthread_rwlock_rdlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    if (!mod) {
        pthread_rwlock_unlock(&reg->lock);
        return false;
    }

    pthread_mutex_lock(&mod->lock);
    pthread_rwlock_unlock(&reg->lock);

    /* Find the block */
    ModuleBlock *mb = mod->blocks;
    while (mb) {
        if (mb->block_pid == block_pid) {
            break;
        }
        mb = mb->next;
    }

    if (!mb || !mb->pending_upgrade) {
        pthread_mutex_unlock(&mod->lock);
        return false;
    }

    /* Get the old and new versions */
    ModuleVersion *old_ver = mb->version;
    ModuleVersion *new_ver = mod->current;

    if (!new_ver || old_ver == new_ver) {
        mb->pending_upgrade = false;
        pthread_mutex_unlock(&mod->lock);
        return false;
    }

    /* Migrate state */
    uint32_t old_version = old_ver ? old_ver->version : 0;
    *new_state = module_version_migrate(new_ver, old_state, old_version);

    /* Update block's version */
    if (old_ver) {
        module_version_release(old_ver);
    }
    mb->version = module_version_retain(new_ver);
    mb->pending_upgrade = false;

    pthread_mutex_unlock(&mod->lock);
    return true;
}

bool module_rollback(ModuleRegistry *reg, const char *name) {
    if (!reg || !name) return false;

    pthread_rwlock_rdlock(&reg->lock);

    Module *mod = registry_find(reg, name);
    if (!mod) {
        pthread_rwlock_unlock(&reg->lock);
        return false;
    }

    pthread_mutex_lock(&mod->lock);
    pthread_rwlock_unlock(&reg->lock);

    if (!mod->old) {
        pthread_mutex_unlock(&mod->lock);
        return false;  /* No previous version to rollback to */
    }

    /* Swap current and old */
    ModuleVersion *current = mod->current;
    mod->current = mod->old;
    mod->old = current;

    /* Mark all blocks for upgrade (to the rolled-back version) */
    ModuleBlock *mb = mod->blocks;
    while (mb) {
        mb->pending_upgrade = true;
        mb = mb->next;
    }

    pthread_mutex_unlock(&mod->lock);
    return true;
}

/*============================================================================
 * Block Integration
 *============================================================================*/

void module_apply_upgrade_block(Block *block) {
    /* This would be called from safe points in the VM.
     * For now, it's a placeholder that would:
     * 1. Find the module this block uses
     * 2. Call module_apply_upgrade
     * 3. Update the block's bytecode pointer
     * 4. Migrate any state if needed
     */
    (void)block;
}
