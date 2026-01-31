/*
 * Agim - Mailbox (Message Queue)
 *
 * Message passing infrastructure for block communication.
 * Implements a lock-free MPSC (Multiple Producer, Single Consumer) queue
 * for high-performance message passing between blocks.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_MAILBOX_H
#define AGIM_RUNTIME_MAILBOX_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Value Value;

/* Types */

typedef uint64_t Pid;

#define PID_INVALID 0

/* Overflow Policies */

typedef enum OverflowPolicy {
    OVERFLOW_DROP_NEW,
    OVERFLOW_DROP_OLD,
    OVERFLOW_BLOCK_SENDER,
    OVERFLOW_CRASH,
} OverflowPolicy;

typedef enum SendResult {
    SEND_OK,
    SEND_FULL,
    SEND_WOULD_BLOCK,
    SEND_DEAD,
    SEND_ERROR,
} SendResult;

/* Message */

typedef struct Message {
    Pid sender;
    Value *value;
    _Atomic(struct Message *) next;
} Message;

/* Mailbox (Lock-Free MPSC Queue) */

typedef struct Mailbox {
    _Atomic(Message *) head;
    _Atomic(Message *) tail;
    _Atomic(size_t) count;
    Message stub;

    size_t max_messages;
    size_t max_bytes;
    OverflowPolicy overflow_policy;
    _Atomic(size_t) current_bytes;

    _Atomic(size_t) dropped_count;
    _Atomic(size_t) total_received;
} Mailbox;

/* Message Operations */

Message *message_new(Pid sender, Value *value);
void message_free(Message *msg);

/* Mailbox Operations */

void mailbox_init(Mailbox *mailbox);
void mailbox_free(Mailbox *mailbox);
bool mailbox_push(Mailbox *mailbox, Message *msg, size_t max_size);
SendResult mailbox_push_ex(Mailbox *mailbox, Message *msg);
Message *mailbox_pop(Mailbox *mailbox);
bool mailbox_empty(const Mailbox *mailbox);
size_t mailbox_count(const Mailbox *mailbox);

/* Configuration */

void mailbox_set_limits(Mailbox *mailbox, size_t max_messages, size_t max_bytes);
void mailbox_set_overflow_policy(Mailbox *mailbox, OverflowPolicy policy);
OverflowPolicy mailbox_get_overflow_policy(const Mailbox *mailbox);
size_t mailbox_dropped_count(const Mailbox *mailbox);
size_t mailbox_bytes_used(const Mailbox *mailbox);

#endif /* AGIM_RUNTIME_MAILBOX_H */
