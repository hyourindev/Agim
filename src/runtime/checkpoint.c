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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

/*============================================================================
 * Time Helper
 *============================================================================*/

static uint64_t current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/*============================================================================
 * Checkpoint Configuration
 *============================================================================*/

CheckpointConfig checkpoint_config_default(void) {
    return (CheckpointConfig){
        .enabled = false,
        .interval_ms = 0,           /* Manual only */
        .checkpoint_on_exit = false,
        .storage_path = NULL,
        .max_checkpoints = 5,
    };
}

/*============================================================================
 * Checkpoint Lifecycle
 *============================================================================*/

Checkpoint *checkpoint_create(Block *block) {
    if (!block) return NULL;

    Checkpoint *cp = calloc(1, sizeof(Checkpoint));
    if (!cp) return NULL;

    /* Metadata */
    cp->timestamp_ms = current_time_ms();
    cp->checkpoint_id = cp->timestamp_ms;  /* Simple ID generation */
    cp->version = CHECKPOINT_VERSION;

    /* Block identity */
    cp->original_pid = block->pid;
    cp->name = block->name ? strdup(block->name) : NULL;

    /* Initialize serialization buffers */
    serial_buffer_init(&cp->stack_state);
    serial_buffer_init(&cp->globals_state);
    serial_buffer_init(&cp->mailbox_state);

    /* Serialize globals */
    if (block->vm && block->vm->globals) {
        SerializeResult res = serialize_value(block->vm->globals, &cp->globals_state);
        if (res != SERIALIZE_OK) {
            checkpoint_free(cp);
            return NULL;
        }
    }

    /* Serialize pending mailbox messages */
    /* Note: This is a simplified implementation - in production we'd need
     * to handle the lock-free queue more carefully */
    cp->mailbox_count = mailbox_count(&block->mailbox);

    /* Copy links */
    if (block->link_count > 0) {
        cp->links = malloc(sizeof(Pid) * block->link_count);
        if (cp->links) {
            memcpy(cp->links, block->links, sizeof(Pid) * block->link_count);
            cp->link_count = block->link_count;
        }
    }

    cp->parent = block->parent;

    /* Capabilities */
    cp->capabilities = block->capabilities;

    /* Counters */
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

    /* For now, we can't fully restore execution state because we don't
     * serialize the bytecode or stack frames. This is a simplified version
     * that creates a new block with restored globals and links. */

    /* In a full implementation, we would:
     * 1. Recreate the bytecode from the original source or serialized form
     * 2. Restore the stack and call frames
     * 3. Restore the instruction pointer
     * 4. Restore pending messages
     */

    /* For now, return PID_INVALID to indicate restore is not implemented */
    return PID_INVALID;
}

/*============================================================================
 * Checkpoint Serialization
 *============================================================================*/

bool checkpoint_serialize(Checkpoint *cp, SerialBuffer *buf) {
    if (!cp || !buf) return false;

    /* Magic number */
    if (!serial_write_u32(buf, CHECKPOINT_MAGIC)) return false;

    /* Version */
    if (!serial_write_u32(buf, cp->version)) return false;

    /* Metadata */
    if (!serial_write_u64(buf, cp->timestamp_ms)) return false;
    if (!serial_write_u64(buf, cp->checkpoint_id)) return false;

    /* Block identity */
    if (!serial_write_u64(buf, cp->original_pid)) return false;
    if (!serial_write_string(buf, cp->name)) return false;

    /* Globals state */
    if (!serial_write_u32(buf, (uint32_t)cp->globals_state.size)) return false;
    if (cp->globals_state.size > 0) {
        if (!serial_write_bytes(buf, cp->globals_state.data, cp->globals_state.size)) return false;
    }

    /* Links */
    if (!serial_write_u32(buf, (uint32_t)cp->link_count)) return false;
    for (size_t i = 0; i < cp->link_count; i++) {
        if (!serial_write_u64(buf, cp->links[i])) return false;
    }

    /* Parent */
    if (!serial_write_u64(buf, cp->parent)) return false;

    /* Capabilities */
    if (!serial_write_u32(buf, cp->capabilities)) return false;

    /* Counters */
    if (!serial_write_u64(buf, cp->reductions)) return false;
    if (!serial_write_u64(buf, cp->messages_sent)) return false;
    if (!serial_write_u64(buf, cp->messages_received)) return false;

    return true;
}

