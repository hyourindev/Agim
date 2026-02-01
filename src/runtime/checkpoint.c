/*
 * Agim - Block Checkpointing Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/checkpoint.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "runtime/timer.h"
#include "debug/log.h"

#include <stdatomic.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>

/*
 * Configuration
 */

CheckpointConfig checkpoint_config_default(void) {
    return (CheckpointConfig){
        .enabled = false,
        .interval_ms = 0,
        .checkpoint_on_exit = false,
        .storage_path = NULL,
        .max_checkpoints = 5,
    };
}

/* Lifecycle */

Checkpoint *checkpoint_create(Block *block) {
    if (!block) return NULL;

    Checkpoint *cp = calloc(1, sizeof(Checkpoint));
    if (!cp) {
        LOG_ERROR("checkpoint: failed to allocate Checkpoint");
        return NULL;
    }

    cp->timestamp_ms = timer_current_time_ms();
    cp->checkpoint_id = cp->timestamp_ms;
    cp->version = CHECKPOINT_VERSION;

    cp->original_pid = block->pid;
    cp->name = block->name ? strdup(block->name) : NULL;

    serial_buffer_init(&cp->stack_state);
    serial_buffer_init(&cp->globals_state);
    serial_buffer_init(&cp->mailbox_state);

    if (block->vm && block->vm->globals) {
        SerializeResult res = serialize_value(block->vm->globals, &cp->globals_state);
        if (res != SERIALIZE_OK) {
            LOG_ERROR("checkpoint: failed to serialize globals for block %lu", block->pid);
            checkpoint_free(cp);
            return NULL;
        }
    }

    cp->mailbox_count = mailbox_count(&block->mailbox);

    if (block->link_count > 0) {
        cp->links = malloc(sizeof(Pid) * block->link_count);
        if (cp->links) {
            memcpy(cp->links, block->links, sizeof(Pid) * block->link_count);
            cp->link_count = block->link_count;
        }
    }

    cp->parent = block->parent;
    cp->capabilities = block->capabilities;

    cp->reductions = block->counters.reductions;
    cp->messages_sent = block->counters.messages_sent;
    cp->messages_received = block->counters.messages_received;

    return cp;
}

void checkpoint_free(Checkpoint *cp) {
    if (!cp) return;

    free(cp->name);
    serial_buffer_free(&cp->stack_state);
    serial_buffer_free(&cp->globals_state);
    serial_buffer_free(&cp->mailbox_state);
    free(cp->links);

    free(cp);
}

Pid checkpoint_restore(Checkpoint *cp, Scheduler *sched) {
    if (!cp || !sched) return PID_INVALID;

    BlockLimits limits = block_limits_default();
    Pid new_pid = atomic_fetch_add(&sched->next_pid, 1);

    Block *block = block_new(new_pid, cp->name, &limits);
    if (!block) return PID_INVALID;

    block->capabilities = cp->capabilities;
    block->parent = cp->parent;

    if (cp->globals_state.size > 0) {
        cp->globals_state.read_pos = 0;
        SerializeResult res = SERIALIZE_OK;
        Value *globals = deserialize_value(&cp->globals_state, &res);
        if (globals && res == SERIALIZE_OK && block->vm) {
            if (block->vm->globals) {
                value_free(block->vm->globals);
            }
            block->vm->globals = globals;
        }
    }

    for (size_t i = 0; i < cp->link_count; i++) {
        block_link(block, cp->links[i]);
    }

    block->counters.reductions = cp->reductions;
    block->counters.messages_sent = cp->messages_sent;
    block->counters.messages_received = cp->messages_received;

    atomic_store(&block->state, BLOCK_WAITING);

    block->vm->scheduler = sched;

    if (!scheduler_register_block(sched, block)) {
        block_free(block);
        return PID_INVALID;
    }

    return new_pid;
}

/* Serialization */

