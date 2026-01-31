/*
 * Agim - Distributed Node Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "dist/node.h"
#include "runtime/serialize.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/*============================================================================
 * Time Helper
 *============================================================================*/

static uint64_t current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/*============================================================================
 * Configuration
 *============================================================================*/

NodeConfig node_config_default(void) {
    return (NodeConfig){
        .name = "node",
        .host = "0.0.0.0",
        .port = 9000,
        .cookie = 0,
        .heartbeat_ms = 5000,
        .timeout_ms = 10000,
    };
}

/*============================================================================
 * Node Lifecycle
 *============================================================================*/

DistributedNode *node_new(const NodeConfig *config) {
    DistributedNode *node = calloc(1, sizeof(DistributedNode));
    if (!node) return NULL;

    NodeConfig cfg = config ? *config : node_config_default();

    /* Set up local identity */
    strncpy(node->local.name, cfg.name, NODE_NAME_MAX - 1);
    strncpy(node->local.host, cfg.host, NODE_HOST_MAX - 1);
    node->local.port = cfg.port;
    node->local.cookie = cfg.cookie;
    node->local.node_id = (uint64_t)current_time_ms();  /* Simple unique ID */

    node->config = cfg;
    node->peers = NULL;
    node->peer_count = 0;
    node->listen_fd = -1;
    node->accept_running = false;
    node->monitors = NULL;
    node->monitor_count = 0;
    node->running = false;

    node->callback_ctx = NULL;
    node->on_node_up = NULL;
    node->on_node_down = NULL;
    node->on_message = NULL;

    pthread_mutex_init(&node->lock, NULL);

    return node;
}

void node_free(DistributedNode *node) {
    if (!node) return;

    node_stop(node);

    pthread_mutex_lock(&node->lock);

    /* Free peer connections */
    NodeConnection *peer = node->peers;
    while (peer) {
        NodeConnection *next = peer->next;
        if (peer->socket_fd >= 0) {
            close(peer->socket_fd);
        }
        free(peer);
        peer = next;
    }

    /* Free monitors */
    NodeMonitor *mon = node->monitors;
    while (mon) {
        NodeMonitor *next = mon->next;
        free(mon);
        mon = next;
    }

    pthread_mutex_unlock(&node->lock);
    pthread_mutex_destroy(&node->lock);

    free(node);
}

/*============================================================================
 * Accept Thread
 *============================================================================*/

static void *accept_thread_fn(void *arg) {
    DistributedNode *node = (DistributedNode *)arg;

    while (node->accept_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(node->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }

        /* Set TCP_NODELAY */
        int flag = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        /* TODO: Handle incoming connection properly
         * - Read handshake
         * - Verify cookie
         * - Add to peers list
         * - Start receiver thread
         */

        /* For now, just close (basic infrastructure) */
        close(client_fd);
    }

    return NULL;
}

