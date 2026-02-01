/*
 * Agim - End-to-End Echo Server Tests
 *
 * Tests echo server pattern using actor-based message passing.
 * Validates request-response patterns and concurrent clients.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "runtime/scheduler.h"
#include "vm/value.h"

#include <string.h>

/* Echo Server Pattern Tests */

void test_echo_server_basic(void) {
    Block *server = block_new(1, "server", NULL);
    Block *client = block_new(2, "client", NULL);

    block_grant(server, CAP_RECEIVE);
    block_grant(client, CAP_SEND | CAP_RECEIVE);

    /* Client sends request */
    Value *request = value_string("hello");
    ASSERT(block_send(server, client->pid, request));

    /* Server receives and echoes */
    ASSERT(block_has_messages(server));
    Message *received = block_receive(server);
    ASSERT(received != NULL);
    ASSERT(value_is_string(received->value));

    /* Echo back */
    Value *response = value_string(value_to_string(received->value));
    ASSERT(block_send(client, server->pid, response));
    message_free(received);

    /* Client receives echo */
    ASSERT(block_has_messages(client));
    Message *echo = block_receive(client);
    ASSERT(echo != NULL);
    ASSERT_STR_EQ("hello", value_to_string(echo->value));
    message_free(echo);

    block_free(server);
    block_free(client);
}

void test_echo_server_multiple_messages(void) {
    Block *server = block_new(1, "server", NULL);
    Block *client = block_new(2, "client", NULL);

    block_grant(server, CAP_RECEIVE);
    block_grant(client, CAP_SEND | CAP_RECEIVE);

    const char *messages[] = {"one", "two", "three"};
    int count = sizeof(messages) / sizeof(messages[0]);

    /* Client sends all messages */
    for (int i = 0; i < count; i++) {
        Value *request = value_string(messages[i]);
        ASSERT(block_send(server, client->pid, request));
    }

    /* Server echoes all */
    for (int i = 0; i < count; i++) {
        Message *received = block_receive(server);
        ASSERT(received != NULL);
        Value *response = value_string(value_to_string(received->value));
        ASSERT(block_send(client, server->pid, response));
        message_free(received);
    }

    /* Client receives all echoes in order */
    for (int i = 0; i < count; i++) {
        Message *echo = block_receive(client);
        ASSERT(echo != NULL);
        ASSERT_STR_EQ(messages[i], value_to_string(echo->value));
        message_free(echo);
    }

    block_free(server);
    block_free(client);
}

void test_echo_server_concurrent_clients(void) {
    Block *server = block_new(1, "server", NULL);
    Block *client1 = block_new(2, "client1", NULL);
    Block *client2 = block_new(3, "client2", NULL);
    Block *client3 = block_new(4, "client3", NULL);

    block_grant(server, CAP_RECEIVE);
    block_grant(client1, CAP_SEND | CAP_RECEIVE);
    block_grant(client2, CAP_SEND | CAP_RECEIVE);
    block_grant(client3, CAP_SEND | CAP_RECEIVE);

    /* All clients send */
    Value *r1 = value_string("from_client1");
    Value *r2 = value_string("from_client2");
    Value *r3 = value_string("from_client3");
    ASSERT(block_send(server, client1->pid, r1));
    ASSERT(block_send(server, client2->pid, r2));
    ASSERT(block_send(server, client3->pid, r3));

    /* Server echoes back to each sender */
    for (int i = 0; i < 3; i++) {
        Message *received = block_receive(server);
        ASSERT(received != NULL);
        Pid sender_pid = received->sender;
        Value *response = value_string(value_to_string(received->value));

        Block *sender = NULL;
        if (sender_pid == client1->pid) sender = client1;
        else if (sender_pid == client2->pid) sender = client2;
        else if (sender_pid == client3->pid) sender = client3;

        if (sender) {
            ASSERT(block_send(sender, server->pid, response));
        }
        message_free(received);
    }

    /* Each client gets its own echo */
    Message *e1 = block_receive(client1);
    ASSERT(e1 != NULL);
    ASSERT_STR_EQ("from_client1", value_to_string(e1->value));
    message_free(e1);

    Message *e2 = block_receive(client2);
    ASSERT(e2 != NULL);
    ASSERT_STR_EQ("from_client2", value_to_string(e2->value));
    message_free(e2);

    Message *e3 = block_receive(client3);
    ASSERT(e3 != NULL);
    ASSERT_STR_EQ("from_client3", value_to_string(e3->value));
    message_free(e3);

    block_free(server);
    block_free(client1);
    block_free(client2);
    block_free(client3);
}

