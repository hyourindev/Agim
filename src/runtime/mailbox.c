/*
 * Agim - Mailbox (Message Queue)
 *
 * Lock-free MPSC (Multiple Producer, Single Consumer) queue implementation.
 *
 * Based on Dmitry Vyukov's MPSC queue design with improvements for
 * robustness and memory ordering clarity.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "runtime/mailbox.h"
#include "vm/value.h"

#include <stdlib.h>

/*============================================================================
 * Message Operations
 *============================================================================*/

Message *message_new(Pid sender, Value *value) {
    Message *msg = malloc(sizeof(Message));
    if (!msg) return NULL;

    msg->sender = sender;
    msg->value = value;
    atomic_store_explicit(&msg->next, NULL, memory_order_relaxed);

    return msg;
}

void message_free(Message *msg) {
    if (!msg) return;
    if (msg->value) {
        /*
         * For message values, we're typically the last owner if refcount == 1.
         * Use value_free which respects refcount:
         * - If refcount > 1, just decrements (other refs exist)
         * - If refcount == 1, frees the value
         */
        value_free(msg->value);
    }
    free(msg);
}

/*============================================================================
 * Lock-Free MPSC Queue Implementation
 *============================================================================*/

void mailbox_init(Mailbox *mailbox) {
    if (!mailbox) return;

    /*
     * Initialize stub node. The stub is embedded in the Mailbox struct
     * to avoid a separate allocation. It has no payload.
     */
    mailbox->stub.sender = PID_INVALID;
    mailbox->stub.value = NULL;
    atomic_store_explicit(&mailbox->stub.next, NULL, memory_order_relaxed);

    /*
     * Both head and tail point to stub initially.
     * Head points to the stub (consumer will skip it on first pop).
     * Tail points to the stub (producers will link after it).
     */
    atomic_store_explicit(&mailbox->head, &mailbox->stub, memory_order_relaxed);
    atomic_store_explicit(&mailbox->tail, &mailbox->stub, memory_order_relaxed);
    atomic_store_explicit(&mailbox->count, 0, memory_order_relaxed);
}

void mailbox_free(Mailbox *mailbox) {
    if (!mailbox) return;

    /*
     * Free all messages except the stub.
     * This is NOT thread-safe - should only be called during cleanup
     * when no other threads access the mailbox.
     */
    Message *msg = atomic_load_explicit(&mailbox->head, memory_order_relaxed);

    while (msg) {
        Message *next = atomic_load_explicit(&msg->next, memory_order_relaxed);

        /* Don't free the stub - it's embedded in the Mailbox struct */
        if (msg != &mailbox->stub) {
            message_free(msg);
        }

        msg = next;
    }

    /* Reset to initial state */
    atomic_store_explicit(&mailbox->stub.next, NULL, memory_order_relaxed);
    atomic_store_explicit(&mailbox->head, &mailbox->stub, memory_order_relaxed);
    atomic_store_explicit(&mailbox->tail, &mailbox->stub, memory_order_relaxed);
    atomic_store_explicit(&mailbox->count, 0, memory_order_relaxed);
}

bool mailbox_push(Mailbox *mailbox, Message *msg, size_t max_size) {
    if (!mailbox || !msg) return false;

    /*
     * Check size limit (approximate - may slightly exceed due to races).
     * This is acceptable because exact enforcement would require locking.
     */
    if (max_size > 0) {
        size_t current = atomic_load_explicit(&mailbox->count, memory_order_relaxed);
        if (current >= max_size) {
            return false;
        }
    }

    /*
     * Prepare the new message: its next pointer must be NULL.
     * Use release to ensure message contents are visible before linking.
     */
    atomic_store_explicit(&msg->next, NULL, memory_order_release);

    /*
     * Atomically swap tail to point to this message.
     * The previous tail's next pointer will be updated to point to us.
     */
    Message *prev = atomic_exchange_explicit(&mailbox->tail, msg, memory_order_acq_rel);

    /*
     * Link the previous tail to this message.
     * Release ordering ensures message contents are visible to consumer
     * when they follow this link.
     */
    atomic_store_explicit(&prev->next, msg, memory_order_release);

    /* Increment count (approximate) */
    atomic_fetch_add_explicit(&mailbox->count, 1, memory_order_relaxed);

    return true;
}