bool node_start(DistributedNode *node) {
    if (!node || node->running) return false;

    /* Create listening socket */
    node->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (node->listen_fd < 0) {
        return false;
    }

    /* Allow address reuse */
    int opt = 1;
    setsockopt(node->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(node->config.port);

    if (strcmp(node->config.host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, node->config.host, &addr.sin_addr);
    }

    if (bind(node->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(node->listen_fd);
        node->listen_fd = -1;
        return false;
    }

    /* Listen */
    if (listen(node->listen_fd, 10) < 0) {
        close(node->listen_fd);
        node->listen_fd = -1;
        return false;
    }

    /* Start accept thread */
    node->accept_running = true;
    node->running = true;

    if (pthread_create(&node->accept_thread, NULL, accept_thread_fn, node) != 0) {
        close(node->listen_fd);
        node->listen_fd = -1;
        node->accept_running = false;
        node->running = false;
        return false;
    }

    return true;
}

void node_stop(DistributedNode *node) {
    if (!node || !node->running) return;

    node->running = false;
    node->accept_running = false;

    /* Close listening socket to unblock accept */
    if (node->listen_fd >= 0) {
        shutdown(node->listen_fd, SHUT_RDWR);
        close(node->listen_fd);
        node->listen_fd = -1;
    }

    /* Wait for accept thread */
    pthread_join(node->accept_thread, NULL);

    /* Stop all peer receiver threads */
    pthread_mutex_lock(&node->lock);
    NodeConnection *peer = node->peers;
    while (peer) {
        if (peer->recv_running) {
            peer->recv_running = false;
            if (peer->socket_fd >= 0) {
                shutdown(peer->socket_fd, SHUT_RDWR);
            }
        }
        peer = peer->next;
    }
    pthread_mutex_unlock(&node->lock);
}

/*============================================================================
 * Peer Connections
 *============================================================================*/

bool node_connect(DistributedNode *node, const char *peer_name,
                  const char *host, uint16_t port) {
    if (!node || !peer_name || !host) return false;

    pthread_mutex_lock(&node->lock);

    /* Check if already connected */
    NodeConnection *existing = node->peers;
    while (existing) {
        if (strcmp(existing->peer.name, peer_name) == 0) {
            pthread_mutex_unlock(&node->lock);
            return existing->state == NODE_CONNECTED;
        }
        existing = existing->next;
    }

    /* Create connection */
    NodeConnection *conn = calloc(1, sizeof(NodeConnection));
    if (!conn) {
        pthread_mutex_unlock(&node->lock);
        return false;
    }

    strncpy(conn->peer.name, peer_name, NODE_NAME_MAX - 1);
    strncpy(conn->peer.host, host, NODE_HOST_MAX - 1);
    conn->peer.port = port;
    conn->peer.cookie = node->config.cookie;
    conn->state = NODE_CONNECTING;
    conn->socket_fd = -1;

    /* Add to list */
    conn->next = node->peers;
    node->peers = conn;
    node->peer_count++;

    pthread_mutex_unlock(&node->lock);

    /* Create socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        conn->state = NODE_FAILED;
        return false;
    }

    /* Set non-blocking temporarily for connect timeout */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    /* Resolve host */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    struct hostent *he = gethostbyname(host);
    if (!he) {
        close(sock);
        conn->state = NODE_FAILED;
        return false;
    }
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    /* Connect */
    int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        close(sock);
        conn->state = NODE_FAILED;
        return false;
    }

    /* Wait for connection with timeout */
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);

    struct timeval tv = {
        .tv_sec = node->config.timeout_ms / 1000,
        .tv_usec = (node->config.timeout_ms % 1000) * 1000,
    };

    ret = select(sock + 1, NULL, &writefds, NULL, &tv);
    if (ret <= 0) {
        close(sock);
        conn->state = NODE_FAILED;
        return false;
    }

    /* Check for connection error */
    int err;
    socklen_t len = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        close(sock);
        conn->state = NODE_FAILED;
        return false;
    }

    /* Restore blocking mode */
    fcntl(sock, F_SETFL, flags);

    /* Set TCP_NODELAY */
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    conn->socket_fd = sock;
    conn->state = NODE_CONNECTED;
    conn->connected_at = current_time_ms();
    conn->last_heartbeat = conn->connected_at;

    /* TODO: Send handshake, verify cookie, start receiver thread */

    /* Notify callback */
    if (node->on_node_up) {
        node->on_node_up(node->callback_ctx, &conn->peer);
    }

    return true;
}

void node_disconnect(DistributedNode *node, const char *peer_name) {
    if (!node || !peer_name) return;

    pthread_mutex_lock(&node->lock);

    NodeConnection **pp = &node->peers;
    while (*pp) {
        NodeConnection *conn = *pp;
        if (strcmp(conn->peer.name, peer_name) == 0) {
            /* Stop receiver thread */
            if (conn->recv_running) {
                conn->recv_running = false;
            }

            /* Close socket */
            if (conn->socket_fd >= 0) {
                close(conn->socket_fd);
            }

            /* Remove from list */
            *pp = conn->next;
            node->peer_count--;

            /* Notify callback */
            if (node->on_node_down) {
                node->on_node_down(node->callback_ctx, &conn->peer);
            }

            free(conn);
            pthread_mutex_unlock(&node->lock);
            return;
        }
        pp = &conn->next;
    }

    pthread_mutex_unlock(&node->lock);
}

NodeConnection *node_get_peer(DistributedNode *node, const char *peer_name) {
    if (!node || !peer_name) return NULL;

    pthread_mutex_lock(&node->lock);

    NodeConnection *conn = node->peers;
    while (conn) {
        if (strcmp(conn->peer.name, peer_name) == 0) {
            pthread_mutex_unlock(&node->lock);
            return conn;
        }
        conn = conn->next;
    }

    pthread_mutex_unlock(&node->lock);
    return NULL;
}

const NodeId *node_list_peers(DistributedNode *node, size_t *count) {
    /* Note: This is a simplified implementation that returns NULL.
     * A full implementation would build an array of NodeIds. */
    if (count) *count = 0;
    (void)node;
    return NULL;
}

bool node_is_connected(DistributedNode *node, const char *peer_name) {
    NodeConnection *conn = node_get_peer(node, peer_name);
    return conn && conn->state == NODE_CONNECTED;
}

/*============================================================================
 * Messaging
 *============================================================================*/

