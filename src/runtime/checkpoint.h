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

/* Forward declarations */
typedef struct Block Block;
typedef struct Scheduler Scheduler;

/*============================================================================
 * Checkpoint Structure
 *============================================================================*/

/**
 * Checkpoint of a block's state.
 */
typedef struct Checkpoint {
    /* Metadata */
    uint64_t timestamp_ms;      /* When checkpoint was created */
    uint64_t checkpoint_id;     /* Unique identifier */
    uint32_t version;           /* Checkpoint format version */

    /* Block identity */
    Pid original_pid;           /* PID when checkpoint was created */
    char *name;                 /* Block name */

    /* Execution state */
    SerialBuffer stack_state;   /* Serialized stack */
    SerialBuffer globals_state; /* Serialized globals */
    size_t ip_offset;           /* Instruction pointer offset */
    int frame_count;            /* Number of call frames */

    /* Mailbox state */
    SerialBuffer mailbox_state; /* Serialized pending messages */
    size_t mailbox_count;       /* Number of messages */

    /* Linking state */
    Pid *links;                 /* Linked PIDs */
    size_t link_count;

    Pid parent;                 /* Parent PID */

    /* Capabilities */
    uint32_t capabilities;      /* Capability set */

    /* Resource counters */
    size_t reductions;
    size_t messages_sent;
    size_t messages_received;
} Checkpoint;

/*============================================================================
 * Checkpoint API
 *============================================================================*/

/**
 * Create a checkpoint of a block's current state.
 * Returns NULL on failure.
 */
Checkpoint *checkpoint_create(Block *block);

/**
 * Free a checkpoint and all its resources.
 */
void checkpoint_free(Checkpoint *cp);

/**
 * Restore a block from a checkpoint.
 * Creates a new block with the checkpointed state.
 * Returns the new block's PID, or PID_INVALID on failure.
 */
Pid checkpoint_restore(Checkpoint *cp, Scheduler *sched);

/**
 * Serialize a checkpoint to a buffer.
 */
bool checkpoint_serialize(Checkpoint *cp, SerialBuffer *buf);

/**
 * Deserialize a checkpoint from a buffer.
 */
Checkpoint *checkpoint_deserialize(SerialBuffer *buf);

/**
 * Save a checkpoint to a file.
 */
bool checkpoint_save(Checkpoint *cp, const char *path);

/**
 * Load a checkpoint from a file.
 */
Checkpoint *checkpoint_load(const char *path);

/*============================================================================
 * Checkpoint Manager
 *============================================================================*/

/**
 * Configuration for automatic checkpointing.
 */
typedef struct CheckpointConfig {
    bool enabled;               /* Whether checkpointing is enabled */
    uint64_t interval_ms;       /* Auto-checkpoint interval (0 = manual only) */
    bool checkpoint_on_exit;    /* Checkpoint when block exits normally */
    char *storage_path;         /* Directory for checkpoint files */
    size_t max_checkpoints;     /* Max checkpoints to keep per block (0 = unlimited) */
} CheckpointConfig;

/**
 * Get default checkpoint configuration.
 */
CheckpointConfig checkpoint_config_default(void);

/**
 * Checkpoint manager for a scheduler.
 */
typedef struct CheckpointManager {
    CheckpointConfig config;
    char *storage_path;
    uint64_t next_checkpoint_id;
} CheckpointManager;

/**
 * Create a checkpoint manager.
 */
CheckpointManager *checkpoint_manager_new(const CheckpointConfig *config);

/**
 * Free a checkpoint manager.
 */
void checkpoint_manager_free(CheckpointManager *mgr);

/**
 * Trigger a checkpoint for a block.
 */
Checkpoint *checkpoint_manager_checkpoint(CheckpointManager *mgr, Block *block);

/**
 * List available checkpoints for a block (by name).
 */
uint64_t *checkpoint_manager_list(CheckpointManager *mgr, const char *block_name,
                                   size_t *count);

/**
 * Get a specific checkpoint.
 */
Checkpoint *checkpoint_manager_get(CheckpointManager *mgr, uint64_t checkpoint_id);

/**
 * Delete old checkpoints to stay within limit.
 */
void checkpoint_manager_cleanup(CheckpointManager *mgr, const char *block_name);

/*============================================================================
 * Checkpoint Format Version
 *============================================================================*/

#define CHECKPOINT_VERSION 1
#define CHECKPOINT_MAGIC 0x41474D43  /* "AGMC" */

#endif /* AGIM_RUNTIME_CHECKPOINT_H */
