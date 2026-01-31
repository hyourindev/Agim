/*
 * Agim - Process Groups
 *
 * Group multiple blocks for broadcast messaging.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_PROCGROUP_H
#define AGIM_RUNTIME_PROCGROUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "runtime/mailbox.h"

/*============================================================================
 * Process Group
 *============================================================================*/

#define GROUP_NAME_MAX 64

/**
 * A named group of block PIDs.
 */
typedef struct ProcessGroup {
    char name[GROUP_NAME_MAX];      /* Group name */
    Pid *members;                   /* Array of member PIDs */
    size_t count;                   /* Number of members */
    size_t capacity;                /* Allocated capacity */
    pthread_mutex_t lock;           /* Lock for thread-safe operations */
    struct ProcessGroup *next;      /* Next group in registry */
} ProcessGroup;

/**
 * Registry of all process groups.
 */
typedef struct ProcessGroupRegistry {
    ProcessGroup *groups;           /* Linked list of groups */
    size_t count;                   /* Number of groups */
    pthread_rwlock_t lock;          /* Read-write lock */
} ProcessGroupRegistry;

/*============================================================================
 * Process Group API - Registry
 *============================================================================*/

/**
 * Create a new process group registry.
 */
ProcessGroupRegistry *procgroup_registry_new(void);

/**
 * Free the process group registry and all groups.
 */
void procgroup_registry_free(ProcessGroupRegistry *reg);

/*============================================================================
 * Process Group API - Groups
 *============================================================================*/

/**
 * Create or get a process group by name.
 * Creates the group if it doesn't exist.
 */
ProcessGroup *procgroup_get_or_create(ProcessGroupRegistry *reg, const char *name);

/**
 * Get a process group by name.
 * Returns NULL if not found.
 */
ProcessGroup *procgroup_get(ProcessGroupRegistry *reg, const char *name);

/**
 * Delete a process group.
 * Returns true if deleted, false if not found.
 */
bool procgroup_delete(ProcessGroupRegistry *reg, const char *name);

/**
 * List all group names.
 * Returns array of strings (caller should free the array but not the strings).
 */
const char **procgroup_list(ProcessGroupRegistry *reg, size_t *count);

/*============================================================================
 * Process Group API - Membership
 *============================================================================*/

/**
 * Join a process group.
 * Returns true on success.
 */
bool procgroup_join(ProcessGroupRegistry *reg, const char *name, Pid pid);

/**
 * Leave a process group.
 */
void procgroup_leave(ProcessGroupRegistry *reg, const char *name, Pid pid);

/**
 * Leave all groups (called when block exits).
 */
void procgroup_leave_all(ProcessGroupRegistry *reg, Pid pid);

/**
 * Check if a PID is member of a group.
 */
bool procgroup_is_member(ProcessGroupRegistry *reg, const char *name, Pid pid);

/**
 * Get all members of a group.
 * Returns a copy of the member array (caller must free).
 */
Pid *procgroup_members(ProcessGroupRegistry *reg, const char *name, size_t *count);

/**
 * Get member count of a group.
 */
size_t procgroup_member_count(ProcessGroupRegistry *reg, const char *name);

/*============================================================================
 * Process Group API - Broadcasting
 *============================================================================*/

/* Forward declaration */
struct Scheduler;
struct Value;

/**
 * Send a message to all members of a group.
 * Returns the number of recipients that received the message.
 */
size_t procgroup_broadcast(ProcessGroupRegistry *reg, struct Scheduler *sched,
                           const char *name, Pid sender, struct Value *message);

/**
 * Send a message to all members except the sender.
 */
size_t procgroup_broadcast_others(ProcessGroupRegistry *reg, struct Scheduler *sched,
                                  const char *name, Pid sender, struct Value *message);

#endif /* AGIM_RUNTIME_PROCGROUP_H */
