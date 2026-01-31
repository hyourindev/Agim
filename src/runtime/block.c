/*
 * Agim - Block (Process) Model
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "runtime/supervisor.h"
#include "runtime/telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default Limits */

BlockLimits block_limits_default(void) {
    return (BlockLimits){
        .max_heap_size = 1 * 1024 * 1024,
        .max_stack_depth = 256,
        .max_call_depth = 64,
        .max_reductions = 10000,
        .max_mailbox_size = 100,
    };
}

/* Message Passing */

bool block_send(Block *target, Pid sender, Value *value) {
    if (!target || !block_is_alive(target)) return false;

    /*
     * Message passing with COW optimization:
     * - Immutable types: share directly with refcounting
     * - Mutable types (array, map): COW sharing
     * - Unsafe types (closure): deep copy
     */
    Value *msg_value;

    if (!value) {
        msg_value = value_nil();
    } else {
        switch (value->type) {
        case VAL_NIL:
        case VAL_BOOL:
        case VAL_INT:
        case VAL_FLOAT:
        case VAL_STRING:
        case VAL_PID:
        case VAL_FUNCTION:
        case VAL_VECTOR:
            msg_value = value_retain(value);
            break;

        case VAL_ARRAY:
        case VAL_MAP:
            msg_value = value_cow_share(value);
            break;

        case VAL_BYTES:
            msg_value = value_copy(value);
            break;

        case VAL_CLOSURE:
        default:
            msg_value = value_copy(value);
            break;
        }
    }

    if (!msg_value) return false;

    Message *msg = message_new(sender, msg_value);
    if (!msg) {
        value_release(msg_value);
        return false;
    }

    if (!mailbox_push(&target->mailbox, msg, target->limits.max_mailbox_size)) {
        message_free(msg);
        return false;
    }

    target->counters.messages_received++;

    return true;
}

Message *block_receive(Block *block) {
    if (!block) return NULL;
    return mailbox_pop(&block->mailbox);
}

bool block_has_messages(const Block *block) {
    return block && !mailbox_empty(&block->mailbox);
}

/* Lifecycle */

Block *block_new(Pid pid, const char *name, const BlockLimits *limits) {
    Block *block = malloc(sizeof(Block));
    if (!block) return NULL;

    block->pid = pid;
    block->name = name ? strdup(name) : NULL;

    atomic_store(&block->state, BLOCK_RUNNABLE);
    block->u.exit.exit_code = 0;
    block->u.exit.exit_reason = NULL;

    block->vm = vm_new();
    if (!block->vm) {
        free((void *)block->name);
        free(block);
        return NULL;
    }

    GCConfig gc_config = gc_config_default();
    if (limits) {
        gc_config.max_heap_size = limits->max_heap_size;
    }
    block->heap = heap_new(&gc_config);
    if (!block->heap) {
        vm_free(block->vm);
        free((void *)block->name);
        free(block);
        return NULL;
    }

    block->code = NULL;

    mailbox_init(&block->mailbox);

    block->capabilities = CAP_NONE;

    if (limits) {
        block->limits = *limits;
    } else {
        block->limits = block_limits_default();
    }

    memset(&block->counters, 0, sizeof(BlockCounters));

    block->links = NULL;
    block->link_count = 0;
    block->link_capacity = 0;

    block->parent = PID_INVALID;
    block->supervisor = NULL;

    block->monitors = NULL;
    block->monitor_count = 0;
    block->monitor_capacity = 0;
    block->monitored_by = NULL;
    block->monitored_by_count = 0;
    block->monitored_by_capacity = 0;

    block->next = NULL;
    block->prev = NULL;

    block->pending_timer = NULL;
    block->timeout_fired = false;

    block->save_queue_head = NULL;
    block->save_queue_tail = NULL;

    block->tracer = NULL;

    block->module_name = NULL;
    block->pending_upgrade = false;

    block->vm->block = block;

    return block;
}

void block_free(Block *block) {
    if (!block) return;

    if (block->vm) {
        vm_free(block->vm);
    }

    if (block->heap) {
        heap_free(block->heap);
    }

    mailbox_free(&block->mailbox);

    free(block->links);

    free(block->monitors);
    free(block->monitored_by);

    if (block->supervisor) {
        supervisor_free(block->supervisor);
    }

    Message *save_msg = block->save_queue_head;
    while (save_msg) {
        Message *next = save_msg->next;
        message_free(save_msg);
        save_msg = next;
    }

    if (block->tracer) {
        tracer_free(block->tracer);
    }

    free(block->module_name);

    free((void *)block->name);

    free((void *)block->u.exit.exit_reason);

    free(block);
}