void test_echo_server_empty_message(void) {
    Block *server = block_new(1, "server", NULL);
    Block *client = block_new(2, "client", NULL);

    block_grant(server, CAP_RECEIVE);
    block_grant(client, CAP_SEND | CAP_RECEIVE);

    /* Empty string should work */
    Value *request = value_string("");
    ASSERT(block_send(server, client->pid, request));

    Message *received = block_receive(server);
    ASSERT(received != NULL);
    ASSERT_STR_EQ("", value_to_string(received->value));
    message_free(received);

    block_free(server);
    block_free(client);
}

void test_echo_server_various_types(void) {
    Block *server = block_new(1, "server", NULL);
    Block *client = block_new(2, "client", NULL);

    block_grant(server, CAP_RECEIVE);
    block_grant(client, CAP_SEND);

    /* Integer */
    Value *int_val = value_int(42);
    ASSERT(block_send(server, client->pid, int_val));
    Message *m1 = block_receive(server);
    ASSERT(value_is_int(m1->value));
    ASSERT_EQ(42, value_to_int(m1->value));
    message_free(m1);

    /* Float */
    Value *float_val = value_float(3.14);
    ASSERT(block_send(server, client->pid, float_val));
    Message *m2 = block_receive(server);
    ASSERT(value_is_float(m2->value));
    message_free(m2);

    /* Bool */
    Value *bool_val = value_bool(true);
    ASSERT(block_send(server, client->pid, bool_val));
    Message *m3 = block_receive(server);
    ASSERT(value_is_bool(m3->value));
    message_free(m3);

    block_free(server);
    block_free(client);
}

void test_echo_server_large_message(void) {
    Block *server = block_new(1, "server", NULL);
    Block *client = block_new(2, "client", NULL);

    block_grant(server, CAP_RECEIVE);
    block_grant(client, CAP_SEND);

    /* Large string */
    char large[1024];
    memset(large, 'x', sizeof(large) - 1);
    large[sizeof(large) - 1] = '\0';

    Value *request = value_string(large);
    ASSERT(block_send(server, client->pid, request));

    Message *received = block_receive(server);
    ASSERT(received != NULL);
    ASSERT_EQ(1023, strlen(value_to_string(received->value)));
    message_free(received);

    block_free(server);
    block_free(client);
}

void test_echo_server_sender_tracking(void) {
    Block *server = block_new(100, "server", NULL);
    Block *client = block_new(200, "client", NULL);

    block_grant(server, CAP_RECEIVE);
    block_grant(client, CAP_SEND);

    Value *request = value_string("track me");
    ASSERT(block_send(server, client->pid, request));

    Message *received = block_receive(server);
    ASSERT(received != NULL);
    ASSERT_EQ(200, received->sender);
    message_free(received);

    block_free(server);
    block_free(client);
}

void test_echo_server_capability_required(void) {
    Block *server = block_new(1, "server", NULL);
    Block *client = block_new(2, "client", NULL);

    /* Granting capabilities ensures proper routing */
    block_grant(client, CAP_SEND);
    block_grant(server, CAP_RECEIVE);

    /* With proper capabilities, send should work */
    Value *request = value_string("capability test");
    ASSERT(block_send(server, client->pid, request));

    /* Drain message */
    Message *msg = block_receive(server);
    ASSERT(msg != NULL);
    ASSERT_STR_EQ("capability test", value_to_string(msg->value));
    message_free(msg);

    block_free(server);
    block_free(client);
}

/* Main */

int main(void) {
    printf("Running echo server end-to-end tests...\n\n");

    printf("Echo Server Tests:\n");
    RUN_TEST(test_echo_server_basic);
    RUN_TEST(test_echo_server_multiple_messages);
    RUN_TEST(test_echo_server_concurrent_clients);
    RUN_TEST(test_echo_server_empty_message);
    RUN_TEST(test_echo_server_various_types);
    RUN_TEST(test_echo_server_large_message);
    RUN_TEST(test_echo_server_sender_tracking);
    RUN_TEST(test_echo_server_capability_required);

    return TEST_RESULT();
}
