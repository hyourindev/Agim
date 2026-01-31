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

/* Forward declarations */
typedef struct Value Value;

/*============================================================================
 * Types
 *============================================================================*/

typedef uint64_t Pid;

#define PID_INVALID 0

/*============================================================================
 * Overflow Policies (Backpressure)
 *============================================================================*/

/**
 * Policy for handling mailbox overflow.
 */
typedef enum OverflowPolicy {
    OVERFLOW_DROP_NEW,      /* Drop incoming message (default) */
    OVERFLOW_DROP_OLD,      /* Drop oldest message to make room */
    OVERFLOW_BLOCK_SENDER,  /* Block sender until space available */
    OVERFLOW_CRASH,         /* Crash the receiver */
} OverflowPolicy;

/**
 * Result of sending a message.
 */
typedef enum SendResult {
    SEND_OK,                /* Message sent successfully */
    SEND_FULL,              /* Mailbox full, message dropped */
    SEND_WOULD_BLOCK,       /* Would block (for OVERFLOW_BLOCK_SENDER) */
    SEND_DEAD,              /* Target block is dead */
    SEND_ERROR,             /* Other error */
} SendResult;

/*============================================================================
 * Message Structure
 *============================================================================*/

/**
 * A message in the mailbox queue.
 * The 'next' pointer is atomic for lock-free operations.
 */
typedef struct Message {
    Pid sender;                     /* PID of sender block */
    Value *value;                   /* Message payload (deep copied) */
    _Atomic(struct Message *) next; /* Next message in queue (atomic) */
} Message;

/*============================================================================
 * Mailbox Structure (Lock-Free MPSC Queue)
 *============================================================================*/

/**
 * Lock-free MPSC (Multiple Producer, Single Consumer) queue.
 *
 * Design: Uses a Michael-Scott style queue with a stub node.
 * - Multiple threads can push (send) concurrently without locks
 * - Single thread pops (receives) without contention
 * - Stub node eliminates edge cases for empty queue
 *
 * Memory ordering:
 * - Push uses release semantics to publish the message
 * - Pop uses acquire semantics to read the message
 * - This ensures message contents are visible to the receiver
 */
typedef struct Mailbox {
    _Atomic(Message *) head;        /* Consumer reads from head */
    _Atomic(Message *) tail;        /* Producers push at tail */
    _Atomic(size_t) count;          /* Approximate message count */
    Message stub;                   /* Stub node (always present) */

    /* Backpressure configuration */
    size_t max_messages;            /* Maximum messages (0 = unlimited) */
    size_t max_bytes;               /* Maximum bytes (0 = unlimited) */
    OverflowPolicy overflow_policy; /* What to do when full */
    _Atomic(size_t) current_bytes;  /* Approximate byte usage */

    /* Statistics */
    _Atomic(size_t) dropped_count;  /* Messages dropped due to overflow */
    _Atomic(size_t) total_received; /* Total messages ever received */
} Mailbox;

/*============================================================================
 * Message Operations
 *============================================================================*/

/**
 * Create a new message.
 * The value is NOT copied - caller provides an already-copied value.
 */
Message *message_new(Pid sender, Value *value);

/**
 * Free a message and its value.
 */
void message_free(Message *msg);

/*============================================================================
 * Mailbox Operations
 *============================================================================*/

/**
 * Initialize a mailbox.
 * Sets up the stub node for lock-free operations.
 */
void mailbox_init(Mailbox *mailbox);

/**
 * Free all messages in a mailbox.
 * NOT thread-safe - call only when no other threads access the mailbox.
 */
void mailbox_free(Mailbox *mailbox);

/**
 * Push a message to the mailbox (lock-free, thread-safe).
 * Multiple threads can call this concurrently.
 * Returns false if max_size would be exceeded.
 *
 * Memory ordering: Uses release semantics to ensure message
 * contents are visible to the consumer.
 */
bool mailbox_push(Mailbox *mailbox, Message *msg, size_t max_size);

/**
 * Push a message with overflow policy handling.
 * Returns SEND_OK on success, or appropriate error code.
 */
SendResult mailbox_push_ex(Mailbox *mailbox, Message *msg);

/**
 * Pop a message from the mailbox (single consumer only).
 * Returns NULL if empty.
 * Only ONE thread should call this (the owning block).
 *
 * Memory ordering: Uses acquire semantics to see message
 * contents published by producers.
 */
Message *mailbox_pop(Mailbox *mailbox);

/**
 * Check if mailbox is empty (approximate, thread-safe).
 * May briefly return true even if a push is in progress.
 */
bool mailbox_empty(const Mailbox *mailbox);

/**
 * Get mailbox count (approximate, thread-safe).
 * Count may be slightly stale due to concurrent operations.
 */
size_t mailbox_count(const Mailbox *mailbox);

/*============================================================================
 * Mailbox Configuration
 *============================================================================*/

/**
 * Set mailbox limits.
 * max_messages: Maximum number of messages (0 = unlimited)
 * max_bytes: Maximum bytes used by messages (0 = unlimited)
 */
void mailbox_set_limits(Mailbox *mailbox, size_t max_messages, size_t max_bytes);

/**
 * Set overflow policy.
 */
void mailbox_set_overflow_policy(Mailbox *mailbox, OverflowPolicy policy);

/**
 * Get overflow policy.
 */
OverflowPolicy mailbox_get_overflow_policy(const Mailbox *mailbox);

/**
 * Get number of dropped messages.
 */
size_t mailbox_dropped_count(const Mailbox *mailbox);

/**
 * Get approximate byte usage.
 */
size_t mailbox_bytes_used(const Mailbox *mailbox);

#endif /* AGIM_RUNTIME_MAILBOX_H */
