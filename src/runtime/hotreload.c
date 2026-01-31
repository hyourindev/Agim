/*
 * Agim - Hot Code Reloading Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/module.h"
#include "runtime/block.h"
#include "runtime/timer.h"
#include "vm/vm.h"

#ifdef AGIM_WITH_COMPILER
#include "lang/lexer.h"
#include "lang/parser.h"
#include "lang/compiler.h"
#include "lang/ast.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Module Version */

static ModuleVersion *module_version_new(const char *name, Bytecode *code, uint32_t version) {
    ModuleVersion *ver = calloc(1, sizeof(ModuleVersion));
    if (!ver) return NULL;

    ver->name = name ? strdup(name) : NULL;
    ver->version = version;
    ver->code = code ? bytecode_retain(code) : NULL;
    atomic_store(&ver->ref_count, 1);
    ver->loaded_at = timer_current_time_ms();
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
        module_version_free(ver);
    }
}

bool module_version_has_migrate(ModuleVersion *ver) {
    return ver && ver->migrate_func_index != SIZE_MAX;
}

Value *module_version_migrate(ModuleVersion *ver, Value *old_state, uint32_t from_version) {
    if (!ver || !module_version_has_migrate(ver)) {
        return old_state;
    }

    if (!ver->code) {
        return old_state;
    }

    VM *vm = vm_new();
    if (!vm) {
        return old_state;
    }

    vm_load(vm, ver->code);

    vm_push(vm, old_state ? old_state : value_nil());
    vm_push(vm, value_int((int64_t)from_version));

    VMResult result = vm_run(vm);

    Value *new_state = old_state;
    if (result == VM_OK || result == VM_HALT) {
        Value *stack_result = vm_pop(vm);
        if (stack_result) {
            new_state = value_copy(stack_result);
        }
    }

    vm_free(vm);

    return new_state;
}

/* Module */

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

    if (mod->current) {
        module_version_release(mod->current);
    }

    ModuleVersion *old = mod->old;
    while (old) {
        ModuleVersion *prev = old->prev_version;
        module_version_release(old);
        old = prev;
    }

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

/* Module Registry */

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
        mod = module_new(name);
        if (!mod) {
            pthread_rwlock_unlock(&reg->lock);
            return NULL;
        }

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

    uint32_t version = mod->current ? mod->current->version + 1 : 1;

    ModuleVersion *ver = module_version_new(name, code, version);
    if (!ver) {
        pthread_mutex_unlock(&mod->lock);
        return NULL;
    }

    const ToolInfo *tools;
    size_t tool_count;
    tools = bytecode_get_tools(code, &tool_count);
    for (size_t i = 0; i < tool_count; i++) {
        if (strcmp(tools[i].name, "migrate") == 0) {
            ver->migrate_func_index = tools[i].func_index;
            break;
        }
    }

    if (mod->current) {
        ver->prev_version = mod->current;
        mod->old = mod->current;
    }

    mod->current = ver;

    pthread_mutex_unlock(&mod->lock);
    return ver;
}

#ifdef AGIM_WITH_COMPILER
ModuleVersion *module_load_file(ModuleRegistry *reg, const char *name, const char *path) {
    if (!reg || !name || !path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 10 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    char *source = malloc((size_t)size + 1);
    if (!source) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(source, 1, (size_t)size, f);
    fclose(f);

    if (read != (size_t)size) {
        free(source);
        return NULL;
    }
    source[size] = '\0';

    Lexer *lexer = lexer_new(source);
    if (!lexer) {
        free(source);
        return NULL;
    }

    Parser *parser = parser_new(lexer);
    if (!parser) {
        lexer_free(lexer);
        free(source);
        return NULL;
    }

    AstNode *ast = parser_parse(parser);
    if (!ast) {
        parser_free(parser);
        lexer_free(lexer);
        free(source);
        return NULL;
    }

    Compiler *compiler = compiler_new();
    if (!compiler) {
        ast_free(ast);
        parser_free(parser);
        lexer_free(lexer);
        free(source);
        return NULL;
    }

    compiler_set_source_path(compiler, path);

    Bytecode *code = compiler_compile(compiler, ast);
    if (!code) {
        compiler_free(compiler);
        ast_free(ast);
        parser_free(parser);
        lexer_free(lexer);
        free(source);
        return NULL;
    }

    compiler_free(compiler);
    ast_free(ast);
    parser_free(parser);
    lexer_free(lexer);
    free(source);

    ModuleVersion *ver = module_load(reg, name, code);

    bytecode_release(code);

    return ver;
}
#else
ModuleVersion *module_load_file(ModuleRegistry *reg, const char *name, const char *path) {
    (void)reg; (void)name; (void)path;
    return NULL;  /* Compiler not available */
}
#endif /* AGIM_WITH_COMPILER */

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
    pthread_rwlock_unlock(&reg->lock);

    return (const Module **)reg->modules;
}

