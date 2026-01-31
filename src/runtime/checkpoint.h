/*
 * Agim - Block Checkpointing
 *
 * Checkpoint and restore block state for persistence and recovery.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_CHECKPOINT_H
#define AGIM_RUNTIME_CHECKPOINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/mailbox.h"
#include "runtime/serialize.h"

typedef struct Block Block;
typedef struct Scheduler Scheduler;

#define CHECKPOINT_VERSION 1
#define CHECKPOINT_MAGIC 0x41474D43

/* Checkpoint Structure */

typedef struct Checkpoint {
    uint64_t timestamp_ms;
    uint64_t checkpoint_id;
    uint32_t version;

    Pid original_pid;
    char *name;

    SerialBuffer stack_state;
    SerialBuffer globals_state;
    size_t ip_offset;
    int frame_count;

    SerialBuffer mailbox_state;
    size_t mailbox_count;

    Pid *links;
    size_t link_count;

    Pid parent;

    uint32_t capabilities;

    size_t reductions;
    size_t messages_sent;
    size_t messages_received;
} Checkpoint;

/* Checkpoint API */

Checkpoint *checkpoint_create(Block *block);
void checkpoint_free(Checkpoint *cp);
Pid checkpoint_restore(Checkpoint *cp, Scheduler *sched);
bool checkpoint_serialize(Checkpoint *cp, SerialBuffer *buf);
Checkpoint *checkpoint_deserialize(SerialBuffer *buf);
bool checkpoint_save(Checkpoint *cp, const char *path);
Checkpoint *checkpoint_load(const char *path);

/* Checkpoint Configuration */

typedef struct CheckpointConfig {
    bool enabled;
    uint64_t interval_ms;
    bool checkpoint_on_exit;
    char *storage_path;
    size_t max_checkpoints;
} CheckpointConfig;

CheckpointConfig checkpoint_config_default(void);

/* Checkpoint Manager */

typedef struct CheckpointManager {
    CheckpointConfig config;
    char *storage_path;
    uint64_t next_checkpoint_id;
} CheckpointManager;

CheckpointManager *checkpoint_manager_new(const CheckpointConfig *config);
void checkpoint_manager_free(CheckpointManager *mgr);
Checkpoint *checkpoint_manager_checkpoint(CheckpointManager *mgr, Block *block);
uint64_t *checkpoint_manager_list(CheckpointManager *mgr, const char *block_name,
                                   size_t *count);
Checkpoint *checkpoint_manager_get(CheckpointManager *mgr, uint64_t checkpoint_id);
void checkpoint_manager_cleanup(CheckpointManager *mgr, const char *block_name);

#endif /* AGIM_RUNTIME_CHECKPOINT_H */
