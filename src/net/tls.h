/*
 * Agim - TLS Layer
 *
 * TLS/SSL support wrapping BearSSL for secure connections.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_NET_TLS_H
#define AGIM_NET_TLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/*============================================================================
 * Types
 *============================================================================*/

typedef struct TLSSocket TLSSocket;

typedef enum {
    TLS_OK = 0,
    TLS_ERROR_HANDSHAKE,    /* TLS handshake failed */
    TLS_ERROR_CERTIFICATE,  /* Certificate validation failed */
    TLS_ERROR_HOSTNAME,     /* Hostname verification failed */
    TLS_ERROR_IO,           /* I/O error during TLS operation */
    TLS_ERROR_CLOSED,       /* Connection closed by peer */
    TLS_ERROR_MEMORY,       /* Memory allocation failed */
    TLS_ERROR_CONNECT,      /* TCP connection failed */
    TLS_ERROR_TIMEOUT,      /* Operation timed out */
} TLSError;

/*============================================================================
 * Global Init
 *============================================================================*/

/**
 * Initialize TLS subsystem.
 * Loads embedded CA certificates.
 * Safe to call multiple times.
 * Returns true on success.
 */
bool tls_init(void);

/**
 * Cleanup TLS subsystem.
 */
void tls_cleanup(void);

/*============================================================================
 * Connection
 *============================================================================*/

/**
 * Connect to a remote host over TLS.
 *
 * @param host       Hostname (used for SNI and certificate verification)
 * @param port       Port number
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @param error      Optional pointer to receive error code
 * @return           TLS socket handle or NULL on error
 */
TLSSocket *tls_connect(const char *host, uint16_t port,
                       int timeout_ms, TLSError *error);

/**
 * Close a TLS connection and free resources.
 */
void tls_close(TLSSocket *sock);

/*============================================================================
 * I/O
 *============================================================================*/

/**
 * Read data from TLS socket.
 *
 * @param sock      TLS socket handle
 * @param buf       Buffer to read into
 * @param len       Maximum bytes to read
 * @return          Bytes read, 0 on EOF, or -1 on error
 */
ssize_t tls_read(TLSSocket *sock, void *buf, size_t len);

/**
 * Write data to TLS socket.
 *
 * @param sock      TLS socket handle
 * @param data      Data to write
 * @param len       Length of data
 * @return          Bytes written, or -1 on error
 */
ssize_t tls_write(TLSSocket *sock, const void *data, size_t len);

/**
 * Write all data to TLS socket (blocking until complete).
 *
 * @param sock      TLS socket handle
 * @param data      Data to write
 * @param len       Length of data
 * @return          true if all data written, false on error
 */
bool tls_write_all(TLSSocket *sock, const void *data, size_t len);

/*============================================================================
 * Info
 *============================================================================*/

/**
 * Get the underlying file descriptor (for select/poll).
 * Returns -1 on error.
 */
int tls_get_fd(TLSSocket *sock);

/**
 * Get last error code from a TLS socket.
 */
TLSError tls_last_error(TLSSocket *sock);

/**
 * Get error message string for a TLS error code.
 */
const char *tls_error_string(TLSError error);

/**
 * Check if TLS socket is connected.
 */
bool tls_is_connected(TLSSocket *sock);

#endif /* AGIM_NET_TLS_H */