bool module_unload(ModuleRegistry *reg, const char *name) {
    if (!reg || !name) return false;

    pthread_rwlock_wrlock(&reg->lock);

    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->modules[i]->name, name) == 0) {
            Module *mod = reg->modules[i];

            pthread_mutex_lock(&mod->lock);
            if (mod->block_count > 0) {
                pthread_mutex_unlock(&mod->lock);
                pthread_rwlock_unlock(&reg->lock);
                return false;
            }
            pthread_mutex_unlock(&mod->lock);

            reg->modules[i] = reg->modules[--reg->count];

            module_free(mod);

            pthread_rwlock_unlock(&reg->lock);
            return true;
        }
    }

    pthread_rwlock_unlock(&reg->lock);
    return false;
}

/* Hot Reload */

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

    if (cfg.require_migrate && mod->current && !module_version_has_migrate(mod->current)) {
        pthread_mutex_unlock(&mod->lock);
        return false;
    }

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

    ModuleBlock *mb = mod->blocks;
    while (mb) {
        if (mb->block_pid == block_pid) {
            pthread_mutex_unlock(&mod->lock);
            return true;
        }
        mb = mb->next;
    }

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

    ModuleVersion *old_ver = mb->version;
    ModuleVersion *new_ver = mod->current;

    if (!new_ver || old_ver == new_ver) {
        mb->pending_upgrade = false;
        pthread_mutex_unlock(&mod->lock);
        return false;
    }

    uint32_t old_version = old_ver ? old_ver->version : 0;
    *new_state = module_version_migrate(new_ver, old_state, old_version);

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

    /* Check if there's a previous version to rollback to */
    if (!mod->current || !mod->current->prev_version) {
        pthread_mutex_unlock(&mod->lock);
        return false;
    }

    /* Rollback to previous version */
    mod->old = mod->current;
    mod->current = mod->current->prev_version;

    /* Mark all blocks for upgrade */
    ModuleBlock *mb = mod->blocks;
    while (mb) {
        mb->pending_upgrade = true;
        mb = mb->next;
    }

    pthread_mutex_unlock(&mod->lock);
    return true;
}

/* Block Integration */

static ModuleRegistry *g_module_registry = NULL;

void module_registry_set_global(ModuleRegistry *reg) {
    g_module_registry = reg;
}

ModuleRegistry *module_registry_get_global(void) {
    return g_module_registry;
}

void module_apply_upgrade_block(Block *block) {
    if (!block || !block->pending_upgrade) {
        return;
    }

    if (!g_module_registry) {
        return;
    }

    if (!block->module_name) {
        block->pending_upgrade = false;
        return;
    }

    Value *old_state = NULL;
    if (block->vm) {
        old_state = block->vm->globals;
    }

    Value *new_state = NULL;
    bool success = module_apply_upgrade(g_module_registry, block->module_name,
                                         block->pid, old_state, &new_state);

    if (success) {
        ModuleVersion *new_ver = module_get(g_module_registry, block->module_name);
        if (new_ver && new_ver->code) {
            block->code = new_ver->code;

            if (block->vm) {
                vm_load(block->vm, new_ver->code);

                if (new_state && new_state != old_state) {
                    block->vm->globals = new_state;
                }
            }
        }
        module_version_release(new_ver);
    }

    block->pending_upgrade = false;
}
