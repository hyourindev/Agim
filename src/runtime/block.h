/*
 * Agim - Block (Process) Model
 *
 * A Block is an isolated unit of execution with its own heap,
 * stack, and mailbox. Blocks communicate only via message passing.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_BLOCK_H
#define AGIM_RUNTIME_BLOCK_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/capability.h"
#include "runtime/mailbox.h"
#include "vm/bytecode.h"
#include "vm/gc.h"
#include "vm/value.h"
#include "vm/vm.h"

/* Forward declaration for supervisor */
typedef struct Supervisor Supervisor;

/*============================================================================
 * Block State
 *============================================================================*/

typedef enum BlockState {
    BLOCK_RUNNABLE,   /* Ready to execute */
    BLOCK_RUNNING,    /* Currently executing */
    BLOCK_WAITING,    /* Blocked on receive or async op */
    BLOCK_DEAD,       /* Terminated */
} BlockState;

/*============================================================================
 * Resource Limits
 *============================================================================*/

typedef struct BlockLimits {
    size_t max_heap_size;       /* Maximum heap memory (bytes) */
    size_t max_stack_depth;     /* Maximum value stack entries */
    size_t max_call_depth;      /* Maximum call frames */
    size_t max_reductions;      /* Reductions per time slice */
    size_t max_mailbox_size;    /* Maximum pending messages */
} BlockLimits;

/*============================================================================
 * Resource Counters
 *============================================================================*/

typedef struct BlockCounters {
    size_t reductions;          /* Instructions executed */
    size_t messages_sent;       /* Messages sent */
    size_t messages_received;   /* Messages received */
    size_t gc_collections;      /* GC runs */
    size_t gc_bytes_collected;  /* Total bytes freed by GC */
} BlockCounters;

/*============================================================================
 * Block Structure
 *============================================================================*/

typedef struct Block {
    /* Identity */
    Pid pid;
    const char *name;           /* Optional name for debugging */

    /* State (atomic for thread-safe access) */
    _Atomic(BlockState) state;

    /*
     * Exit info - only valid when state == BLOCK_DEAD.
     * We use a union to share space with fields only needed during execution.
     * This saves 12 bytes for live blocks.
     */
    union {
        struct {
            int exit_code;              /* Exit code if DEAD */
            const char *exit_reason;    /* Error message if crashed */
        } exit;
        struct {
            /* Reserved for future runtime data */
            void *_reserved1;
            void *_reserved2;
        } runtime;
    } u;

    /* Execution */
    VM *vm;                     /* Virtual machine instance */
    Heap *heap;                 /* Isolated memory heap */
    Bytecode *code;             /* Bytecode being executed */

    /* Message passing */
    Mailbox mailbox;            /* Incoming message queue */

    /* Security */
    CapabilitySet capabilities;

    /* Resource management */
    BlockLimits limits;
    BlockCounters counters;

    /* Linking (for crash notification) - use uint32_t to save 8 bytes */
    Pid *links;                 /* PIDs to notify on death */
    uint32_t link_count;        /* Current number of links */
    uint32_t link_capacity;     /* Allocated link slots */

    /* Supervision */
    Pid parent;                 /* Parent block (0 if none) */
    Supervisor *supervisor;     /* Supervisor struct if this is a supervisor (NULL otherwise) */

    /* Monitor references (for monitoring without linking) */
    Pid *monitors;              /* PIDs we are monitoring */
    uint32_t monitor_count;
    uint32_t monitor_capacity;

    /* Monitored by (who is monitoring us) */
    Pid *monitored_by;          /* PIDs monitoring us */
    uint32_t monitored_by_count;
    uint32_t monitored_by_capacity;

    /* Scheduler bookkeeping */
    struct Block *next;         /* Next in run queue */
    struct Block *prev;         /* Previous in run queue */
} Block;

/*============================================================================
 * Block Lifecycle
 *============================================================================*/

/**
 * Get default block limits.
 */
BlockLimits block_limits_default(void);

