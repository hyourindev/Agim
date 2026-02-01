/*
 * Agim - Process Groups Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/procgroup.h"
#include "runtime/scheduler.h"
#include "vm/value.h"
#include "debug/log.h"

#include <stdlib.h>
#include <string.h>

/* Process Group Registry */

ProcessGroupRegistry *procgroup_registry_new(void) {
    ProcessGroupRegistry *reg = calloc(1, sizeof(ProcessGroupRegistry));
    if (!reg) {
        LOG_ERROR("procgroup: failed to allocate ProcessGroupRegistry");
        return NULL;
    }

    reg->groups = NULL;
    reg->count = 0;
    pthread_rwlock_init(&reg->lock, NULL);

    return reg;
}

static void group_free(ProcessGroup *group) {
    if (!group) return;

    pthread_mutex_lock(&group->lock);
    free(group->members);
    pthread_mutex_unlock(&group->lock);
    pthread_mutex_destroy(&group->lock);
    free(group);
}

void procgroup_registry_free(ProcessGroupRegistry *reg) {
    if (!reg) return;

    pthread_rwlock_wrlock(&reg->lock);

    ProcessGroup *group = reg->groups;
    while (group) {
        ProcessGroup *next = group->next;
        group_free(group);
        group = next;
    }

    pthread_rwlock_unlock(&reg->lock);
    pthread_rwlock_destroy(&reg->lock);
    free(reg);
}

/* Process Group Operations */

static ProcessGroup *group_new(const char *name) {
    ProcessGroup *group = calloc(1, sizeof(ProcessGroup));
    if (!group) {
        LOG_ERROR("procgroup: failed to allocate ProcessGroup '%s'", name);
        return NULL;
    }

    strncpy(group->name, name, GROUP_NAME_MAX - 1);
    group->name[GROUP_NAME_MAX - 1] = '\0';

    group->members = NULL;
    group->count = 0;
    group->capacity = 0;
    group->next = NULL;
    pthread_mutex_init(&group->lock, NULL);

    return group;
}

ProcessGroup *procgroup_get_or_create(ProcessGroupRegistry *reg, const char *name) {
    if (!reg || !name) return NULL;

    pthread_rwlock_rdlock(&reg->lock);
    ProcessGroup *group = reg->groups;
    while (group) {
        if (strcmp(group->name, name) == 0) {
            pthread_rwlock_unlock(&reg->lock);
            return group;
        }
        group = group->next;
    }
    pthread_rwlock_unlock(&reg->lock);

    pthread_rwlock_wrlock(&reg->lock);

    group = reg->groups;
    while (group) {
        if (strcmp(group->name, name) == 0) {
            pthread_rwlock_unlock(&reg->lock);
            return group;
        }
        group = group->next;
    }

    group = group_new(name);
    if (!group) {
        pthread_rwlock_unlock(&reg->lock);
        return NULL;
    }

    group->next = reg->groups;
    reg->groups = group;
    reg->count++;

    pthread_rwlock_unlock(&reg->lock);
    return group;
}

ProcessGroup *procgroup_get(ProcessGroupRegistry *reg, const char *name) {
    if (!reg || !name) return NULL;

    pthread_rwlock_rdlock(&reg->lock);

    ProcessGroup *group = reg->groups;
    while (group) {
        if (strcmp(group->name, name) == 0) {
            pthread_rwlock_unlock(&reg->lock);
            return group;
        }
        group = group->next;
    }

    pthread_rwlock_unlock(&reg->lock);
    return NULL;
}

bool procgroup_delete(ProcessGroupRegistry *reg, const char *name) {
    if (!reg || !name) return false;

    pthread_rwlock_wrlock(&reg->lock);

    ProcessGroup **pp = &reg->groups;
    while (*pp) {
        ProcessGroup *group = *pp;
        if (strcmp(group->name, name) == 0) {
            *pp = group->next;
            reg->count--;
            pthread_rwlock_unlock(&reg->lock);
            group_free(group);
            return true;
        }
        pp = &group->next;
    }

    pthread_rwlock_unlock(&reg->lock);
    return false;
}

const char **procgroup_list(ProcessGroupRegistry *reg, size_t *count) {
    if (!reg || !count) {
        if (count) *count = 0;
        return NULL;
    }

    pthread_rwlock_rdlock(&reg->lock);

    *count = reg->count;
    if (reg->count == 0) {
        pthread_rwlock_unlock(&reg->lock);
        return NULL;
    }

    const char **names = malloc(sizeof(char *) * reg->count);
    if (!names) {
        LOG_ERROR("procgroup: failed to allocate names array for %zu groups", reg->count);
        *count = 0;
        pthread_rwlock_unlock(&reg->lock);
        return NULL;
    }

    size_t i = 0;
    ProcessGroup *group = reg->groups;
    while (group && i < reg->count) {
        names[i++] = group->name;
        group = group->next;
    }

    pthread_rwlock_unlock(&reg->lock);
    return names;
}

/* Membership Operations */

