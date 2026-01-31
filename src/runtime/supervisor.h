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

/* Forward declarations */
typedef struct Block Block;
typedef struct Scheduler Scheduler;

/*============================================================================
 * Restart Strategies
 *============================================================================*/

/**
 * Child restart policy - determines when a child should be restarted.
 */
typedef enum RestartStrategy {
    RESTART_PERMANENT,   /* Always restart on any exit */
    RESTART_TRANSIENT,   /* Restart only on abnormal exit (crash) */
    RESTART_TEMPORARY,   /* Never restart */
} RestartStrategy;

/**
 * Supervisor strategy - determines how failures affect other children.
 */
typedef enum SupervisorStrategy {
    SUP_ONE_FOR_ONE,     /* Restart only the failed child */
    SUP_ONE_FOR_ALL,     /* Restart all children on any failure */
    SUP_REST_FOR_ONE,    /* Restart failed child + children started after it */
} SupervisorStrategy;

/*============================================================================
 * Exit Reasons
 *============================================================================*/

/**
 * Exit reason for a process.
 */
typedef enum ExitReason {
    EXIT_NORMAL,         /* Normal termination (exit code 0) */
    EXIT_CRASH,          /* Abnormal termination (runtime error) */
    EXIT_KILLED,         /* Forcefully killed */
    EXIT_SHUTDOWN,       /* Shutdown requested */
    EXIT_TIMEOUT,        /* Operation timed out */
} ExitReason;

/*============================================================================
 * Child Specification
 *============================================================================*/

/**
 * Specification for a supervised child process.
 * Describes how to start and restart a child.
 */
typedef struct ChildSpec {
    /* Identity */
    char *name;                  /* Child name for identification */

    /* Code to execute */
    Bytecode *init_code;         /* Bytecode to spawn child */

    /* Restart policy */
    RestartStrategy restart;     /* When to restart */
    uint32_t max_restarts;       /* Max restarts in time window (0 = unlimited) */
    uint32_t restart_window_ms;  /* Time window for restart counting (ms) */

    /* Current state */
    Pid child_pid;               /* Current child PID (PID_INVALID if not running) */
    uint32_t restart_count;      /* Restarts in current window */
    uint64_t window_start_ms;    /* Start of current restart window */
    uint64_t started_at_ms;      /* When child was started */
} ChildSpec;

/*============================================================================
 * Supervisor Structure
 *============================================================================*/

/**
 * Supervisor manages a set of child processes with restart strategies.
 */
typedef struct Supervisor {
    /* Strategy */
    SupervisorStrategy strategy;

    /* Restart limits for the supervisor itself */
    uint32_t max_restarts;       /* Max total restarts before giving up */
    uint32_t restart_window_ms;  /* Time window for restart counting */

    /* Children */
    ChildSpec *children;         /* Array of child specifications */
    size_t child_count;          /* Number of children */
    size_t child_capacity;       /* Allocated capacity */

    /* Restart tracking */
    uint32_t total_restart_count; /* Total restarts in current window */
    uint64_t window_start_ms;    /* Start of current restart window */

    /* State */
    bool shutting_down;          /* True if supervisor is shutting down */
} Supervisor;

/*============================================================================
 * Exit Signal
 *============================================================================*/

/**
 * Exit signal sent to linked processes when a block terminates.
 */
typedef struct ExitSignal {
    Pid from;                    /* PID of exiting block */
    ExitReason reason;           /* Why the block exited */
    int exit_code;               /* Exit code */
    const char *exit_message;    /* Error message (for crashes) */
} ExitSignal;

/*============================================================================
 * Supervisor API - Lifecycle
 *============================================================================*/

/**
 * Create a new supervisor with the given strategy.
 */
Supervisor *supervisor_new(SupervisorStrategy strategy);

/**
 * Free a supervisor and all its resources.
 * Does NOT terminate children - call supervisor_shutdown() first.
 */
void supervisor_free(Supervisor *sup);

/**
 * Initialize a block as a supervisor.
 * The block will handle exit signals as messages instead of crashing.
 */
bool supervisor_init_block(Block *block, SupervisorStrategy strategy);

/*============================================================================
 * Supervisor API - Child Management
 *============================================================================*/

/**
 * Add a child specification to the supervisor.
 * The child will be started immediately.
 * Returns true on success.
 */
bool supervisor_add_child(Supervisor *sup, Scheduler *sched, Block *sup_block,
                          const char *name, Bytecode *code, RestartStrategy restart);

/**
 * Add a child with custom restart limits.
 */
bool supervisor_add_child_ex(Supervisor *sup, Scheduler *sched, Block *sup_block,
                             const char *name, Bytecode *code, RestartStrategy restart,
                             uint32_t max_restarts, uint32_t restart_window_ms);

/**
 * Remove a child from supervision.
 * The child will be terminated.
 */
bool supervisor_remove_child(Supervisor *sup, Scheduler *sched, const char *name);

/**
 * Get child specification by name.
 */
ChildSpec *supervisor_get_child(Supervisor *sup, const char *name);

/**
 * Get child specification by PID.
 */
ChildSpec *supervisor_get_child_by_pid(Supervisor *sup, Pid pid);

/**
 * List all supervised children.
 */
const ChildSpec *supervisor_which_children(const Supervisor *sup, size_t *count);

/*============================================================================
 * Supervisor API - Exit Handling
 *============================================================================*/

/**
 * Handle a child exit.
 * Called when a supervised child terminates.
 * May restart the child according to the restart strategy.
 * Returns true if the supervisor should continue, false if it should stop.
 */
bool supervisor_handle_exit(Supervisor *sup, Scheduler *sched, Block *sup_block,
                            Pid child_pid, ExitReason reason, int exit_code,
                            const char *exit_message);

/**
 * Shutdown the supervisor and all children.
 * Children are terminated in reverse order of start.
 */
void supervisor_shutdown(Supervisor *sup, Scheduler *sched);

/*============================================================================
 * Supervisor API - Queries
 *============================================================================*/

/**
 * Check if supervisor has reached max restart intensity.
 */
bool supervisor_max_restarts_reached(const Supervisor *sup);

/**
 * Get the number of active children.
 */
size_t supervisor_active_count(const Supervisor *sup);

/*============================================================================
 * Child Spec API
 *============================================================================*/

/**
 * Create a new child specification.
 */
ChildSpec *child_spec_new(const char *name, Bytecode *code, RestartStrategy restart);

/**
 * Free a child specification.
 */
void child_spec_free(ChildSpec *spec);

/**
 * Start a child according to its specification.
 * Returns the child's PID, or PID_INVALID on failure.
 */
Pid child_spec_start(ChildSpec *spec, Scheduler *sched, Block *sup_block);

/**
 * Check if restart is allowed for this child.
 * Updates restart counters.
 */
bool child_spec_can_restart(ChildSpec *spec, ExitReason reason);

/*============================================================================
 * Exit Signal API
 *============================================================================*/

/**
 * Create an exit signal value (as a map) for sending to linked processes.
 */
struct Value *exit_signal_to_value(const ExitSignal *signal);

/**
 * Parse an exit signal from a received value.
 * Returns true if the value is a valid exit signal.
 */
bool exit_signal_from_value(struct Value *value, ExitSignal *signal);

/**
 * Get exit reason name as string.
 */
const char *exit_reason_name(ExitReason reason);

#endif /* AGIM_RUNTIME_SUPERVISOR_H */