bool checkpoint_serialize(Checkpoint *cp, SerialBuffer *buf) {
    if (!cp || !buf) return false;

    if (!serial_write_u32(buf, CHECKPOINT_MAGIC)) return false;
    if (!serial_write_u32(buf, cp->version)) return false;

    if (!serial_write_u64(buf, cp->timestamp_ms)) return false;
    if (!serial_write_u64(buf, cp->checkpoint_id)) return false;

    if (!serial_write_u64(buf, cp->original_pid)) return false;
    if (!serial_write_string(buf, cp->name)) return false;

    if (!serial_write_u32(buf, (uint32_t)cp->globals_state.size)) return false;
    if (cp->globals_state.size > 0) {
        if (!serial_write_bytes(buf, cp->globals_state.data, cp->globals_state.size)) return false;
    }

    if (!serial_write_u32(buf, (uint32_t)cp->link_count)) return false;
    for (size_t i = 0; i < cp->link_count; i++) {
        if (!serial_write_u64(buf, cp->links[i])) return false;
    }

    if (!serial_write_u64(buf, cp->parent)) return false;
    if (!serial_write_u32(buf, cp->capabilities)) return false;

    if (!serial_write_u64(buf, cp->reductions)) return false;
    if (!serial_write_u64(buf, cp->messages_sent)) return false;
    if (!serial_write_u64(buf, cp->messages_received)) return false;

    if (!serial_write_u32(buf, (uint32_t)cp->mailbox_count)) return false;

    return true;
}

Checkpoint *checkpoint_deserialize(SerialBuffer *buf) {
    if (!buf) return NULL;

    Checkpoint *cp = calloc(1, sizeof(Checkpoint));
    if (!cp) return NULL;

    serial_buffer_init(&cp->stack_state);
    serial_buffer_init(&cp->globals_state);
    serial_buffer_init(&cp->mailbox_state);

    uint32_t magic;
    if (!serial_read_u32(buf, &magic) || magic != CHECKPOINT_MAGIC) {
        checkpoint_free(cp);
        return NULL;
    }

    if (!serial_read_u32(buf, &cp->version) || cp->version > CHECKPOINT_VERSION) {
        checkpoint_free(cp);
        return NULL;
    }

    if (!serial_read_u64(buf, &cp->timestamp_ms)) goto error;
    if (!serial_read_u64(buf, &cp->checkpoint_id)) goto error;

    if (!serial_read_u64(buf, &cp->original_pid)) goto error;
    cp->name = serial_read_string(buf);

    uint32_t globals_size;
    if (!serial_read_u32(buf, &globals_size)) goto error;
    if (globals_size > 0) {
        if (!serial_buffer_ensure(&cp->globals_state, globals_size)) goto error;
        if (!serial_read_bytes(buf, cp->globals_state.data, globals_size)) goto error;
        cp->globals_state.size = globals_size;
    }

    uint32_t link_count;
    if (!serial_read_u32(buf, &link_count)) goto error;
    if (link_count > 0) {
        cp->links = malloc(sizeof(Pid) * link_count);
        if (!cp->links) goto error;
        for (uint32_t i = 0; i < link_count; i++) {
            if (!serial_read_u64(buf, &cp->links[i])) goto error;
        }
        cp->link_count = link_count;
    }

    if (!serial_read_u64(buf, &cp->parent)) goto error;
    if (!serial_read_u32(buf, &cp->capabilities)) goto error;

    if (!serial_read_u64(buf, &cp->reductions)) goto error;
    if (!serial_read_u64(buf, &cp->messages_sent)) goto error;
    if (!serial_read_u64(buf, &cp->messages_received)) goto error;

    uint32_t mailbox_count;
    if (!serial_read_u32(buf, &mailbox_count)) goto error;
    cp->mailbox_count = mailbox_count;

    return cp;

error:
    checkpoint_free(cp);
    return NULL;
}

/* File I/O */

bool checkpoint_save(Checkpoint *cp, const char *path) {
    if (!cp || !path) return false;

    SerialBuffer buf;
    serial_buffer_init(&buf);

    if (!checkpoint_serialize(cp, &buf)) {
        serial_buffer_free(&buf);
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        serial_buffer_free(&buf);
        return false;
    }

    size_t expected = buf.size;
    size_t written = fwrite(buf.data, 1, expected, f);
    fclose(f);

    serial_buffer_free(&buf);
    return written == expected;
}

Checkpoint *checkpoint_load(const char *path) {
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    uint8_t *data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)size, f);
    fclose(f);

    if (read != (size_t)size) {
        free(data);
        return NULL;
    }

    SerialBuffer buf;
    serial_buffer_init_data(&buf, data, (size_t)size);

    Checkpoint *cp = checkpoint_deserialize(&buf);

    free(data);
    return cp;
}

/* Manager */

