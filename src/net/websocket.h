/*
 * Agim - WebSocket Protocol
 *
 * WebSocket client implementation (RFC 6455).
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_NET_WEBSOCKET_H
#define AGIM_NET_WEBSOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*============================================================================
 * Types
 *============================================================================*/

typedef struct WebSocket WebSocket;

typedef enum {
    WS_OK = 0,
    WS_ERROR_URL,           /* Invalid URL */
    WS_ERROR_CONNECT,       /* Connection failed */
    WS_ERROR_HANDSHAKE,     /* WebSocket handshake failed */
    WS_ERROR_PROTOCOL,      /* Protocol error */
    WS_ERROR_CLOSED,        /* Connection closed */
    WS_ERROR_IO,            /* I/O error */
    WS_ERROR_MEMORY,        /* Memory allocation failed */
    WS_ERROR_TIMEOUT,       /* Operation timed out */
} WSError;

/* WebSocket opcode types */
typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA,
} WSOpcode;

/*============================================================================
 * Connection
 *============================================================================*/

/**
 * Connect to a WebSocket server.
 *
 * @param url        WebSocket URL (ws:// or wss://)
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @param error      Optional pointer to receive error code
 * @return           WebSocket handle or NULL on error
 */
WebSocket *ws_connect(const char *url, int timeout_ms, WSError *error);

/**
 * Close a WebSocket connection.
 *
 * @param ws      WebSocket handle
 * @param code    Close status code (1000 = normal)
 * @param reason  Close reason string (may be NULL)
 */
void ws_close(WebSocket *ws, uint16_t code, const char *reason);

/*============================================================================
 * Send
 *============================================================================*/

/**
 * Send a text message.
 *
 * @param ws       WebSocket handle
 * @param message  Message string (UTF-8)
 * @return         true on success
 */
bool ws_send_text(WebSocket *ws, const char *message);

/**
 * Send a binary message.
 *
 * @param ws    WebSocket handle
 * @param data  Binary data
 * @param len   Data length
 * @return      true on success
 */
bool ws_send_binary(WebSocket *ws, const void *data, size_t len);

/**
 * Send a ping frame.
 *
 * @param ws    WebSocket handle
 * @param data  Optional ping data (may be NULL)
 * @param len   Data length
 * @return      true on success
 */
bool ws_send_ping(WebSocket *ws, const void *data, size_t len);

/*============================================================================
 * Receive
 *============================================================================*/

/**
 * Receive a message from the WebSocket.
 *
 * @param ws         WebSocket handle
 * @param len        Output: message length
 * @param opcode     Output: message type (WS_OPCODE_TEXT or WS_OPCODE_BINARY)
 * @param timeout_ms Timeout in milliseconds (-1 = block forever)
 * @return           Message data (caller must free), or NULL on close/error
 */
char *ws_recv(WebSocket *ws, size_t *len, int *opcode, int timeout_ms);

/*============================================================================
 * Status
 *============================================================================*/

/**
 * Check if WebSocket is connected.
 */
bool ws_is_connected(WebSocket *ws);

/**
 * Get last error code.
 */
WSError ws_last_error(WebSocket *ws);

/**
 * Get error message string.
 */
const char *ws_error_string(WSError error);

/**
 * Get close status code (available after close frame received).
 */
uint16_t ws_close_code(WebSocket *ws);

/**
 * Get close reason (available after close frame received).
 */
const char *ws_close_reason(WebSocket *ws);

#endif /* AGIM_NET_WEBSOCKET_H */