bool block_load(Block *block, Bytecode *code) {
    if (!block || !code) return false;

    block->code = code;
    vm_load(block->vm, code);
    atomic_store(&block->state, BLOCK_RUNNABLE);

    return true;
}

/* Execution */

BlockRunResult block_run(Block *block) {
    if (!block) return BLOCK_RUN_ERROR;

    BlockState current = atomic_load(&block->state);
    if (current == BLOCK_DEAD) {
        return BLOCK_RUN_HALTED;
    }

    if (current == BLOCK_WAITING) {
        return BLOCK_RUN_WAITING;
    }

    if (!block_try_transition(block, BLOCK_RUNNABLE, BLOCK_RUNNING)) {
        return BLOCK_RUN_ERROR;
    }

    block->vm->reduction_limit = block->limits.max_reductions;
    block->vm->reductions = 0;

    VMResult result = vm_run(block->vm);

    block->counters.reductions += block->vm->reductions;

    switch (result) {
    case VM_OK:
        atomic_store(&block->state, BLOCK_DEAD);
        block->u.exit.exit_code = 0;
        return BLOCK_RUN_OK;

    case VM_HALT:
        atomic_store(&block->state, BLOCK_DEAD);
        block->u.exit.exit_code = 0;
        return BLOCK_RUN_HALTED;

    case VM_YIELD:
        atomic_store(&block->state, BLOCK_RUNNABLE);
        return BLOCK_RUN_YIELD;

    case VM_WAITING:
        return BLOCK_RUN_WAITING;

    case VM_ERROR_RUNTIME:
    case VM_ERROR_STACK_OVERFLOW:
    case VM_ERROR_STACK_UNDERFLOW:
    case VM_ERROR_TYPE:
    case VM_ERROR_DIVISION_BY_ZERO:
    case VM_ERROR_OUT_OF_BOUNDS:
    case VM_ERROR_UNDEFINED_VARIABLE:
    case VM_ERROR_ARITY:
    case VM_ERROR_CAPABILITY:
    case VM_ERROR_SEND_FAILED:
        block_crash(block, vm_error(block->vm));
        return BLOCK_RUN_ERROR;

    default:
        block_crash(block, "unknown VM error");
        return BLOCK_RUN_ERROR;
    }
}

BlockState block_state(const Block *block) {
    return block ? atomic_load(&block->state) : BLOCK_DEAD;
}

void block_set_state(Block *block, BlockState state) {
    if (block) {
        atomic_store(&block->state, state);
    }
}

bool block_try_transition(Block *block, BlockState from, BlockState to) {
    if (!block) return false;
    return atomic_compare_exchange_strong(&block->state, &from, to);
}

/* Capabilities */

void block_grant(Block *block, CapabilitySet caps) {
    if (block) {
        block->capabilities |= caps;
    }
}

void block_revoke(Block *block, CapabilitySet caps) {
    if (block) {
        block->capabilities &= ~caps;
    }
}

bool block_has_cap(const Block *block, Capability cap) {
    if (!block) return false;
    return (block->capabilities & cap) == cap;
}

bool block_check_cap(Block *block, Capability cap) {
    if (!block_has_cap(block, cap)) {
        block_crash(block, "capability denied");
        return false;
    }
    return true;
}

/* Linking */

bool block_link(Block *block, Pid other) {
    if (!block || other == PID_INVALID) return false;

    for (size_t i = 0; i < block->link_count; i++) {
        if (block->links[i] == other) {
            return true;
        }
    }

    if (block->link_count >= block->link_capacity) {
        uint32_t new_cap = block->link_capacity == 0 ? 4 : block->link_capacity * 2;
        Pid *new_links = realloc(block->links, sizeof(Pid) * new_cap);
        if (!new_links) return false;
        block->links = new_links;
        block->link_capacity = new_cap;
    }

    block->links[block->link_count++] = other;
    return true;
}

void block_unlink(Block *block, Pid other) {
    if (!block) return;

    for (size_t i = 0; i < block->link_count; i++) {
        if (block->links[i] == other) {
            block->links[i] = block->links[--block->link_count];
            return;
        }
    }
}

