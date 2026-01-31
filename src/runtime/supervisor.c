/*
 * Agim - Supervisor Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/supervisor.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "runtime/timer.h"
#include "vm/value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exit Reason Names */

const char *exit_reason_name(ExitReason reason) {
    switch (reason) {
    case EXIT_NORMAL:   return "normal";
    case EXIT_CRASH:    return "crash";
    case EXIT_KILLED:   return "killed";
    case EXIT_SHUTDOWN: return "shutdown";
    case EXIT_TIMEOUT:  return "timeout";
    default:            return "unknown";
    }
}

/* Child Specification */

ChildSpec *child_spec_new(const char *name, Bytecode *code, RestartStrategy restart) {
    ChildSpec *spec = malloc(sizeof(ChildSpec));
    if (!spec) return NULL;

    spec->name = name ? strdup(name) : NULL;
    spec->init_code = code ? bytecode_retain(code) : NULL;
    spec->restart = restart;
    spec->max_restarts = 3;
    spec->restart_window_ms = 5000;
    spec->child_pid = PID_INVALID;
    spec->restart_count = 0;
    spec->window_start_ms = 0;
    spec->started_at_ms = 0;

    return spec;
}

void child_spec_free(ChildSpec *spec) {
    if (!spec) return;

    free(spec->name);
    if (spec->init_code) {
        bytecode_release(spec->init_code);
    }
    free(spec);
}

Pid child_spec_start(ChildSpec *spec, Scheduler *sched, Block *sup_block) {
    if (!spec || !sched || !spec->init_code) return PID_INVALID;

    Pid child_pid = scheduler_spawn(sched, spec->init_code, spec->name);
    if (child_pid == PID_INVALID) {
        return PID_INVALID;
    }

    Block *child = scheduler_get_block(sched, child_pid);
    if (!child) {
        return PID_INVALID;
    }

    child->parent = sup_block->pid;

    block_link(child, sup_block->pid);
    block_link(sup_block, child_pid);

    spec->child_pid = child_pid;
    spec->started_at_ms = timer_current_time_ms();

    return child_pid;
}

bool child_spec_can_restart(ChildSpec *spec, ExitReason reason) {
    if (!spec) return false;

    switch (spec->restart) {
    case RESTART_TEMPORARY:
        return false;

    case RESTART_TRANSIENT:
        if (reason == EXIT_NORMAL) {
            return false;
        }
        break;

    case RESTART_PERMANENT:
        break;
    }

    if (spec->max_restarts == 0) {
        return true;
    }

    uint64_t now = timer_current_time_ms();

    if (spec->restart_window_ms > 0 &&
        now - spec->window_start_ms >= spec->restart_window_ms) {
        spec->restart_count = 0;
        spec->window_start_ms = now;
    }

    if (spec->window_start_ms == 0) {
        spec->window_start_ms = now;
    }

    if (spec->restart_count >= spec->max_restarts) {
        return false;
    }

    spec->restart_count++;

    return true;
}

/* Supervisor Lifecycle */

Supervisor *supervisor_new(SupervisorStrategy strategy) {
    Supervisor *sup = malloc(sizeof(Supervisor));
    if (!sup) return NULL;

    sup->strategy = strategy;
    sup->max_restarts = 5;
    sup->restart_window_ms = 60000;

    sup->children = NULL;
    sup->child_count = 0;
    sup->child_capacity = 0;

    sup->total_restart_count = 0;
    sup->window_start_ms = 0;
    sup->shutting_down = false;

    return sup;
}

void supervisor_free(Supervisor *sup) {
    if (!sup) return;

    for (size_t i = 0; i < sup->child_count; i++) {
        free(sup->children[i].name);
        if (sup->children[i].init_code) {
            bytecode_release(sup->children[i].init_code);
        }
    }
    free(sup->children);

    free(sup);
}

bool supervisor_init_block(Block *block, SupervisorStrategy strategy) {
    if (!block) return false;

    block_grant(block, CAP_TRAP_EXIT);

    Supervisor *sup = supervisor_new(strategy);
    if (!sup) return false;

    block->supervisor = sup;

    return true;
}

/* Child Management */

static bool grow_children_array(Supervisor *sup) {
    size_t new_capacity = sup->child_capacity == 0 ? 4 : sup->child_capacity * 2;
    ChildSpec *new_children = realloc(sup->children, sizeof(ChildSpec) * new_capacity);
    if (!new_children) return false;

    sup->children = new_children;
    sup->child_capacity = new_capacity;
    return true;
}

bool supervisor_add_child(Supervisor *sup, Scheduler *sched, Block *sup_block,
                          const char *name, Bytecode *code, RestartStrategy restart) {
    return supervisor_add_child_ex(sup, sched, sup_block, name, code, restart, 3, 5000);
}

