/*
 * Agim - TCP Socket Layer
 *
 * Cross-platform TCP socket abstraction.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_NET_TCP_H
#define AGIM_NET_TCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/*============================================================================
 * Types
 *============================================================================*/

typedef struct TCPSocket TCPSocket;

typedef enum {
    TCP_OK = 0,
    TCP_ERROR_RESOLVE,      /* DNS resolution failed */
    TCP_ERROR_CONNECT,      /* Connection failed */
    TCP_ERROR_TIMEOUT,      /* Operation timed out */
    TCP_ERROR_CLOSED,       /* Connection closed by peer */
    TCP_ERROR_IO,           /* Read/write error */
    TCP_ERROR_MEMORY,       /* Memory allocation failed */
} TCPError;

/*============================================================================
 * Global Init
 *============================================================================*/

/**
 * Initialize TCP subsystem (required on Windows for Winsock).
 * Safe to call multiple times.
 * Returns true on success.
 */
bool tcp_init(void);

/**
 * Cleanup TCP subsystem.
 */
void tcp_cleanup(void);

/*============================================================================
 * Connection
 *============================================================================*/

/**
 * Connect to a remote host.
 *
 * @param host      Hostname or IP address
 * @param port      Port number
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @param error     Optional pointer to receive error code
 * @return          Socket handle or NULL on error
 */
TCPSocket *tcp_connect(const char *host, uint16_t port, int timeout_ms, TCPError *error);

/**
 * Close a TCP connection and free resources.
 */
void tcp_close(TCPSocket *sock);

/*============================================================================
 * I/O
 *============================================================================*/

/**
 * Write data to socket.
 *
 * @param sock      Socket handle
 * @param data      Data to write
 * @param len       Length of data
 * @return          Bytes written, or -1 on error
 */
ssize_t tcp_write(TCPSocket *sock, const void *data, size_t len);

/**
 * Write all data to socket (blocking until complete).
 *
 * @param sock      Socket handle
 * @param data      Data to write
 * @param len       Length of data
 * @return          true if all data written, false on error
 */
bool tcp_write_all(TCPSocket *sock, const void *data, size_t len);

/**
 * Read data from socket.
 *
 * @param sock      Socket handle
 * @param buf       Buffer to read into
 * @param len       Maximum bytes to read
 * @return          Bytes read, 0 on EOF, or -1 on error
 */
ssize_t tcp_read(TCPSocket *sock, void *buf, size_t len);

/*============================================================================
 * Options
 *============================================================================*/

/**
 * Set socket read/write timeout.
 *
 * @param sock       Socket handle
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return           true on success
 */
bool tcp_set_timeout(TCPSocket *sock, int timeout_ms);

/**
 * Set socket to non-blocking mode.
 *
 * @param sock        Socket handle
 * @param nonblocking true for non-blocking, false for blocking
 * @return            true on success
 */
bool tcp_set_nonblocking(TCPSocket *sock, bool nonblocking);

/*============================================================================
 * Info
 *============================================================================*/

/**
 * Get the underlying file descriptor (for select/poll).
 * Returns -1 on error.
 */
int tcp_get_fd(TCPSocket *sock);

/**
 * Get last error code.
 */
TCPError tcp_last_error(TCPSocket *sock);

/**
 * Get error message string.
 */
const char *tcp_error_string(TCPError error);

#endif /* AGIM_NET_TCP_H */
