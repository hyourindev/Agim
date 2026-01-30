/*
 * Agim - TLS Layer
 *
 * TLS/SSL support using BearSSL for secure connections.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "net/tls.h"
#include "net/tcp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <bcrypt.h>
    #pragma comment(lib, "bcrypt.lib")
#else
    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>
#endif

#include <bearssl.h>
#include "net/cacerts.h"

/*============================================================================
 * Constants
 *============================================================================*/

#define TLS_IO_BUF_SIZE  BR_SSL_BUFSIZE_BIDI
#define TLS_READ_TIMEOUT_MS 30000

/*============================================================================
 * TLS Socket Structure
 *============================================================================*/

struct TLSSocket {
    TCPSocket *tcp;                     /* Underlying TCP socket */
    br_ssl_client_context sc;           /* BearSSL client context */
    br_x509_minimal_context xc;         /* X.509 validation context */
    unsigned char iobuf[TLS_IO_BUF_SIZE]; /* I/O buffer for BearSSL */
    br_sslio_context ioc;               /* Simplified I/O context */
    TLSError last_error;                /* Last error code */
    bool connected;                     /* Connection state */
    int timeout_ms;                     /* Operation timeout */
};

/*============================================================================
 * Global State
 *============================================================================*/

static bool g_tls_initialized = false;

/*============================================================================
 * BearSSL I/O Callbacks
 *============================================================================*/

/**
 * Low-level data read callback for BearSSL.
 * Reads data from the underlying TCP socket.
 */
static int tls_sock_read(void *ctx, unsigned char *buf, size_t len) {
    TLSSocket *sock = (TLSSocket *)ctx;

    for (;;) {
        ssize_t rlen = tcp_read(sock->tcp, buf, len);
        if (rlen < 0) {
            TCPError err = tcp_last_error(sock->tcp);
            if (err == TCP_ERROR_TIMEOUT) {
                /* For non-blocking, return -1 to indicate would block */
                return -1;
            }
            return -1;
        }
        if (rlen == 0) {
            /* EOF */
            return -1;
        }
        return (int)rlen;
    }
}

/**
 * Low-level data write callback for BearSSL.
 * Writes data to the underlying TCP socket.
 */
static int tls_sock_write(void *ctx, const unsigned char *buf, size_t len) {
    TLSSocket *sock = (TLSSocket *)ctx;

    for (;;) {
        ssize_t wlen = tcp_write(sock->tcp, buf, len);
        if (wlen < 0) {
            return -1;
        }
        if (wlen == 0) {
            return -1;
        }
        return (int)wlen;
    }
}

/*============================================================================
 * Initialization
 *============================================================================*/

bool tls_init(void) {
    if (g_tls_initialized) return true;

    /* Initialize TCP layer */
    if (!tcp_init()) return false;

    g_tls_initialized = true;
    return true;
}

void tls_cleanup(void) {
    if (!g_tls_initialized) return;

    tcp_cleanup();
    g_tls_initialized = false;
}

/*============================================================================
 * Connection
 *============================================================================*/

TLSSocket *tls_connect(const char *host, uint16_t port,
                       int timeout_ms, TLSError *error) {
    if (!host) {
        if (error) *error = TLS_ERROR_CONNECT;
        return NULL;
    }

    /* Ensure initialized */
    if (!g_tls_initialized) {
        tls_init();
    }

    /* Allocate socket structure */
    TLSSocket *sock = calloc(1, sizeof(TLSSocket));
    if (!sock) {
        if (error) *error = TLS_ERROR_MEMORY;
        return NULL;
    }
    sock->timeout_ms = timeout_ms > 0 ? timeout_ms : TLS_READ_TIMEOUT_MS;

    /* Connect TCP socket */
    TCPError tcp_err;
    sock->tcp = tcp_connect(host, port, sock->timeout_ms, &tcp_err);
    if (!sock->tcp) {
        sock->last_error = TLS_ERROR_CONNECT;
        if (error) *error = TLS_ERROR_CONNECT;
        free(sock);
        return NULL;
    }

    /* Initialize BearSSL client context with full profile and trust anchors */
    br_ssl_client_init_full(&sock->sc, &sock->xc, TAS, TAS_NUM);

    /* Set the I/O buffer */
    br_ssl_engine_set_buffer(&sock->sc.eng, sock->iobuf, sizeof(sock->iobuf), 1);

    /* Reset client context for new handshake with SNI hostname */
    br_ssl_client_reset(&sock->sc, host, 0);

    /* Initialize simplified I/O wrapper */
    br_sslio_init(&sock->ioc, &sock->sc.eng,
                  tls_sock_read, sock,
                  tls_sock_write, sock);

    /* Perform handshake by doing a flush (which triggers the handshake) */
    if (br_sslio_flush(&sock->ioc) < 0) {
        int err = br_ssl_engine_last_error(&sock->sc.eng);
        sock->last_error = TLS_ERROR_HANDSHAKE;

        /* Check for certificate errors */
        if (err >= BR_ERR_X509_OK && err <= BR_ERR_X509_TIME_UNKNOWN) {
            sock->last_error = TLS_ERROR_CERTIFICATE;
        }

        if (error) *error = sock->last_error;
        tcp_close(sock->tcp);
        free(sock);
        return NULL;
    }

    /* Check if engine is in a good state */
    unsigned state = br_ssl_engine_current_state(&sock->sc.eng);
    if (state == BR_SSL_CLOSED) {
        int err = br_ssl_engine_last_error(&sock->sc.eng);
        sock->last_error = (err == 0) ? TLS_ERROR_CLOSED : TLS_ERROR_HANDSHAKE;
        if (error) *error = sock->last_error;
        tcp_close(sock->tcp);
        free(sock);
        return NULL;
    }

    sock->connected = true;
    if (error) *error = TLS_OK;
    return sock;
}

