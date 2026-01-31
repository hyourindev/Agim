/*
 * Agim - Supervisor (OTP-style Process Supervision)
 *
 * Supervisors monitor child processes and implement restart strategies
 * for fault-tolerant agent hierarchies.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_SUPERVISOR_H
#define AGIM_RUNTIME_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/mailbox.h"
#include "vm/bytecode.h"

typedef struct Block Block;
typedef struct Scheduler Scheduler;

/* Restart Strategies */

typedef enum RestartStrategy {
    RESTART_PERMANENT,
    RESTART_TRANSIENT,
    RESTART_TEMPORARY,
} RestartStrategy;

typedef enum SupervisorStrategy {
    SUP_ONE_FOR_ONE,
    SUP_ONE_FOR_ALL,
    SUP_REST_FOR_ONE,
} SupervisorStrategy;

/* Exit Reasons */

typedef enum ExitReason {
    EXIT_NORMAL,
    EXIT_CRASH,
    EXIT_KILLED,
    EXIT_SHUTDOWN,
    EXIT_TIMEOUT,
} ExitReason;

/* Child Specification */

typedef struct ChildSpec {
    char *name;
    Bytecode *init_code;
    RestartStrategy restart;
    uint32_t max_restarts;
    uint32_t restart_window_ms;
    Pid child_pid;
    uint32_t restart_count;
    uint64_t window_start_ms;
    uint64_t started_at_ms;
} ChildSpec;

/* Supervisor */

typedef struct Supervisor {
    SupervisorStrategy strategy;
    uint32_t max_restarts;
    uint32_t restart_window_ms;
    ChildSpec *children;
    size_t child_count;
    size_t child_capacity;
    uint32_t total_restart_count;
    uint64_t window_start_ms;
    bool shutting_down;
} Supervisor;

/* Exit Signal */

typedef struct ExitSignal {
    Pid from;
    ExitReason reason;
    int exit_code;
    const char *exit_message;
} ExitSignal;

/* Supervisor Lifecycle */

Supervisor *supervisor_new(SupervisorStrategy strategy);
void supervisor_free(Supervisor *sup);
bool supervisor_init_block(Block *block, SupervisorStrategy strategy);

/* Child Management */

bool supervisor_add_child(Supervisor *sup, Scheduler *sched, Block *sup_block,
                          const char *name, Bytecode *code, RestartStrategy restart);
bool supervisor_add_child_ex(Supervisor *sup, Scheduler *sched, Block *sup_block,
                             const char *name, Bytecode *code, RestartStrategy restart,
                             uint32_t max_restarts, uint32_t restart_window_ms);
bool supervisor_remove_child(Supervisor *sup, Scheduler *sched, const char *name);
ChildSpec *supervisor_get_child(Supervisor *sup, const char *name);
ChildSpec *supervisor_get_child_by_pid(Supervisor *sup, Pid pid);
const ChildSpec *supervisor_which_children(const Supervisor *sup, size_t *count);

/* Exit Handling */

bool supervisor_handle_exit(Supervisor *sup, Scheduler *sched, Block *sup_block,
                            Pid child_pid, ExitReason reason, int exit_code,
                            const char *exit_message);
void supervisor_shutdown(Supervisor *sup, Scheduler *sched);

/* Queries */

bool supervisor_max_restarts_reached(const Supervisor *sup);
size_t supervisor_active_count(const Supervisor *sup);

/* Child Spec API */

ChildSpec *child_spec_new(const char *name, Bytecode *code, RestartStrategy restart);
void child_spec_free(ChildSpec *spec);
Pid child_spec_start(ChildSpec *spec, Scheduler *sched, Block *sup_block);
bool child_spec_can_restart(ChildSpec *spec, ExitReason reason);

/* Exit Signal API */

struct Value *exit_signal_to_value(const ExitSignal *signal);
bool exit_signal_from_value(struct Value *value, ExitSignal *signal);
const char *exit_reason_name(ExitReason reason);

#endif /* AGIM_RUNTIME_SUPERVISOR_H */