Checkpoint *checkpoint_deserialize(SerialBuffer *buf) {
    if (!buf) return NULL;

    Checkpoint *cp = calloc(1, sizeof(Checkpoint));
    if (!cp) return NULL;

    serial_buffer_init(&cp->stack_state);
    serial_buffer_init(&cp->globals_state);
    serial_buffer_init(&cp->mailbox_state);

    /* Magic number */
    uint32_t magic;
    if (!serial_read_u32(buf, &magic) || magic != CHECKPOINT_MAGIC) {
        checkpoint_free(cp);
        return NULL;
    }

    /* Version */
    if (!serial_read_u32(buf, &cp->version) || cp->version > CHECKPOINT_VERSION) {
        checkpoint_free(cp);
        return NULL;
    }

    /* Metadata */
    if (!serial_read_u64(buf, &cp->timestamp_ms)) goto error;
    if (!serial_read_u64(buf, &cp->checkpoint_id)) goto error;

    /* Block identity */
    if (!serial_read_u64(buf, &cp->original_pid)) goto error;
    cp->name = serial_read_string(buf);

    /* Globals state */
    uint32_t globals_size;
    if (!serial_read_u32(buf, &globals_size)) goto error;
    if (globals_size > 0) {
        if (!serial_buffer_ensure(&cp->globals_state, globals_size)) goto error;
        if (!serial_read_bytes(buf, cp->globals_state.data, globals_size)) goto error;
        cp->globals_state.size = globals_size;
    }

    /* Links */
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

    /* Parent */
    if (!serial_read_u64(buf, &cp->parent)) goto error;

    /* Capabilities */
    if (!serial_read_u32(buf, &cp->capabilities)) goto error;

    /* Counters */
    if (!serial_read_u64(buf, &cp->reductions)) goto error;
    if (!serial_read_u64(buf, &cp->messages_sent)) goto error;
    if (!serial_read_u64(buf, &cp->messages_received)) goto error;

    return cp;

error:
    checkpoint_free(cp);
    return NULL;
}

/*============================================================================
 * File I/O
 *============================================================================*/

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

    size_t written = fwrite(buf.data, 1, buf.size, f);
    fclose(f);

    serial_buffer_free(&buf);
    return written == buf.size;
}

Checkpoint *checkpoint_load(const char *path) {
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    /* Read file */
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

    /* Deserialize */
    SerialBuffer buf;
    serial_buffer_init_data(&buf, data, (size_t)size);

    Checkpoint *cp = checkpoint_deserialize(&buf);

    free(data);
    return cp;
}

/*============================================================================
 * Checkpoint Manager
 *============================================================================*/

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

    mgr->next_checkpoint_id = (uint64_t)current_time_ms();

    /* Create storage directory if it doesn't exist */
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

    /* Assign unique ID */
    cp->checkpoint_id = mgr->next_checkpoint_id++;

    /* Save to storage if configured */
    if (mgr->storage_path && block->name) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s_%lu.checkpoint",
                 mgr->storage_path, block->name, (unsigned long)cp->checkpoint_id);
        checkpoint_save(cp, path);

        /* Cleanup old checkpoints */
        checkpoint_manager_cleanup(mgr, block->name);
    }

    return cp;
}

uint64_t *checkpoint_manager_list(CheckpointManager *mgr, const char *block_name,
                                   size_t *count) {
    if (!mgr || !block_name || !count) {
        if (count) *count = 0;
        return NULL;
    }

    /* TODO: Implement directory scanning for checkpoint files */
    *count = 0;
    return NULL;
}

Checkpoint *checkpoint_manager_get(CheckpointManager *mgr, uint64_t checkpoint_id) {
    if (!mgr || !mgr->storage_path) return NULL;

    /* TODO: Implement checkpoint lookup by ID */
    (void)checkpoint_id;
    return NULL;
}

void checkpoint_manager_cleanup(CheckpointManager *mgr, const char *block_name) {
    if (!mgr || !block_name || mgr->config.max_checkpoints == 0) return;

    /* TODO: Implement cleanup of old checkpoints */
}
