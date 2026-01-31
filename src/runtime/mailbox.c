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

/* Message Operations */

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
        value_free(msg->value);
    }
    free(msg);
}

/* Lock-Free MPSC Queue */

void mailbox_init(Mailbox *mailbox) {
    if (!mailbox) return;

    mailbox->stub.sender = PID_INVALID;
    mailbox->stub.value = NULL;
    atomic_store_explicit(&mailbox->stub.next, NULL, memory_order_relaxed);

    atomic_store_explicit(&mailbox->head, &mailbox->stub, memory_order_relaxed);
    atomic_store_explicit(&mailbox->tail, &mailbox->stub, memory_order_relaxed);
    atomic_store_explicit(&mailbox->count, 0, memory_order_relaxed);

    mailbox->max_messages = 0;
    mailbox->max_bytes = 0;
    mailbox->overflow_policy = OVERFLOW_DROP_NEW;
    atomic_store_explicit(&mailbox->current_bytes, 0, memory_order_relaxed);
    atomic_store_explicit(&mailbox->dropped_count, 0, memory_order_relaxed);
    atomic_store_explicit(&mailbox->total_received, 0, memory_order_relaxed);
}

void mailbox_free(Mailbox *mailbox) {
    if (!mailbox) return;

    Message *msg = atomic_load_explicit(&mailbox->head, memory_order_relaxed);

    while (msg) {
        Message *next = atomic_load_explicit(&msg->next, memory_order_relaxed);

        if (msg != &mailbox->stub) {
            message_free(msg);
        }

        msg = next;
    }

    atomic_store_explicit(&mailbox->stub.next, NULL, memory_order_relaxed);
    atomic_store_explicit(&mailbox->head, &mailbox->stub, memory_order_relaxed);
    atomic_store_explicit(&mailbox->tail, &mailbox->stub, memory_order_relaxed);
    atomic_store_explicit(&mailbox->count, 0, memory_order_relaxed);
    atomic_store_explicit(&mailbox->current_bytes, 0, memory_order_relaxed);
}

bool mailbox_push(Mailbox *mailbox, Message *msg, size_t max_size) {
    if (!mailbox || !msg) return false;

    if (max_size > 0) {
        size_t current = atomic_load_explicit(&mailbox->count, memory_order_relaxed);
        if (current >= max_size) {
            return false;
        }
    }

    atomic_store_explicit(&msg->next, NULL, memory_order_release);

    Message *prev = atomic_exchange_explicit(&mailbox->tail, msg, memory_order_acq_rel);

    atomic_store_explicit(&prev->next, msg, memory_order_release);

    atomic_fetch_add_explicit(&mailbox->count, 1, memory_order_relaxed);

    return true;
}

Message *mailbox_pop(Mailbox *mailbox) {
    if (!mailbox) return NULL;

    Message *head = atomic_load_explicit(&mailbox->head, memory_order_relaxed);
    Message *next = atomic_load_explicit(&head->next, memory_order_acquire);

    if (head == &mailbox->stub) {
        if (next == NULL) {
            return NULL;
        }

        atomic_store_explicit(&mailbox->head, next, memory_order_relaxed);
        head = next;
        next = atomic_load_explicit(&head->next, memory_order_acquire);
    }

    if (next != NULL) {
        atomic_store_explicit(&mailbox->head, next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&mailbox->count, 1, memory_order_relaxed);

        atomic_store_explicit(&head->next, NULL, memory_order_relaxed);
        return head;
    }

    Message *tail = atomic_load_explicit(&mailbox->tail, memory_order_acquire);
    if (head != tail) {
        int spin_count = 0;
        const int max_spins = 1000;
        int backoff = 1;

        do {
            next = atomic_load_explicit(&head->next, memory_order_acquire);
            if (next != NULL) break;

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
            return NULL;
        }

        atomic_store_explicit(&mailbox->head, next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&mailbox->count, 1, memory_order_relaxed);
        atomic_store_explicit(&head->next, NULL, memory_order_relaxed);
        return head;
    }

    atomic_store_explicit(&mailbox->stub.next, NULL, memory_order_relaxed);
    Message *stub = &mailbox->stub;

    Message *prev_tail = atomic_exchange_explicit(&mailbox->tail, stub, memory_order_acq_rel);
    atomic_store_explicit(&prev_tail->next, stub, memory_order_release);

    next = atomic_load_explicit(&head->next, memory_order_acquire);
    if (next != NULL) {
        atomic_store_explicit(&mailbox->head, next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&mailbox->count, 1, memory_order_relaxed);
        atomic_store_explicit(&head->next, NULL, memory_order_relaxed);
        return head;
    }

    return NULL;
}