bool procgroup_join(ProcessGroupRegistry *reg, const char *name, Pid pid) {
    ProcessGroup *group = procgroup_get_or_create(reg, name);
    if (!group) return false;

    pthread_mutex_lock(&group->lock);

    for (size_t i = 0; i < group->count; i++) {
        if (group->members[i] == pid) {
            pthread_mutex_unlock(&group->lock);
            return true;
        }
    }

    if (group->count >= group->capacity) {
        size_t new_capacity = group->capacity ? group->capacity * 2 : 8;
        Pid *new_members = realloc(group->members, sizeof(Pid) * new_capacity);
        if (!new_members) {
            LOG_ERROR("procgroup: failed to grow members array to %zu for group '%s'",
                      new_capacity, group->name);
            pthread_mutex_unlock(&group->lock);
            return false;
        }
        group->members = new_members;
        group->capacity = new_capacity;
    }

    group->members[group->count++] = pid;

    pthread_mutex_unlock(&group->lock);
    return true;
}

void procgroup_leave(ProcessGroupRegistry *reg, const char *name, Pid pid) {
    ProcessGroup *group = procgroup_get(reg, name);
    if (!group) return;

    pthread_mutex_lock(&group->lock);

    for (size_t i = 0; i < group->count; i++) {
        if (group->members[i] == pid) {
            group->members[i] = group->members[--group->count];
            pthread_mutex_unlock(&group->lock);
            return;
        }
    }

    pthread_mutex_unlock(&group->lock);
}

void procgroup_leave_all(ProcessGroupRegistry *reg, Pid pid) {
    if (!reg) return;

    pthread_rwlock_rdlock(&reg->lock);

    ProcessGroup *group = reg->groups;
    while (group) {
        pthread_mutex_lock(&group->lock);

        for (size_t i = 0; i < group->count; i++) {
            if (group->members[i] == pid) {
                group->members[i] = group->members[--group->count];
                break;
            }
        }

        pthread_mutex_unlock(&group->lock);
        group = group->next;
    }

    pthread_rwlock_unlock(&reg->lock);
}

bool procgroup_is_member(ProcessGroupRegistry *reg, const char *name, Pid pid) {
    ProcessGroup *group = procgroup_get(reg, name);
    if (!group) return false;

    pthread_mutex_lock(&group->lock);

    for (size_t i = 0; i < group->count; i++) {
        if (group->members[i] == pid) {
            pthread_mutex_unlock(&group->lock);
            return true;
        }
    }

    pthread_mutex_unlock(&group->lock);
    return false;
}

Pid *procgroup_members(ProcessGroupRegistry *reg, const char *name, size_t *count) {
    if (!count) return NULL;
    *count = 0;

    ProcessGroup *group = procgroup_get(reg, name);
    if (!group) return NULL;

    pthread_mutex_lock(&group->lock);

    if (group->count == 0) {
        pthread_mutex_unlock(&group->lock);
        return NULL;
    }

    Pid *members = malloc(sizeof(Pid) * group->count);
    if (!members) {
        LOG_ERROR("procgroup: failed to allocate members copy for group '%s' (%zu members)",
                  group->name, group->count);
        pthread_mutex_unlock(&group->lock);
        return NULL;
    }

    memcpy(members, group->members, sizeof(Pid) * group->count);
    *count = group->count;

    pthread_mutex_unlock(&group->lock);
    return members;
}

size_t procgroup_member_count(ProcessGroupRegistry *reg, const char *name) {
    ProcessGroup *group = procgroup_get(reg, name);
    if (!group) return 0;

    pthread_mutex_lock(&group->lock);
    size_t count = group->count;
    pthread_mutex_unlock(&group->lock);

    return count;
}

/* Broadcasting */

size_t procgroup_broadcast(ProcessGroupRegistry *reg, Scheduler *sched,
                           const char *name, Pid sender, Value *message) {
    if (!reg || !sched || !name || !message) return 0;

    size_t count;
    Pid *members = procgroup_members(reg, name, &count);
    if (!members || count == 0) {
        free(members);
        return 0;
    }

    size_t sent = 0;
    for (size_t i = 0; i < count; i++) {
        Block *target = scheduler_get_block(sched, members[i]);
        if (target && block_is_alive(target)) {
            if (block_send(target, sender, message)) {
                sent++;
            }
        }
    }

    free(members);
    return sent;
}

size_t procgroup_broadcast_others(ProcessGroupRegistry *reg, Scheduler *sched,
                                  const char *name, Pid sender, Value *message) {
    if (!reg || !sched || !name || !message) return 0;

    size_t count;
    Pid *members = procgroup_members(reg, name, &count);
    if (!members || count == 0) {
        free(members);
        return 0;
    }

    size_t sent = 0;
    for (size_t i = 0; i < count; i++) {
        if (members[i] == sender) continue;

        Block *target = scheduler_get_block(sched, members[i]);
        if (target && block_is_alive(target)) {
            if (block_send(target, sender, message)) {
                sent++;
            }
        }
    }

    free(members);
    return sent;
}