bool supervisor_add_child_ex(Supervisor *sup, Scheduler *sched, Block *sup_block,
                             const char *name, Bytecode *code, RestartStrategy restart,
                             uint32_t max_restarts, uint32_t restart_window_ms) {
    if (!sup || !sched || !code) return false;

    if (sup->child_count >= sup->child_capacity) {
        if (!grow_children_array(sup)) return false;
    }

    ChildSpec *spec = &sup->children[sup->child_count];
    spec->name = name ? strdup(name) : NULL;
    spec->init_code = bytecode_retain(code);
    spec->restart = restart;
    spec->max_restarts = max_restarts;
    spec->restart_window_ms = restart_window_ms;
    spec->child_pid = PID_INVALID;
    spec->restart_count = 0;
    spec->window_start_ms = 0;
    spec->started_at_ms = 0;

    Pid child_pid = child_spec_start(spec, sched, sup_block);
    if (child_pid == PID_INVALID) {
        free(spec->name);
        bytecode_release(spec->init_code);
        return false;
    }

    sup->child_count++;
    return true;
}

bool supervisor_remove_child(Supervisor *sup, Scheduler *sched, const char *name) {
    if (!sup || !name) return false;

    for (size_t i = 0; i < sup->child_count; i++) {
        if (sup->children[i].name && strcmp(sup->children[i].name, name) == 0) {
            ChildSpec *spec = &sup->children[i];

            if (spec->child_pid != PID_INVALID) {
                scheduler_kill(sched, spec->child_pid);
            }

            free(spec->name);
            if (spec->init_code) {
                bytecode_release(spec->init_code);
            }

            if (i < sup->child_count - 1) {
                sup->children[i] = sup->children[sup->child_count - 1];
            }
            sup->child_count--;

            return true;
        }
    }

    return false;
}

ChildSpec *supervisor_get_child(Supervisor *sup, const char *name) {
    if (!sup || !name) return NULL;

    for (size_t i = 0; i < sup->child_count; i++) {
        if (sup->children[i].name && strcmp(sup->children[i].name, name) == 0) {
            return &sup->children[i];
        }
    }

    return NULL;
}

ChildSpec *supervisor_get_child_by_pid(Supervisor *sup, Pid pid) {
    if (!sup || pid == PID_INVALID) return NULL;

    for (size_t i = 0; i < sup->child_count; i++) {
        if (sup->children[i].child_pid == pid) {
            return &sup->children[i];
        }
    }

    return NULL;
}

const ChildSpec *supervisor_which_children(const Supervisor *sup, size_t *count) {
    if (!sup) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = sup->child_count;
    return sup->children;
}

/* Exit Handling */

