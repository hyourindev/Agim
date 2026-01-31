/*
 * Agim - WebSocket Tests
 *
 * Tests for WebSocket client implementation.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/websocket.h"

/* Test Utilities */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s", #name); \
    fflush(stdout); \
    test_##name(); \
    printf(" PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf(" FAIL\n    Assertion failed: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define SKIP(msg) do { \
    printf(" SKIP (%s)\n", msg); \
    return; \
} while(0)

/* Tests */

TEST(error_strings) {
    ASSERT(strcmp(ws_error_string(WS_OK), "Success") == 0, "WS_OK");
    ASSERT(strcmp(ws_error_string(WS_ERROR_URL), "Invalid URL") == 0, "WS_ERROR_URL");
    ASSERT(strcmp(ws_error_string(WS_ERROR_CONNECT), "Connection failed") == 0, "WS_ERROR_CONNECT");
    ASSERT(strcmp(ws_error_string(WS_ERROR_HANDSHAKE), "WebSocket handshake failed") == 0, "WS_ERROR_HANDSHAKE");
    ASSERT(strcmp(ws_error_string(WS_ERROR_PROTOCOL), "Protocol error") == 0, "WS_ERROR_PROTOCOL");
    ASSERT(strcmp(ws_error_string(WS_ERROR_CLOSED), "Connection closed") == 0, "WS_ERROR_CLOSED");
    ASSERT(strcmp(ws_error_string(WS_ERROR_IO), "I/O error") == 0, "WS_ERROR_IO");
    ASSERT(strcmp(ws_error_string(WS_ERROR_MEMORY), "Memory allocation failed") == 0, "WS_ERROR_MEMORY");
    ASSERT(strcmp(ws_error_string(WS_ERROR_TIMEOUT), "Operation timed out") == 0, "WS_ERROR_TIMEOUT");
}

TEST(invalid_url) {
    WSError error;

    /* NULL URL */
    WebSocket *ws = ws_connect(NULL, 5000, &error);
    ASSERT(ws == NULL, "NULL URL should fail");
    ASSERT(error == WS_ERROR_URL, "Should return URL error");

    /* Invalid scheme */
    ws = ws_connect("http://example.com", 5000, &error);
    ASSERT(ws == NULL, "http:// should fail");
    ASSERT(error == WS_ERROR_URL, "Should return URL error");

    ws = ws_connect("https://example.com", 5000, &error);
    ASSERT(ws == NULL, "https:// should fail");
    ASSERT(error == WS_ERROR_URL, "Should return URL error");

    /* Empty URL */
    ws = ws_connect("", 5000, &error);
    ASSERT(ws == NULL, "Empty URL should fail");
    ASSERT(error == WS_ERROR_URL, "Should return URL error");
}

TEST(connection_refused) {
    WSError error;

    /* Try to connect to a port that's likely not listening */
    WebSocket *ws = ws_connect("ws://127.0.0.1:59999", 2000, &error);
    ASSERT(ws == NULL, "Connection to closed port should fail");
    ASSERT(error == WS_ERROR_CONNECT, "Should return connect error");
}

TEST(null_websocket_operations) {
    /* These should not crash with NULL */
    ASSERT(!ws_is_connected(NULL), "NULL ws should not be connected");
    ASSERT(ws_last_error(NULL) == WS_ERROR_IO, "NULL ws error should be IO");
    ASSERT(ws_close_code(NULL) == 0, "NULL ws close code should be 0");
    ASSERT(ws_close_reason(NULL) == NULL, "NULL ws close reason should be NULL");

    /* send/recv with NULL should return false/NULL */
    ASSERT(!ws_send_text(NULL, "test"), "send_text with NULL ws should fail");
    ASSERT(!ws_send_binary(NULL, "test", 4), "send_binary with NULL ws should fail");
    ASSERT(!ws_send_ping(NULL, NULL, 0), "send_ping with NULL ws should fail");

    size_t len;
    int opcode;
    char *data = ws_recv(NULL, &len, &opcode, 1000);
    ASSERT(data == NULL, "recv with NULL ws should return NULL");
    ASSERT(len == 0, "recv with NULL ws should set len to 0");

    /* Close with NULL should not crash */
    ws_close(NULL, 1000, "normal");
}

TEST(echo_websocket) {
    /* Connect to a public echo WebSocket server */
    WSError error;
    WebSocket *ws = ws_connect("wss://echo.websocket.events", 10000, &error);

    if (!ws) {
        /* Network may not be available, skip test */
        SKIP("Could not connect to echo server");
    }

    ASSERT(ws_is_connected(ws), "Should be connected");

    /* Send a text message */
    const char *test_msg = "Hello, WebSocket!";
    ASSERT(ws_send_text(ws, test_msg), "Send should succeed");

    /* Receive the echo */
    size_t len;
    int opcode;
    char *response = ws_recv(ws, &len, &opcode, 10000);

    /* The echo server may send a greeting first, so we may need to skip it */
    if (response && strstr(response, "Hello") == NULL) {
        free(response);
        response = ws_recv(ws, &len, &opcode, 10000);
    }

    ASSERT(response != NULL, "Should receive response");
    ASSERT(opcode == WS_OPCODE_TEXT, "Should be text message");
    ASSERT(strstr(response, "Hello") != NULL || strstr(response, test_msg) != NULL,
           "Response should contain our message");

    free(response);

    /* Close cleanly */
    ws_close(ws, 1000, "test complete");
}

TEST(binary_message) {
    WSError error;
    WebSocket *ws = ws_connect("wss://echo.websocket.events", 10000, &error);

    if (!ws) {
        SKIP("Could not connect to echo server");
    }

    /* Send binary data */
    uint8_t binary_data[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
    ASSERT(ws_send_binary(ws, binary_data, sizeof(binary_data)), "Binary send should succeed");

    /* Receive echo */
    size_t len;
    int opcode;
    char *response = ws_recv(ws, &len, &opcode, 10000);

    /* May need to skip greeting */
    if (response && opcode == WS_OPCODE_TEXT) {
        free(response);
        response = ws_recv(ws, &len, &opcode, 10000);
    }

    if (response) {
        ASSERT(opcode == WS_OPCODE_BINARY || opcode == WS_OPCODE_TEXT, "Should be binary or text");
        free(response);
    }

    ws_close(ws, 1000, NULL);
}

TEST(ping_pong) {
    WSError error;
    WebSocket *ws = ws_connect("wss://echo.websocket.events", 10000, &error);

    if (!ws) {
        SKIP("Could not connect to echo server");
    }

    /* Send ping */
    ASSERT(ws_send_ping(ws, "ping", 4), "Ping should succeed");

    /* Server should send pong, but it's handled internally by ws_recv */
    /* Just verify we can still communicate */
    ASSERT(ws_send_text(ws, "test"), "Should still be able to send");

    ws_close(ws, 1000, NULL);
}

TEST(close_codes) {
    /* Test close code constants */
    ASSERT(WS_OPCODE_CLOSE == 0x8, "Close opcode should be 0x8");
    ASSERT(WS_OPCODE_PING == 0x9, "Ping opcode should be 0x9");
    ASSERT(WS_OPCODE_PONG == 0xA, "Pong opcode should be 0xA");
    ASSERT(WS_OPCODE_TEXT == 0x1, "Text opcode should be 0x1");
    ASSERT(WS_OPCODE_BINARY == 0x2, "Binary opcode should be 0x2");
    ASSERT(WS_OPCODE_CONTINUATION == 0x0, "Continuation opcode should be 0x0");
}

/* Main */

int main(void) {
    printf("\n=== WebSocket Tests ===\n\n");

    RUN_TEST(error_strings);
    RUN_TEST(invalid_url);
    RUN_TEST(connection_refused);
    RUN_TEST(null_websocket_operations);
    RUN_TEST(close_codes);
    RUN_TEST(echo_websocket);
    RUN_TEST(binary_message);
    RUN_TEST(ping_pong);

    printf("\n=== Results: %d passed, %d failed ===\n\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
