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

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/capability.h"
#include "runtime/mailbox.h"
#include "runtime/timer.h"
#include "vm/bytecode.h"
#include "vm/gc.h"
#include "vm/value.h"
#include "vm/vm.h"

typedef struct Tracer Tracer;
typedef struct Supervisor Supervisor;

/* Block State */

typedef enum BlockState {
    BLOCK_RUNNABLE,
    BLOCK_RUNNING,
    BLOCK_WAITING,
    BLOCK_DEAD,
} BlockState;

/* Resource Limits */

typedef struct BlockLimits {
    size_t max_heap_size;
    size_t max_stack_depth;
    size_t max_call_depth;
    size_t max_reductions;
    size_t max_mailbox_size;
} BlockLimits;

/* Resource Counters */

typedef struct BlockCounters {
    size_t reductions;
    size_t messages_sent;
    _Atomic(size_t) messages_received;  /* Atomic: updated by multiple sender threads */
    size_t gc_collections;
    size_t gc_bytes_collected;
} BlockCounters;

/* Block Structure */

typedef struct Block {
    Pid pid;
    const char *name;

    _Atomic(BlockState) state;

    /*
     * Exit info - only valid when state == BLOCK_DEAD.
     * Union saves 12 bytes for live blocks.
     */
    union {
        struct {
            int exit_code;
            const char *exit_reason;
        } exit;
        struct {
            void *_reserved1;
            void *_reserved2;
        } runtime;
    } u;

    VM *vm;
    Heap *heap;
    Bytecode *code;

    Mailbox mailbox;

    CapabilitySet capabilities;

    BlockLimits limits;
    BlockCounters counters;

    Pid *links;
    uint32_t link_count;
    uint32_t link_capacity;

    Pid parent;
    Supervisor *supervisor;

    Pid *monitors;
    uint32_t monitor_count;
    uint32_t monitor_capacity;

    Pid *monitored_by;
    uint32_t monitored_by_count;
    uint32_t monitored_by_capacity;

    pthread_mutex_t link_mutex;  /* Protects links, monitors, monitored_by arrays */

    struct Block *next;
    struct Block *prev;

    TimerEntry *pending_timer;
    bool timeout_fired;

    Message *save_queue_head;
    Message *save_queue_tail;

    Tracer *tracer;

    char *module_name;
    bool pending_upgrade;
} Block;

/* Lifecycle */

BlockLimits block_limits_default(void);
Block *block_new(Pid pid, const char *name, const BlockLimits *limits);
void block_free(Block *block);
bool block_load(Block *block, Bytecode *code);

/* Execution */

typedef enum BlockRunResult {
    BLOCK_RUN_OK,
    BLOCK_RUN_YIELD,
    BLOCK_RUN_WAITING,
    BLOCK_RUN_ERROR,
    BLOCK_RUN_HALTED,
} BlockRunResult;

BlockRunResult block_run(Block *block);
BlockState block_state(const Block *block);
void block_set_state(Block *block, BlockState state);
bool block_try_transition(Block *block, BlockState from, BlockState to);

/* Capabilities */

void block_grant(Block *block, CapabilitySet caps);
void block_revoke(Block *block, CapabilitySet caps);
bool block_has_cap(const Block *block, Capability cap);
bool block_check_cap(Block *block, Capability cap);

/* Linking */

bool block_link(Block *block, Pid other);
void block_unlink(Block *block, Pid other);
const Pid *block_get_links(const Block *block, size_t *count);

/* Monitoring */

bool block_monitor(Block *block, Pid target);
void block_demonitor(Block *block, Pid target);
bool block_add_monitored_by(Block *block, Pid monitor_pid);
void block_remove_monitored_by(Block *block, Pid monitor_pid);
const Pid *block_get_monitors(const Block *block, size_t *count);

/* Message Passing */

bool block_send(Block *target, Pid sender, Value *value);
Message *block_receive(Block *block);
bool block_has_messages(const Block *block);

/* Termination */

void block_exit(Block *block, int exit_code);
void block_crash(Block *block, const char *reason);
bool block_is_alive(const Block *block);

/* Debug */

void block_print(const Block *block);
const char *block_state_name(BlockState state);

#endif /* AGIM_RUNTIME_BLOCK_H */