Message *mailbox_pop(Mailbox *mailbox) {
    if (!mailbox) return NULL;

    /*
     * MPSC pop - only called by the single consumer (owning block).
     *
     * The head always points to either:
     * 1. The stub node (if queue is empty or only stub remains)
     * 2. A message that has already been "consumed" (its next is what we return)
     *
     * We return head->next, then advance head.
     */
    Message *head = atomic_load_explicit(&mailbox->head, memory_order_relaxed);
    Message *next = atomic_load_explicit(&head->next, memory_order_acquire);

    /*
     * If head is the stub, try to advance past it.
     */
    if (head == &mailbox->stub) {
        if (next == NULL) {
            /* Queue is empty */
            return NULL;
        }

        /*
         * Advance head past stub to first real message.
         * The stub is no longer at head, but it's still referenced by tail
         * or will be re-added later.
         */
        atomic_store_explicit(&mailbox->head, next, memory_order_relaxed);
        head = next;
        next = atomic_load_explicit(&head->next, memory_order_acquire);
    }

    /*
     * Now head points to a real message. Try to return it.
     */
    if (next != NULL) {
        /*
         * Normal case: advance head to next, return the old head.
         */
        atomic_store_explicit(&mailbox->head, next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&mailbox->count, 1, memory_order_relaxed);

        /* Clear next pointer before returning */
        atomic_store_explicit(&head->next, NULL, memory_order_relaxed);
        return head;
    }

    /*
     * head->next is NULL. This could mean:
     * 1. head is the last message (and tail points to it)
     * 2. A producer is in the middle of pushing (tail updated but next not yet)
     *
     * Check if head is tail.
     */
    Message *tail = atomic_load_explicit(&mailbox->tail, memory_order_acquire);
    if (head != tail) {
        /*
         * A push is in progress - the producer has claimed tail but hasn't
         * linked next yet. Spin briefly waiting for the link.
         *
         * Use bounded spin with exponential backoff to prevent hangs if
         * producer crashes mid-push. After max iterations, return NULL
         * and retry later.
         */
        int spin_count = 0;
        const int max_spins = 1000;
        int backoff = 1;

        do {
            next = atomic_load_explicit(&head->next, memory_order_acquire);
            if (next != NULL) break;

            /* Exponential backoff with spin-wait */
            for (int i = 0; i < backoff; i++) {
#if defined(__x86_64__) || defined(__i386__)
                __asm__ __volatile__("pause" ::: "memory");
#elif defined(__aarch64__)
                __asm__ __volatile__("yield" ::: "memory");
#endif
            }
            if (backoff < 64) backoff *= 2;
            spin_count++;
        } while (spin_count < max_spins);

        if (next == NULL) {
            /*
             * Producer didn't complete in time. Return NULL and the caller
             * will retry later. This prevents hanging if producer crashed.
             */
            return NULL;
        }

        atomic_store_explicit(&mailbox->head, next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&mailbox->count, 1, memory_order_relaxed);
        atomic_store_explicit(&head->next, NULL, memory_order_relaxed);
        return head;
    }

    /*
     * head == tail and next is NULL: queue has one message but we need
     * to keep the stub invariant. Re-add stub as the new tail so we can
     * pop the message.
     */
    atomic_store_explicit(&mailbox->stub.next, NULL, memory_order_relaxed);
    Message *stub = &mailbox->stub;

    /*
     * Push stub as the new tail.
     */
    Message *prev_tail = atomic_exchange_explicit(&mailbox->tail, stub, memory_order_acq_rel);
    atomic_store_explicit(&prev_tail->next, stub, memory_order_release);

    /*
     * Now head->next should be stub (or another message if races happened).
     * Retry the pop logic.
     */
    next = atomic_load_explicit(&head->next, memory_order_acquire);
    if (next != NULL) {
        atomic_store_explicit(&mailbox->head, next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&mailbox->count, 1, memory_order_relaxed);
        atomic_store_explicit(&head->next, NULL, memory_order_relaxed);
        return head;
    }

    /*
     * Still NULL - this shouldn't happen if our stub push succeeded,
     * but handle gracefully by returning NULL (will retry later).
     */
    return NULL;
}

bool mailbox_empty(const Mailbox *mailbox) {
    if (!mailbox) return true;

    /*
     * Check if count is zero. This is approximate due to concurrent
     * push/pop operations but good enough for most uses.
     */
    return atomic_load_explicit(&mailbox->count, memory_order_relaxed) == 0;
}

size_t mailbox_count(const Mailbox *mailbox) {
    if (!mailbox) return 0;
    return atomic_load_explicit(&mailbox->count, memory_order_relaxed);
}
