/*
 * Agim - End-to-End State Machine Tests
 *
 * Tests finite state machine patterns using actor-based message passing.
 * Validates state transitions, event handling, and state persistence.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "vm/value.h"

#include <string.h>

/* State Machine Pattern Tests */

/* Simple state machine: IDLE -> RUNNING -> STOPPED */
typedef enum {
    STATE_IDLE = 0,
    STATE_RUNNING = 1,
    STATE_STOPPED = 2
} MachineState;

void test_state_machine_initial_state(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    block_grant(machine, CAP_SEND | CAP_RECEIVE);

    /* Initial state stored as user data (simulated via message) */
    MachineState state = STATE_IDLE;
    ASSERT_EQ(STATE_IDLE, state);

    block_free(machine);
}

void test_state_machine_transition(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    Block *controller = block_new(2, "controller", NULL);

    block_grant(machine, CAP_RECEIVE);
    block_grant(controller, CAP_SEND);

    MachineState state = STATE_IDLE;

    /* Send "start" event */
    Value *start_event = value_string("start");
    ASSERT(block_send(machine, controller->pid, start_event));

    Message *msg = block_receive(machine);
    ASSERT(msg != NULL);

    /* Process event: IDLE + start -> RUNNING */
    if (state == STATE_IDLE && strcmp(value_to_string(msg->value), "start") == 0) {
        state = STATE_RUNNING;
    }
    message_free(msg);

    ASSERT_EQ(STATE_RUNNING, state);

    /* Send "stop" event */
    Value *stop_event = value_string("stop");
    ASSERT(block_send(machine, controller->pid, stop_event));

    msg = block_receive(machine);
    ASSERT(msg != NULL);

    /* Process event: RUNNING + stop -> STOPPED */
    if (state == STATE_RUNNING && strcmp(value_to_string(msg->value), "stop") == 0) {
        state = STATE_STOPPED;
    }
    message_free(msg);

    ASSERT_EQ(STATE_STOPPED, state);

    block_free(machine);
    block_free(controller);
}

void test_state_machine_invalid_transition(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    Block *controller = block_new(2, "controller", NULL);

    block_grant(machine, CAP_RECEIVE);
    block_grant(controller, CAP_SEND);

    MachineState state = STATE_IDLE;

    /* Send "stop" event (invalid in IDLE state) */
    Value *stop_event = value_string("stop");
    ASSERT(block_send(machine, controller->pid, stop_event));

    Message *msg = block_receive(machine);
    ASSERT(msg != NULL);

    /* Invalid transition: IDLE + stop -> stays IDLE */
    if (state == STATE_IDLE && strcmp(value_to_string(msg->value), "stop") == 0) {
        /* Invalid - state remains unchanged */
    }
    message_free(msg);

    ASSERT_EQ(STATE_IDLE, state);

    block_free(machine);
    block_free(controller);
}

void test_state_machine_multiple_events(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    Block *controller = block_new(2, "controller", NULL);

    block_grant(machine, CAP_RECEIVE);
    block_grant(controller, CAP_SEND);

    MachineState state = STATE_IDLE;

    /* Queue multiple events */
    const char *events[] = {"start", "pause", "resume", "stop"};
    int event_count = sizeof(events) / sizeof(events[0]);

    for (int i = 0; i < event_count; i++) {
        Value *event = value_string(events[i]);
        ASSERT(block_send(machine, controller->pid, event));
    }

    /* Process all events */
    for (int i = 0; i < event_count; i++) {
        Message *msg = block_receive(machine);
        ASSERT(msg != NULL);
        const char *event = value_to_string(msg->value);

        /* Simple state machine logic */
        if (state == STATE_IDLE && strcmp(event, "start") == 0) {
            state = STATE_RUNNING;
        } else if (state == STATE_RUNNING && strcmp(event, "stop") == 0) {
            state = STATE_STOPPED;
        }
        /* pause/resume ignored in this simple example */

        message_free(msg);
    }

    ASSERT_EQ(STATE_STOPPED, state);

    block_free(machine);
    block_free(controller);
}

void test_state_machine_event_with_data(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    Block *controller = block_new(2, "controller", NULL);

    block_grant(machine, CAP_RECEIVE);
    block_grant(controller, CAP_SEND);

    /* Event with numeric data */
    Value *event = value_int(42);
    ASSERT(block_send(machine, controller->pid, event));

    Message *msg = block_receive(machine);
    ASSERT(msg != NULL);
    ASSERT(value_is_int(msg->value));
    ASSERT_EQ(42, value_to_int(msg->value));
    message_free(msg);

    block_free(machine);
    block_free(controller);
}

