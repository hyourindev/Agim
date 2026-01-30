/*
 * Agim - TCP Socket Layer
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "net/tcp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define SOCKET_ERROR_VAL SOCKET_ERROR
    #define close_socket closesocket
    #define sock_errno WSAGetLastError()
#else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VAL (-1)
    #define SOCKET_ERROR_VAL (-1)
    #define close_socket close
    #define sock_errno errno
#endif

/*============================================================================
 * Socket Structure
 *============================================================================*/

struct TCPSocket {
    socket_t fd;
    TCPError last_error;
    int timeout_ms;
};

/*============================================================================
 * Global State
 *============================================================================*/

static bool g_tcp_initialized = false;

bool tcp_init(void) {
    if (g_tcp_initialized) return true;

#ifdef _WIN32
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        return false;
    }
#endif

    g_tcp_initialized = true;
    return true;
}

void tcp_cleanup(void) {
    if (!g_tcp_initialized) return;

#ifdef _WIN32
    WSACleanup();
#endif

    g_tcp_initialized = false;
}

/*============================================================================
 * Connection
 *============================================================================*/

/**
 * Set socket to non-blocking mode (internal).
 */
static bool set_nonblocking_internal(socket_t fd, bool nonblocking) {
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags) == 0;
#endif
}

/**
 * Wait for socket to become writable (for connect timeout).
 */
static bool wait_writable(socket_t fd, int timeout_ms) {
#ifdef _WIN32
    fd_set write_fds, error_fds;
    struct timeval tv;

    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);
    FD_SET(fd, &write_fds);
    FD_SET(fd, &error_fds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int result = select(0, NULL, &write_fds, &error_fds, timeout_ms > 0 ? &tv : NULL);
    if (result <= 0) return false;
    if (FD_ISSET(fd, &error_fds)) return false;
    return FD_ISSET(fd, &write_fds);
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    int result = poll(&pfd, 1, timeout_ms);
    if (result <= 0) return false;
    if (pfd.revents & (POLLERR | POLLHUP)) return false;
    return (pfd.revents & POLLOUT) != 0;
#endif
}

TCPSocket *tcp_connect(const char *host, uint16_t port, int timeout_ms, TCPError *error) {
    if (!host) {
        if (error) *error = TCP_ERROR_RESOLVE;
        return NULL;
    }

    /* Ensure initialized */
    if (!g_tcp_initialized) {
        tcp_init();
    }

    TCPSocket *sock = calloc(1, sizeof(TCPSocket));
    if (!sock) {
        if (error) *error = TCP_ERROR_MEMORY;
        return NULL;
    }
    sock->fd = INVALID_SOCKET_VAL;
    sock->timeout_ms = timeout_ms;

    /* Resolve hostname */
    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    int gai_result = getaddrinfo(host, port_str, &hints, &result);
    if (gai_result != 0) {
        sock->last_error = TCP_ERROR_RESOLVE;
        if (error) *error = TCP_ERROR_RESOLVE;
        free(sock);
        return NULL;
    }

    /* Try each address until we connect */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sock->fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock->fd == INVALID_SOCKET_VAL) {
            continue;
        }

        /* Set non-blocking for connect timeout */
        if (timeout_ms > 0) {
            set_nonblocking_internal(sock->fd, true);
        }

        int connect_result = connect(sock->fd, rp->ai_addr, (int)rp->ai_addrlen);

        if (connect_result == 0) {
            /* Connected immediately */
            if (timeout_ms > 0) {
                set_nonblocking_internal(sock->fd, false);
            }
            break;
        }

#ifdef _WIN32
        if (sock_errno == WSAEWOULDBLOCK)
#else
        if (sock_errno == EINPROGRESS)