/**
 * Create a new block with the given pid and limits.
 * Returns NULL on allocation failure.
 */
Block *block_new(Pid pid, const char *name, const BlockLimits *limits);

/**
 * Free a block and all its resources.
 */
void block_free(Block *block);

/**
 * Load bytecode into a block for execution.
 */
bool block_load(Block *block, Bytecode *code);

/*============================================================================
 * Execution
 *============================================================================*/

/**
 * Result of running a block for one time slice.
 */
typedef enum BlockRunResult {
    BLOCK_RUN_OK,           /* Completed normally */
    BLOCK_RUN_YIELD,        /* Yielded (reductions exhausted) */
    BLOCK_RUN_WAITING,      /* Waiting for message/async */
    BLOCK_RUN_ERROR,        /* Runtime error */
    BLOCK_RUN_HALTED,       /* Explicit halt */
} BlockRunResult;

/**
 * Run a block for one time slice.
 * Returns the result of execution.
 */
BlockRunResult block_run(Block *block);

/**
 * Get current block state.
 */
BlockState block_state(const Block *block);

/**
 * Set block state (atomic).
 */
void block_set_state(Block *block, BlockState state);

/**
 * Atomically transition block state from one state to another.
 * Returns true if transition succeeded, false if current state didn't match 'from'.
 */
bool block_try_transition(Block *block, BlockState from, BlockState to);

/*============================================================================
 * Capabilities
 *============================================================================*/

/**
 * Grant capabilities to a block.
 */
void block_grant(Block *block, CapabilitySet caps);

/**
 * Revoke capabilities from a block.
 */
void block_revoke(Block *block, CapabilitySet caps);

/**
 * Check if block has a capability.
 */
bool block_has_cap(const Block *block, Capability cap);

/**
 * Check capability and set error if missing.
 * Returns true if capability is present.
 */
bool block_check_cap(Block *block, Capability cap);

/*============================================================================
 * Linking
 *============================================================================*/

/**
 * Link this block to another (will be notified on death).
 */
bool block_link(Block *block, Pid other);

/**
 * Unlink from another block.
 */
void block_unlink(Block *block, Pid other);

/**
 * Get array of linked PIDs.
 */
const Pid *block_get_links(const Block *block, size_t *count);

/*============================================================================
 * Monitoring (like linking but unidirectional, no crash propagation)
 *============================================================================*/

/**
 * Start monitoring another block (receive down message on exit).
 */
bool block_monitor(Block *block, Pid target);

/**
 * Stop monitoring another block.
 */
void block_demonitor(Block *block, Pid target);

/**
 * Add a monitor to this block (internal - called from monitored block).
 */
bool block_add_monitored_by(Block *block, Pid monitor_pid);

/**
 * Remove a monitor from this block (internal).
 */
void block_remove_monitored_by(Block *block, Pid monitor_pid);

/**
 * Get array of monitored PIDs.
 */
const Pid *block_get_monitors(const Block *block, size_t *count);

/*============================================================================
 * Message Passing
 *============================================================================*/

/**
 * Send a message to a block (high-level API).
 * Deep copies the value into the target block's heap.
 * Returns true on success.
 */
bool block_send(Block *target, Pid sender, Value *value);

/**
 * Receive a message from the mailbox.
 * Returns NULL if no message available.
 */
Message *block_receive(Block *block);

/**
 * Check if block has pending messages.
 */
bool block_has_messages(const Block *block);

/*============================================================================
 * Termination
 *============================================================================*/

/**
 * Terminate a block normally.
 */
void block_exit(Block *block, int exit_code);

/**
 * Terminate a block with an error.
 */
void block_crash(Block *block, const char *reason);

/**
 * Check if block is alive (not DEAD).
 */
bool block_is_alive(const Block *block);

/*============================================================================
 * Debug
 *============================================================================*/

/**
 * Print block info for debugging.
 */
void block_print(const Block *block);

/**
 * Get state name as string.
 */
const char *block_state_name(BlockState state);

#endif /* AGIM_RUNTIME_BLOCK_H */