bool mailbox_empty(const Mailbox *mailbox) {
    if (!mailbox) return true;

    return atomic_load_explicit(&mailbox->count, memory_order_relaxed) == 0;
}

size_t mailbox_count(const Mailbox *mailbox) {
    if (!mailbox) return 0;
    return atomic_load_explicit(&mailbox->count, memory_order_relaxed);
}

/* Extended Push with Overflow Policy */

static size_t estimate_message_size(Message *msg) {
    size_t size = sizeof(Message);
    if (msg && msg->value) {
        size += sizeof(Value);
        if (msg->value->type == VAL_STRING && msg->value->as.string) {
            size += msg->value->as.string->length;
        }
    }
    return size;
}

static bool mailbox_drop_oldest(Mailbox *mailbox) {
    Message *msg = mailbox_pop(mailbox);
    if (msg) {
        size_t msg_size = estimate_message_size(msg);
        atomic_fetch_sub_explicit(&mailbox->current_bytes, msg_size, memory_order_relaxed);
        message_free(msg);
        return true;
    }
    return false;
}

SendResult mailbox_push_ex(Mailbox *mailbox, Message *msg) {
    if (!mailbox || !msg) return SEND_ERROR;

    size_t msg_size = estimate_message_size(msg);

    if (mailbox->max_messages > 0) {
        size_t current = atomic_load_explicit(&mailbox->count, memory_order_relaxed);
        if (current >= mailbox->max_messages) {
            switch (mailbox->overflow_policy) {
            case OVERFLOW_DROP_NEW:
                atomic_fetch_add_explicit(&mailbox->dropped_count, 1, memory_order_relaxed);
                return SEND_FULL;

            case OVERFLOW_DROP_OLD:
                if (mailbox_drop_oldest(mailbox)) {
                    atomic_fetch_add_explicit(&mailbox->dropped_count, 1, memory_order_relaxed);
                } else {
                    return SEND_FULL;
                }
                break;

            case OVERFLOW_BLOCK_SENDER:
                return SEND_WOULD_BLOCK;

            case OVERFLOW_CRASH:
                return SEND_FULL;
            }
        }
    }

    if (mailbox->max_bytes > 0) {
        size_t current_bytes = atomic_load_explicit(&mailbox->current_bytes, memory_order_relaxed);
        if (current_bytes + msg_size > mailbox->max_bytes) {
            switch (mailbox->overflow_policy) {
            case OVERFLOW_DROP_NEW:
                atomic_fetch_add_explicit(&mailbox->dropped_count, 1, memory_order_relaxed);
                return SEND_FULL;

            case OVERFLOW_DROP_OLD:
                while (current_bytes + msg_size > mailbox->max_bytes) {
                    if (!mailbox_drop_oldest(mailbox)) {
                        break;
                    }
                    atomic_fetch_add_explicit(&mailbox->dropped_count, 1, memory_order_relaxed);
                    current_bytes = atomic_load_explicit(&mailbox->current_bytes, memory_order_relaxed);
                }
                break;

            case OVERFLOW_BLOCK_SENDER:
                return SEND_WOULD_BLOCK;

            case OVERFLOW_CRASH:
                return SEND_FULL;
            }
        }
    }

    atomic_store_explicit(&msg->next, NULL, memory_order_release);
    Message *prev = atomic_exchange_explicit(&mailbox->tail, msg, memory_order_acq_rel);
    atomic_store_explicit(&prev->next, msg, memory_order_release);
    atomic_fetch_add_explicit(&mailbox->count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&mailbox->current_bytes, msg_size, memory_order_relaxed);
    atomic_fetch_add_explicit(&mailbox->total_received, 1, memory_order_relaxed);

    return SEND_OK;
}

/* Configuration */

void mailbox_set_limits(Mailbox *mailbox, size_t max_messages, size_t max_bytes) {
    if (!mailbox) return;
    mailbox->max_messages = max_messages;
    mailbox->max_bytes = max_bytes;
}

void mailbox_set_overflow_policy(Mailbox *mailbox, OverflowPolicy policy) {
    if (!mailbox) return;
    mailbox->overflow_policy = policy;
}

OverflowPolicy mailbox_get_overflow_policy(const Mailbox *mailbox) {
    if (!mailbox) return OVERFLOW_DROP_NEW;
    return mailbox->overflow_policy;
}

size_t mailbox_dropped_count(const Mailbox *mailbox) {
    if (!mailbox) return 0;
    return atomic_load_explicit(&mailbox->dropped_count, memory_order_relaxed);
}

size_t mailbox_bytes_used(const Mailbox *mailbox) {
    if (!mailbox) return 0;
    return atomic_load_explicit(&mailbox->current_bytes, memory_order_relaxed);
}