const Pid *block_get_links(const Block *block, size_t *count) {
    if (!block) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = block->link_count;
    return block->links;
}

/* Monitoring */

bool block_monitor(Block *block, Pid target) {
    if (!block || target == PID_INVALID) return false;

    for (size_t i = 0; i < block->monitor_count; i++) {
        if (block->monitors[i] == target) {
            return true;
        }
    }

    if (block->monitor_count >= block->monitor_capacity) {
        uint32_t new_cap = block->monitor_capacity == 0 ? 4 : block->monitor_capacity * 2;
        Pid *new_monitors = realloc(block->monitors, sizeof(Pid) * new_cap);
        if (!new_monitors) return false;
        block->monitors = new_monitors;
        block->monitor_capacity = new_cap;
    }

    block->monitors[block->monitor_count++] = target;
    return true;
}

void block_demonitor(Block *block, Pid target) {
    if (!block) return;

    for (size_t i = 0; i < block->monitor_count; i++) {
        if (block->monitors[i] == target) {
            block->monitors[i] = block->monitors[--block->monitor_count];
            return;
        }
    }
}

bool block_add_monitored_by(Block *block, Pid monitor_pid) {
    if (!block || monitor_pid == PID_INVALID) return false;

    for (size_t i = 0; i < block->monitored_by_count; i++) {
        if (block->monitored_by[i] == monitor_pid) {
            return true;
        }
    }

    if (block->monitored_by_count >= block->monitored_by_capacity) {
        uint32_t new_cap = block->monitored_by_capacity == 0 ? 4 : block->monitored_by_capacity * 2;
        Pid *new_monitored = realloc(block->monitored_by, sizeof(Pid) * new_cap);
        if (!new_monitored) return false;
        block->monitored_by = new_monitored;
        block->monitored_by_capacity = new_cap;
    }

    block->monitored_by[block->monitored_by_count++] = monitor_pid;
    return true;
}

void block_remove_monitored_by(Block *block, Pid monitor_pid) {
    if (!block) return;

    for (size_t i = 0; i < block->monitored_by_count; i++) {
        if (block->monitored_by[i] == monitor_pid) {
            block->monitored_by[i] = block->monitored_by[--block->monitored_by_count];
            return;
        }
    }
}

const Pid *block_get_monitors(const Block *block, size_t *count) {
    if (!block) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = block->monitor_count;
    return block->monitors;
}

/* Termination */

void block_exit(Block *block, int exit_code) {
    if (!block) return;

    atomic_store(&block->state, BLOCK_DEAD);
    block->u.exit.exit_code = exit_code;
    block->u.exit.exit_reason = NULL;
}

void block_crash(Block *block, const char *reason) {
    if (!block) return;

    atomic_store(&block->state, BLOCK_DEAD);
    block->u.exit.exit_code = -1;
    block->u.exit.exit_reason = reason ? strdup(reason) : NULL;
}

bool block_is_alive(const Block *block) {
    return block && atomic_load(&block->state) != BLOCK_DEAD;
}

/* Debug */

const char *block_state_name(BlockState state) {
    switch (state) {
    case BLOCK_RUNNABLE: return "RUNNABLE";
    case BLOCK_RUNNING:  return "RUNNING";
    case BLOCK_WAITING:  return "WAITING";
    case BLOCK_DEAD:     return "DEAD";
    default:             return "UNKNOWN";
    }
}

void block_print(const Block *block) {
    if (!block) {
        printf("Block: (null)\n");
        return;
    }

    BlockState state = atomic_load(&block->state);
    printf("Block {\n");
    printf("  pid: %lu\n", block->pid);
    printf("  name: %s\n", block->name ? block->name : "(none)");
    printf("  state: %s\n", block_state_name(state));

    if (state == BLOCK_DEAD) {
        printf("  exit_code: %d\n", block->u.exit.exit_code);
        if (block->u.exit.exit_reason) {
            printf("  exit_reason: %s\n", block->u.exit.exit_reason);
        }
    }

    printf("  capabilities: 0x%08x\n", block->capabilities);
    printf("  reductions: %zu\n", block->counters.reductions);
    printf("  heap_used: %zu bytes\n", heap_used(block->heap));
    printf("  links: %u\n", block->link_count);
    printf("}\n");
}