CheckpointManager *checkpoint_manager_new(const CheckpointConfig *config) {
    CheckpointManager *mgr = calloc(1, sizeof(CheckpointManager));
    if (!mgr) return NULL;

    if (config) {
        mgr->config = *config;
        if (config->storage_path) {
            mgr->storage_path = strdup(config->storage_path);
        }
    } else {
        mgr->config = checkpoint_config_default();
    }

    mgr->next_checkpoint_id = (uint64_t)timer_current_time_ms();

    if (mgr->storage_path) {
        mkdir(mgr->storage_path, 0755);
    }

    return mgr;
}

void checkpoint_manager_free(CheckpointManager *mgr) {
    if (!mgr) return;
    free(mgr->storage_path);
    free(mgr);
}

Checkpoint *checkpoint_manager_checkpoint(CheckpointManager *mgr, Block *block) {
    if (!mgr || !block) return NULL;

    Checkpoint *cp = checkpoint_create(block);
    if (!cp) return NULL;

    cp->checkpoint_id = mgr->next_checkpoint_id++;

    if (mgr->storage_path && block->name) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s_%lu.checkpoint",
                 mgr->storage_path, block->name, (unsigned long)cp->checkpoint_id);
        checkpoint_save(cp, path);

        checkpoint_manager_cleanup(mgr, block->name);
    }

    return cp;
}

static int compare_uint64(const void *a, const void *b) {
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

uint64_t *checkpoint_manager_list(CheckpointManager *mgr, const char *block_name,
                                   size_t *count) {
    if (!mgr || !block_name || !count) {
        if (count) *count = 0;
        return NULL;
    }

    if (!mgr->storage_path) {
        *count = 0;
        return NULL;
    }

    DIR *dir = opendir(mgr->storage_path);
    if (!dir) {
        *count = 0;
        return NULL;
    }

    size_t prefix_len = strlen(block_name) + 1;
    char *prefix = malloc(prefix_len + 1);
    if (!prefix) {
        closedir(dir);
        *count = 0;
        return NULL;
    }
    snprintf(prefix, prefix_len + 1, "%s_", block_name);

    size_t capacity = 16;
    size_t n = 0;
    uint64_t *ids = malloc(sizeof(uint64_t) * capacity);
    if (!ids) {
        free(prefix);
        closedir(dir);
        *count = 0;
        return NULL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, prefix_len) != 0) continue;

        const char *suffix = strstr(entry->d_name, ".checkpoint");
        if (!suffix || suffix[11] != '\0') continue;

        const char *id_start = entry->d_name + prefix_len;
        char *id_end;
        unsigned long id = strtoul(id_start, &id_end, 10);
        if (id_end != suffix) continue;

        if (n >= capacity) {
            capacity *= 2;
            uint64_t *new_ids = realloc(ids, sizeof(uint64_t) * capacity);
            if (!new_ids) {
                free(ids);
                free(prefix);
                closedir(dir);
                *count = 0;
                return NULL;
            }
            ids = new_ids;
        }
        ids[n++] = (uint64_t)id;
    }

    free(prefix);
    closedir(dir);

    if (n > 1) {
        qsort(ids, n, sizeof(uint64_t), compare_uint64);
    }

    *count = n;
    return ids;
}

Checkpoint *checkpoint_manager_get(CheckpointManager *mgr, uint64_t checkpoint_id) {
    if (!mgr || !mgr->storage_path) return NULL;

    DIR *dir = opendir(mgr->storage_path);
    if (!dir) return NULL;

    char suffix[64];
    snprintf(suffix, sizeof(suffix), "_%lu.checkpoint", (unsigned long)checkpoint_id);
    size_t suffix_len = strlen(suffix);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        if (name_len < suffix_len) continue;

        if (strcmp(entry->d_name + name_len - suffix_len, suffix) == 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", mgr->storage_path, entry->d_name);
            closedir(dir);
            return checkpoint_load(path);
        }
    }

    closedir(dir);
    return NULL;
}

void checkpoint_manager_cleanup(CheckpointManager *mgr, const char *block_name) {
    if (!mgr || !block_name || mgr->config.max_checkpoints == 0) return;
    if (!mgr->storage_path) return;

    size_t count;
    uint64_t *ids = checkpoint_manager_list(mgr, block_name, &count);
    if (!ids || count == 0) {
        free(ids);
        return;
    }

    if (count > mgr->config.max_checkpoints) {
        size_t to_delete = count - mgr->config.max_checkpoints;

        for (size_t i = 0; i < to_delete; i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s_%lu.checkpoint",
                     mgr->storage_path, block_name, (unsigned long)ids[i]);

            unlink(path);
        }
    }

    free(ids);
}