void tls_close(TLSSocket *sock) {
    if (!sock) return;

    if (sock->connected) {
        /* Send close_notify alert */
        br_sslio_close(&sock->ioc);
    }

    if (sock->tcp) {
        tcp_close(sock->tcp);
    }

    free(sock);
}

/*============================================================================
 * I/O Operations
 *============================================================================*/

ssize_t tls_read(TLSSocket *sock, void *buf, size_t len) {
    if (!sock || !buf || len == 0) {
        return -1;
    }

    if (!sock->connected) {
        sock->last_error = TLS_ERROR_CLOSED;
        return -1;
    }

    int result = br_sslio_read(&sock->ioc, buf, len);
    if (result < 0) {
        /* Check why we failed */
        unsigned state = br_ssl_engine_current_state(&sock->sc.eng);
        if (state == BR_SSL_CLOSED) {
            int err = br_ssl_engine_last_error(&sock->sc.eng);
            if (err == 0) {
                /* Clean close */
                sock->connected = false;
                sock->last_error = TLS_ERROR_CLOSED;
                return 0;
            } else {
                sock->connected = false;
                sock->last_error = TLS_ERROR_IO;
                return -1;
            }
        }
        sock->last_error = TLS_ERROR_IO;
        return -1;
    }

    return result;
}

ssize_t tls_write(TLSSocket *sock, const void *data, size_t len) {
    if (!sock || !data || len == 0) {
        return -1;
    }

    if (!sock->connected) {
        sock->last_error = TLS_ERROR_CLOSED;
        return -1;
    }

    int result = br_sslio_write(&sock->ioc, data, len);
    if (result < 0) {
        sock->last_error = TLS_ERROR_IO;
        sock->connected = false;
        return -1;
    }

    /* Flush to ensure data is sent */
    if (br_sslio_flush(&sock->ioc) < 0) {
        sock->last_error = TLS_ERROR_IO;
        sock->connected = false;
        return -1;
    }

    return result;
}

bool tls_write_all(TLSSocket *sock, const void *data, size_t len) {
    if (!sock || !data) return false;

    if (!sock->connected) {
        sock->last_error = TLS_ERROR_CLOSED;
        return false;
    }

    int result = br_sslio_write_all(&sock->ioc, data, len);
    if (result < 0) {
        sock->last_error = TLS_ERROR_IO;
        sock->connected = false;
        return false;
    }

    /* Flush to ensure data is sent */
    if (br_sslio_flush(&sock->ioc) < 0) {
        sock->last_error = TLS_ERROR_IO;
        sock->connected = false;
        return false;
    }

    return true;
}

/*============================================================================
 * Info
 *============================================================================*/

int tls_get_fd(TLSSocket *sock) {
    if (!sock || !sock->tcp) return -1;
    return tcp_get_fd(sock->tcp);
}

TLSError tls_last_error(TLSSocket *sock) {
    if (!sock) return TLS_ERROR_IO;
    return sock->last_error;
}

bool tls_is_connected(TLSSocket *sock) {
    if (!sock) return false;
    return sock->connected;
}

const char *tls_error_string(TLSError error) {
    switch (error) {
    case TLS_OK:                return "Success";
    case TLS_ERROR_HANDSHAKE:   return "TLS handshake failed";
    case TLS_ERROR_CERTIFICATE: return "Certificate validation failed";
    case TLS_ERROR_HOSTNAME:    return "Hostname verification failed";
    case TLS_ERROR_IO:          return "I/O error";
    case TLS_ERROR_CLOSED:      return "Connection closed";
    case TLS_ERROR_MEMORY:      return "Memory allocation failed";
    case TLS_ERROR_CONNECT:     return "TCP connection failed";
    case TLS_ERROR_TIMEOUT:     return "Operation timed out";
    default:                    return "Unknown TLS error";
    }
}