#endif
        {
            /* Connection in progress, wait for it */
            if (wait_writable(sock->fd, timeout_ms)) {
                /* Check if connection succeeded */
                int so_error;
                socklen_t len = sizeof(so_error);
                if (getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len) == 0 && so_error == 0) {
                    /* Connected */
                    set_nonblocking_internal(sock->fd, false);
                    break;
                }
            }
        }

        /* Connection failed, try next address */
        close_socket(sock->fd);
        sock->fd = INVALID_SOCKET_VAL;
    }

    freeaddrinfo(result);

    if (sock->fd == INVALID_SOCKET_VAL) {
        sock->last_error = TCP_ERROR_CONNECT;
        if (error) *error = TCP_ERROR_CONNECT;
        free(sock);
        return NULL;
    }

    /* Set TCP_NODELAY to disable Nagle's algorithm */
    int flag = 1;
    setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));

    /* Set default timeout */
    if (timeout_ms > 0) {
        tcp_set_timeout(sock, timeout_ms);
    }

    if (error) *error = TCP_OK;
    return sock;
}

void tcp_close(TCPSocket *sock) {
    if (!sock) return;
    if (sock->fd != INVALID_SOCKET_VAL) {
        close_socket(sock->fd);
    }
    free(sock);
}

/*============================================================================
 * I/O
 *============================================================================*/

ssize_t tcp_write(TCPSocket *sock, const void *data, size_t len) {
    if (!sock || sock->fd == INVALID_SOCKET_VAL || !data || len == 0) {
        return -1;
    }

#ifdef _WIN32
    int result = send(sock->fd, (const char *)data, (int)len, 0);
#else
    ssize_t result = send(sock->fd, data, len, MSG_NOSIGNAL);
#endif

    if (result < 0) {
        sock->last_error = TCP_ERROR_IO;
    }

    return result;
}

bool tcp_write_all(TCPSocket *sock, const void *data, size_t len) {
    if (!sock || !data) return false;

    const char *ptr = (const char *)data;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t written = tcp_write(sock, ptr, remaining);
        if (written <= 0) {
            return false;
        }
        ptr += written;
        remaining -= (size_t)written;
    }

    return true;
}

ssize_t tcp_read(TCPSocket *sock, void *buf, size_t len) {
    if (!sock || sock->fd == INVALID_SOCKET_VAL || !buf || len == 0) {
        return -1;
    }

#ifdef _WIN32
    int result = recv(sock->fd, (char *)buf, (int)len, 0);
#else
    ssize_t result = recv(sock->fd, buf, len, 0);
#endif

    if (result < 0) {
#ifdef _WIN32
        if (sock_errno == WSAETIMEDOUT)
#else
        if (sock_errno == EAGAIN || sock_errno == EWOULDBLOCK)
#endif
        {
            sock->last_error = TCP_ERROR_TIMEOUT;
        } else {
            sock->last_error = TCP_ERROR_IO;
        }
    } else if (result == 0) {
        sock->last_error = TCP_ERROR_CLOSED;
    }

    return result;
}

/*============================================================================
 * Options
 *============================================================================*/

bool tcp_set_timeout(TCPSocket *sock, int timeout_ms) {
    if (!sock || sock->fd == INVALID_SOCKET_VAL) return false;

    sock->timeout_ms = timeout_ms;

#ifdef _WIN32
    DWORD timeout = (DWORD)timeout_ms;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) != 0) {
        return false;
    }
    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)) != 0) {
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        return false;
    }
    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
        return false;
    }
#endif

    return true;
}

bool tcp_set_nonblocking(TCPSocket *sock, bool nonblocking) {
    if (!sock || sock->fd == INVALID_SOCKET_VAL) return false;
    return set_nonblocking_internal(sock->fd, nonblocking);
}

/*============================================================================
 * Info
 *============================================================================*/

int tcp_get_fd(TCPSocket *sock) {
    if (!sock) return -1;
    return (int)sock->fd;
}

TCPError tcp_last_error(TCPSocket *sock) {
    if (!sock) return TCP_ERROR_IO;
    return sock->last_error;
}

const char *tcp_error_string(TCPError error) {
    switch (error) {
    case TCP_OK:            return "Success";
    case TCP_ERROR_RESOLVE: return "DNS resolution failed";
    case TCP_ERROR_CONNECT: return "Connection failed";
    case TCP_ERROR_TIMEOUT: return "Operation timed out";
    case TCP_ERROR_CLOSED:  return "Connection closed";
    case TCP_ERROR_IO:      return "I/O error";
    case TCP_ERROR_MEMORY:  return "Memory allocation failed";
    default:                return "Unknown error";
    }
}
