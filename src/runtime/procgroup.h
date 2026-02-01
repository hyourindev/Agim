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

/* Required for pthread_rwlock_t */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "runtime/mailbox.h"

/* Process Group */

#define GROUP_NAME_MAX 64

typedef struct ProcessGroup {
    char name[GROUP_NAME_MAX];
    Pid *members;
    size_t count;
    size_t capacity;
    pthread_mutex_t lock;
    struct ProcessGroup *next;
} ProcessGroup;

typedef struct ProcessGroupRegistry {
    ProcessGroup *groups;
    size_t count;
    pthread_rwlock_t lock;
} ProcessGroupRegistry;

/* Registry */

ProcessGroupRegistry *procgroup_registry_new(void);
void procgroup_registry_free(ProcessGroupRegistry *reg);

/* Groups */

ProcessGroup *procgroup_get_or_create(ProcessGroupRegistry *reg, const char *name);
ProcessGroup *procgroup_get(ProcessGroupRegistry *reg, const char *name);
bool procgroup_delete(ProcessGroupRegistry *reg, const char *name);
const char **procgroup_list(ProcessGroupRegistry *reg, size_t *count);

/* Membership */

bool procgroup_join(ProcessGroupRegistry *reg, const char *name, Pid pid);
void procgroup_leave(ProcessGroupRegistry *reg, const char *name, Pid pid);
void procgroup_leave_all(ProcessGroupRegistry *reg, Pid pid);
bool procgroup_is_member(ProcessGroupRegistry *reg, const char *name, Pid pid);
Pid *procgroup_members(ProcessGroupRegistry *reg, const char *name, size_t *count);
size_t procgroup_member_count(ProcessGroupRegistry *reg, const char *name);

/* Broadcasting */

struct Scheduler;
struct Value;

size_t procgroup_broadcast(ProcessGroupRegistry *reg, struct Scheduler *sched,
                           const char *name, Pid sender, struct Value *message);
size_t procgroup_broadcast_others(ProcessGroupRegistry *reg, struct Scheduler *sched,
                                  const char *name, Pid sender, struct Value *message);

#endif /* AGIM_RUNTIME_PROCGROUP_H */