static int find_child_index(Supervisor *sup, Pid pid) {
    for (size_t i = 0; i < sup->child_count; i++) {
        if (sup->children[i].child_pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

static bool restart_all_children(Supervisor *sup, Scheduler *sched, Block *sup_block) {
    bool all_ok = true;

    for (size_t i = 0; i < sup->child_count; i++) {
        if (sup->children[i].child_pid != PID_INVALID) {
            scheduler_kill(sched, sup->children[i].child_pid);
            block_unlink(sup_block, sup->children[i].child_pid);
            sup->children[i].child_pid = PID_INVALID;
        }
    }

    for (size_t i = 0; i < sup->child_count; i++) {
        Pid new_pid = child_spec_start(&sup->children[i], sched, sup_block);
        if (new_pid == PID_INVALID) {
            all_ok = false;
        }
    }

    return all_ok;
}

static bool restart_rest_for_one(Supervisor *sup, Scheduler *sched, Block *sup_block,
                                  int failed_index) {
    bool all_ok = true;

    for (size_t i = (size_t)failed_index; i < sup->child_count; i++) {
        if (sup->children[i].child_pid != PID_INVALID) {
            scheduler_kill(sched, sup->children[i].child_pid);
            block_unlink(sup_block, sup->children[i].child_pid);
            sup->children[i].child_pid = PID_INVALID;
        }
    }

    for (size_t i = (size_t)failed_index; i < sup->child_count; i++) {
        Pid new_pid = child_spec_start(&sup->children[i], sched, sup_block);
        if (new_pid == PID_INVALID) {
            all_ok = false;
        }
    }

    return all_ok;
}

bool supervisor_handle_exit(Supervisor *sup, Scheduler *sched, Block *sup_block,
                            Pid child_pid, ExitReason reason, int exit_code,
                            const char *exit_message) {
    (void)exit_code;      /* Reserved for future logging/diagnostics */
    (void)exit_message;   /* Reserved for future logging/diagnostics */
    if (!sup || !sched || !sup_block) return false;

    if (sup->shutting_down) {
        return true;
    }

    int child_index = find_child_index(sup, child_pid);
    if (child_index < 0) {
        return true;
    }

    ChildSpec *spec = &sup->children[child_index];

    block_unlink(sup_block, child_pid);
    spec->child_pid = PID_INVALID;

    if (!child_spec_can_restart(spec, reason)) {
        if (reason == EXIT_NORMAL && spec->restart != RESTART_PERMANENT) {
            return true;
        }

        fprintf(stderr, "Supervisor: max restarts reached for child '%s'\n",
                spec->name ? spec->name : "(unnamed)");

        return !supervisor_max_restarts_reached(sup);
    }

    uint64_t now = timer_current_time_ms();
    if (sup->restart_window_ms > 0 &&
        now - sup->window_start_ms >= sup->restart_window_ms) {
        sup->total_restart_count = 0;
        sup->window_start_ms = now;
    }
    if (sup->window_start_ms == 0) {
        sup->window_start_ms = now;
    }
    sup->total_restart_count++;

    if (supervisor_max_restarts_reached(sup)) {
        fprintf(stderr, "Supervisor: max total restarts reached, giving up\n");
        return false;
    }

    switch (sup->strategy) {
    case SUP_ONE_FOR_ONE:
        {
            Pid new_pid = child_spec_start(spec, sched, sup_block);
            if (new_pid == PID_INVALID) {
                fprintf(stderr, "Supervisor: failed to restart child '%s'\n",
                        spec->name ? spec->name : "(unnamed)");
            }
        }
        break;

    case SUP_ONE_FOR_ALL:
        restart_all_children(sup, sched, sup_block);
        break;

    case SUP_REST_FOR_ONE:
        restart_rest_for_one(sup, sched, sup_block, child_index);
        break;
    }

    return true;
}

void supervisor_shutdown(Supervisor *sup, Scheduler *sched) {
    if (!sup || !sched) return;

    sup->shutting_down = true;

    for (size_t i = sup->child_count; i > 0; i--) {
        ChildSpec *spec = &sup->children[i - 1];
        if (spec->child_pid != PID_INVALID) {
            scheduler_kill(sched, spec->child_pid);
            spec->child_pid = PID_INVALID;
        }
    }
}

/* Queries */

bool supervisor_max_restarts_reached(const Supervisor *sup) {
    if (!sup || sup->max_restarts == 0) return false;
    return sup->total_restart_count >= sup->max_restarts;
}

size_t supervisor_active_count(const Supervisor *sup) {
    if (!sup) return 0;

    size_t count = 0;
    for (size_t i = 0; i < sup->child_count; i++) {
        if (sup->children[i].child_pid != PID_INVALID) {
            count++;
        }
    }
    return count;
}

/* Exit Signal */

Value *exit_signal_to_value(const ExitSignal *signal) {
    if (!signal) return value_nil();

    Value *map = value_map();
    map = map_set(map, "type", value_string("exit"));
    map = map_set(map, "pid", value_pid(signal->from));
    map = map_set(map, "reason", value_string(exit_reason_name(signal->reason)));
    map = map_set(map, "code", value_int(signal->exit_code));

    if (signal->exit_message) {
        map = map_set(map, "message", value_string(signal->exit_message));
    }

    return map;
}

bool exit_signal_from_value(Value *value, ExitSignal *signal) {
    if (!value || !signal || !value_is_map(value)) return false;

    Value *type_val = map_get(value, "type");
    if (!type_val || !value_is_string(type_val)) return false;
    if (strcmp(type_val->as.string->data, "exit") != 0) return false;

    Value *pid_val = map_get(value, "pid");
    if (!pid_val || pid_val->type != VAL_PID) return false;
    signal->from = pid_val->as.pid;

    Value *reason_val = map_get(value, "reason");
    if (!reason_val || !value_is_string(reason_val)) return false;

    const char *reason_str = reason_val->as.string->data;
    if (strcmp(reason_str, "normal") == 0) {
        signal->reason = EXIT_NORMAL;
    } else if (strcmp(reason_str, "crash") == 0) {
        signal->reason = EXIT_CRASH;
    } else if (strcmp(reason_str, "killed") == 0) {
        signal->reason = EXIT_KILLED;
    } else if (strcmp(reason_str, "shutdown") == 0) {
        signal->reason = EXIT_SHUTDOWN;
    } else if (strcmp(reason_str, "timeout") == 0) {
        signal->reason = EXIT_TIMEOUT;
    } else {
        signal->reason = EXIT_CRASH;
    }

    Value *code_val = map_get(value, "code");
    signal->exit_code = (code_val && value_is_int(code_val)) ? (int)code_val->as.integer : 0;

    Value *msg_val = map_get(value, "message");
    signal->exit_message = (msg_val && value_is_string(msg_val)) ? msg_val->as.string->data : NULL;

    return true;
}