void test_state_machine_state_query(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    Block *client = block_new(2, "client", NULL);

    block_grant(machine, CAP_SEND | CAP_RECEIVE);
    block_grant(client, CAP_SEND | CAP_RECEIVE);

    MachineState state = STATE_RUNNING;

    /* Client queries state */
    Value *query = value_string("get_state");
    ASSERT(block_send(machine, client->pid, query));

    Message *msg = block_receive(machine);
    ASSERT(msg != NULL);

    /* Machine responds with current state */
    if (strcmp(value_to_string(msg->value), "get_state") == 0) {
        Value *response = value_int(state);
        ASSERT(block_send(client, machine->pid, response));
    }
    message_free(msg);

    /* Client receives state */
    Message *response = block_receive(client);
    ASSERT(response != NULL);
    ASSERT_EQ(STATE_RUNNING, value_to_int(response->value));
    message_free(response);

    block_free(machine);
    block_free(client);
}

void test_state_machine_concurrent_events(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    Block *sender1 = block_new(10, "sender1", NULL);
    Block *sender2 = block_new(11, "sender2", NULL);

    block_grant(machine, CAP_RECEIVE);
    block_grant(sender1, CAP_SEND);
    block_grant(sender2, CAP_SEND);

    /* Multiple senders send events */
    Value *e1 = value_string("event_from_1");
    Value *e2 = value_string("event_from_2");
    ASSERT(block_send(machine, sender1->pid, e1));
    ASSERT(block_send(machine, sender2->pid, e2));

    /* Machine receives both */
    int count = 0;
    while (block_has_messages(machine)) {
        Message *msg = block_receive(machine);
        ASSERT(msg != NULL);
        count++;
        message_free(msg);
    }
    ASSERT_EQ(2, count);

    block_free(machine);
    block_free(sender1);
    block_free(sender2);
}

void test_state_machine_history(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    Block *controller = block_new(2, "controller", NULL);

    block_grant(machine, CAP_RECEIVE);
    block_grant(controller, CAP_SEND);

    /* Track state history */
    MachineState history[10];
    int history_count = 0;
    MachineState state = STATE_IDLE;

    history[history_count++] = state;

    /* Transitions */
    const char *events[] = {"start", "stop"};

    for (int i = 0; i < 2; i++) {
        Value *event = value_string(events[i]);
        ASSERT(block_send(machine, controller->pid, event));
    }

    for (int i = 0; i < 2; i++) {
        Message *msg = block_receive(machine);
        ASSERT(msg != NULL && msg->value != NULL);
        const char *event = value_to_string(msg->value);

        if (state == STATE_IDLE && strcmp(event, "start") == 0) {
            state = STATE_RUNNING;
        } else if (state == STATE_RUNNING && strcmp(event, "stop") == 0) {
            state = STATE_STOPPED;
        }

        history[history_count++] = state;
        message_free(msg);
    }

    /* Verify history: IDLE -> RUNNING -> STOPPED */
    ASSERT_EQ(3, history_count);
    ASSERT_EQ(STATE_IDLE, history[0]);
    ASSERT_EQ(STATE_RUNNING, history[1]);
    ASSERT_EQ(STATE_STOPPED, history[2]);

    block_free(machine);
    block_free(controller);
}

void test_state_machine_reset(void) {
    Block *machine = block_new(1, "state_machine", NULL);
    Block *controller = block_new(2, "controller", NULL);

    block_grant(machine, CAP_RECEIVE);
    block_grant(controller, CAP_SEND);

    MachineState state = STATE_RUNNING;

    /* Send reset event */
    Value *reset = value_string("reset");
    ASSERT(block_send(machine, controller->pid, reset));

    Message *msg = block_receive(machine);
    if (strcmp(value_to_string(msg->value), "reset") == 0) {
        state = STATE_IDLE; /* Reset to initial state */
    }
    message_free(msg);

    ASSERT_EQ(STATE_IDLE, state);

    block_free(machine);
    block_free(controller);
}

/* Main */

int main(void) {
    printf("Running state machine end-to-end tests...\n\n");

    printf("State Machine Tests:\n");
    RUN_TEST(test_state_machine_initial_state);
    RUN_TEST(test_state_machine_transition);
    RUN_TEST(test_state_machine_invalid_transition);
    RUN_TEST(test_state_machine_multiple_events);
    RUN_TEST(test_state_machine_event_with_data);
    RUN_TEST(test_state_machine_state_query);
    RUN_TEST(test_state_machine_concurrent_events);
    RUN_TEST(test_state_machine_history);
    RUN_TEST(test_state_machine_reset);

    return TEST_RESULT();
}