bool node_send(DistributedNode *node, const char *peer_name,
               Pid target_pid, Pid sender_pid, const void *data, size_t len) {
    NodeConnection *conn = node_get_peer(node, peer_name);
    if (!conn || conn->state != NODE_CONNECTED || conn->socket_fd < 0) {
        return false;
    }

    /* Build message header */
    uint8_t header[32];
    size_t hlen = 0;

    /* Message type */
    header[hlen++] = DIST_MSG_SEND;

    /* Target PID (8 bytes) */
    for (int i = 7; i >= 0; i--) {
        header[hlen++] = (target_pid >> (i * 8)) & 0xFF;
    }

    /* Sender PID (8 bytes) */
    for (int i = 7; i >= 0; i--) {
        header[hlen++] = (sender_pid >> (i * 8)) & 0xFF;
    }

    /* Payload length (4 bytes) */
    uint32_t len32 = (uint32_t)len;
    header[hlen++] = (len32 >> 24) & 0xFF;
    header[hlen++] = (len32 >> 16) & 0xFF;
    header[hlen++] = (len32 >> 8) & 0xFF;
    header[hlen++] = len32 & 0xFF;

    /* Send header */
    ssize_t sent = send(conn->socket_fd, header, hlen, MSG_NOSIGNAL);
    if (sent != (ssize_t)hlen) {
        return false;
    }

    /* Send payload */
    if (len > 0 && data) {
        sent = send(conn->socket_fd, data, len, MSG_NOSIGNAL);
        if (sent != (ssize_t)len) {
            return false;
        }
    }

    conn->messages_sent++;
    conn->bytes_sent += hlen + len;

    return true;
}

bool node_send_value(DistributedNode *node, const char *peer_name,
                     Pid target_pid, Pid sender_pid, struct Value *value) {
    if (!value) return false;

    /* Serialize value */
    SerialBuffer buf;
    serial_buffer_init(&buf);

    SerializeResult res = serialize_value(value, &buf);
    if (res != SERIALIZE_OK) {
        serial_buffer_free(&buf);
        return false;
    }

    bool ok = node_send(node, peer_name, target_pid, sender_pid, buf.data, buf.size);
    serial_buffer_free(&buf);

    return ok;
}

/*============================================================================
 * Monitoring
 *============================================================================*/

bool node_monitor(DistributedNode *node, Pid watcher_pid, const char *peer_name) {
    if (!node) return false;

    pthread_mutex_lock(&node->lock);

    NodeMonitor *mon = malloc(sizeof(NodeMonitor));
    if (!mon) {
        pthread_mutex_unlock(&node->lock);
        return false;
    }

    mon->watcher_pid = watcher_pid;
    if (peer_name) {
        strncpy(mon->node_name, peer_name, NODE_NAME_MAX - 1);
    } else {
        mon->node_name[0] = '\0';
    }
    mon->next = node->monitors;
    node->monitors = mon;
    node->monitor_count++;

    pthread_mutex_unlock(&node->lock);
    return true;
}

void node_demonitor(DistributedNode *node, Pid watcher_pid, const char *peer_name) {
    if (!node) return;

    pthread_mutex_lock(&node->lock);

    NodeMonitor **pp = &node->monitors;
    while (*pp) {
        NodeMonitor *mon = *pp;
        bool match = (mon->watcher_pid == watcher_pid);
        if (peer_name) {
            match = match && (strcmp(mon->node_name, peer_name) == 0);
        }

        if (match) {
            *pp = mon->next;
            node->monitor_count--;
            free(mon);
        } else {
            pp = &mon->next;
        }
    }

    pthread_mutex_unlock(&node->lock);
}

/*============================================================================
 * Queries
 *============================================================================*/

const NodeId *node_self(DistributedNode *node) {
    return node ? &node->local : NULL;
}

const char *node_name(DistributedNode *node) {
    return node ? node->local.name : NULL;
}

bool node_parse_ref(const char *ref, char *name, char *host, uint16_t *port) {
    if (!ref || !name || !host || !port) return false;

    /* Format: name@host:port */
    const char *at = strchr(ref, '@');
    const char *colon = at ? strchr(at, ':') : NULL;

    if (!at || !colon) return false;

    /* Extract name */
    size_t name_len = (size_t)(at - ref);
    if (name_len >= NODE_NAME_MAX) return false;
    memcpy(name, ref, name_len);
    name[name_len] = '\0';

    /* Extract host */
    size_t host_len = (size_t)(colon - at - 1);
    if (host_len >= NODE_HOST_MAX) return false;
    memcpy(host, at + 1, host_len);
    host[host_len] = '\0';

    /* Extract port */
    *port = (uint16_t)atoi(colon + 1);

    return true;
}

void node_format_ref(const NodeId *node_id, char *buf, size_t buf_size) {
    if (!node_id || !buf || buf_size == 0) return;
    snprintf(buf, buf_size, "%s@%s:%u", node_id->name, node_id->host, node_id->port);
}
