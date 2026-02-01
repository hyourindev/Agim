/*
 * Agim - Distributed Node Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "dist/node.h"
#include "runtime/serialize.h"
#include "runtime/timer.h"
#include "debug/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/*
 * Configuration
 */

/* Maximum message size to prevent memory exhaustion attacks (16 MB) */
#define DIST_MAX_MESSAGE_SIZE (16 * 1024 * 1024)

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

/* Node Lifecycle */

DistributedNode *node_new(const NodeConfig *config) {
    DistributedNode *node = calloc(1, sizeof(DistributedNode));
    if (!node) {
        LOG_ERROR("node: failed to allocate DistributedNode");
        return NULL;
    }

    NodeConfig cfg = config ? *config : node_config_default();

    /* Set up local identity */
    strncpy(node->local.name, cfg.name, NODE_NAME_MAX - 1);
    strncpy(node->local.host, cfg.host, NODE_HOST_MAX - 1);
    node->local.port = cfg.port;
    node->local.cookie = cfg.cookie;
    node->local.node_id = (uint64_t)timer_current_time_ms();  /* Simple unique ID */

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

/* Accept Thread */

/* Read exactly n bytes from socket */
static bool socket_read_exact(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t remaining = n;
    while (remaining > 0) {
        ssize_t r = recv(fd, p, remaining, 0);
        if (r <= 0) return false;
        p += r;
        remaining -= (size_t)r;
    }
    return true;
}

/* Write exactly n bytes to socket */
static bool socket_write_exact(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t remaining = n;
    while (remaining > 0) {
        ssize_t w = send(fd, p, remaining, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w;
        remaining -= (size_t)w;
    }
    return true;
}

/* Receiver thread for a peer connection */
static void *receiver_thread_fn(void *arg) {
    NodeConnection *conn = (NodeConnection *)arg;

    while (conn->recv_running) {
        /* Read message header: [type:1][length:4] */
        uint8_t header[5];
        if (!socket_read_exact(conn->socket_fd, header, 5)) {
            break;  /* Connection closed or error */
        }

        uint8_t msg_type = header[0];
        uint32_t msg_len = ((uint32_t)header[1] << 24) | ((uint32_t)header[2] << 16) |
                          ((uint32_t)header[3] << 8) | header[4];

        /* Reject messages that are too large (security: prevent memory exhaustion) */
        if (msg_len > DIST_MAX_MESSAGE_SIZE) {
            break;  /* Disconnect - message too large */
        }

        /* Handle message by type */
        switch (msg_type) {
        case DIST_MSG_HEARTBEAT:
            conn->last_heartbeat = timer_current_time_ms();
            break;

        case DIST_MSG_SEND: {
            /* Format: [target_pid:8][sender_pid:8][payload:...] */
            if (msg_len < 16) break;

            uint8_t pid_buf[16];
            if (!socket_read_exact(conn->socket_fd, pid_buf, 16)) break;

            /* Parse PIDs (big-endian) */
            Pid target_pid = 0, sender_pid = 0;
            for (int i = 0; i < 8; i++) {
                target_pid = (target_pid << 8) | pid_buf[i];
                sender_pid = (sender_pid << 8) | pid_buf[8 + i];
            }

            /* Read payload */
            size_t payload_len = msg_len - 16;
            void *payload = NULL;
            if (payload_len > 0) {
                payload = malloc(payload_len);
                if (!payload) {
                    /* Malloc failed - skip the payload bytes to stay in sync */
                    uint8_t skip_buf[256];
                    size_t remaining = payload_len;
                    while (remaining > 0) {
                        size_t chunk = remaining > sizeof(skip_buf) ? sizeof(skip_buf) : remaining;
                        if (!socket_read_exact(conn->socket_fd, skip_buf, chunk)) break;
                        remaining -= chunk;
                    }
                    break;
                }
                if (!socket_read_exact(conn->socket_fd, payload, payload_len)) {
                    free(payload);
                    break;
                }
            }

            /* Deliver message via callback */
            DistributedNode *node = conn->node;
            if (node && node->on_message) {
                node->on_message(node->callback_ctx, &conn->peer, target_pid,
                                 payload, payload_len);
            }
            free(payload);
            conn->messages_received++;
            break;
        }

        default:
            /* Skip unknown message types */
            if (msg_len > 0) {
                uint8_t *skip = malloc(msg_len);
                if (skip) {
                    socket_read_exact(conn->socket_fd, skip, msg_len);
                    free(skip);
                }
            }
            break;
        }

        conn->bytes_received += 5 + msg_len;
    }

    return NULL;
}

/* Send handshake to a peer */
static bool send_handshake(int fd, const NodeId *local) {
    /* Handshake format: [type:1][version:1][cookie:8][name_len:1][name:var] */
    size_t name_len = strlen(local->name);
    if (name_len > 255) name_len = 255;

    size_t total = 1 + 1 + 8 + 1 + name_len;
    uint8_t *buf = malloc(total);
    if (!buf) return false;

    size_t pos = 0;
    buf[pos++] = DIST_MSG_HANDSHAKE;
    buf[pos++] = DIST_PROTOCOL_VERSION;

    /* Cookie (8 bytes big-endian) */
    for (int i = 7; i >= 0; i--) {
        buf[pos++] = (local->cookie >> (i * 8)) & 0xFF;
    }

    buf[pos++] = (uint8_t)name_len;
    memcpy(buf + pos, local->name, name_len);

    bool ok = socket_write_exact(fd, buf, total);
    free(buf);
    return ok;
}

/* Read handshake from a peer */
static bool read_handshake(int fd, uint64_t expected_cookie, NodeId *peer_out) {
    /* Read fixed part: [type:1][version:1][cookie:8][name_len:1] */
    uint8_t header[11];
    if (!socket_read_exact(fd, header, 11)) return false;

    if (header[0] != DIST_MSG_HANDSHAKE) return false;
    if (header[1] != DIST_PROTOCOL_VERSION) return false;

    /* Verify cookie */
    uint64_t cookie = 0;
    for (int i = 0; i < 8; i++) {
        cookie = (cookie << 8) | header[2 + i];
    }
    if (cookie != expected_cookie) return false;

    /* Read name */
    uint8_t name_len = header[10];
    if (name_len > 0) {
        if (name_len >= NODE_NAME_MAX) name_len = NODE_NAME_MAX - 1;
        if (!socket_read_exact(fd, peer_out->name, name_len)) return false;
        peer_out->name[name_len] = '\0';
    } else {
        peer_out->name[0] = '\0';
    }

    peer_out->cookie = cookie;
    return true;
}

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

        /* Read handshake from client */
        NodeId peer_id = {0};
        if (!read_handshake(client_fd, node->config.cookie, &peer_id)) {
            close(client_fd);
            continue;
        }

        /* Send our handshake response */
        if (!send_handshake(client_fd, &node->local)) {
            close(client_fd);
            continue;
        }

        /* Get client address info */
        inet_ntop(AF_INET, &client_addr.sin_addr, peer_id.host, NODE_HOST_MAX);
        peer_id.port = ntohs(client_addr.sin_port);
        peer_id.node_id = timer_current_time_ms();

        /* Create connection and add to peers */
        NodeConnection *conn = calloc(1, sizeof(NodeConnection));
        if (!conn) {
            close(client_fd);
            continue;
        }

        conn->peer = peer_id;
        conn->state = NODE_CONNECTED;
        conn->socket_fd = client_fd;
        conn->connected_at = timer_current_time_ms();
        conn->last_heartbeat = conn->connected_at;
        conn->recv_running = true;
        conn->node = node;

        /* Add to peers list */
        pthread_mutex_lock(&node->lock);
        conn->next = node->peers;
        node->peers = conn;
        node->peer_count++;
        pthread_mutex_unlock(&node->lock);

        /* Start receiver thread */
        if (pthread_create(&conn->recv_thread, NULL, receiver_thread_fn, conn) != 0) {
            /* Thread creation failed - clean up connection */
            pthread_mutex_lock(&node->lock);
            node->peers = conn->next;
            node->peer_count--;
            pthread_mutex_unlock(&node->lock);
            close(conn->socket_fd);
            free(conn);
            continue;
        }

        /* Notify callback */
        if (node->on_node_up) {
            node->on_node_up(node->callback_ctx, &conn->peer);
        }
    }

    return NULL;
}

bool node_start(DistributedNode *node) {
    if (!node || node->running) return false;

    /* Require non-zero cookie for security */
    if (node->config.cookie == 0) {
        LOG_ERROR("node: cookie must be configured (non-zero) for security");
        return false;
    }

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

/* Peer Connections */

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
    conn->node = node;

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
    /* Validate h_length before memcpy to prevent buffer overflow */
    if ((size_t)he->h_length > sizeof(addr.sin_addr)) {
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
    conn->connected_at = timer_current_time_ms();
    conn->last_heartbeat = conn->connected_at;

    /* Send handshake */
    if (!send_handshake(sock, &node->local)) {
        close(sock);
        conn->state = NODE_FAILED;
        return false;
    }

    /* Read handshake response */
    NodeId peer_response = {0};
    if (!read_handshake(sock, node->config.cookie, &peer_response)) {
        close(sock);
        conn->state = NODE_FAILED;
        return false;
    }

    conn->state = NODE_CONNECTED;
    conn->recv_running = true;

    /* Start receiver thread */
    pthread_create(&conn->recv_thread, NULL, receiver_thread_fn, conn);

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
            /* Remove from list first */
            *pp = conn->next;
            node->peer_count--;
            pthread_mutex_unlock(&node->lock);

            /* Stop receiver thread - shutdown socket to unblock recv */
            bool was_running = conn->recv_running;
            conn->recv_running = false;
            if (conn->socket_fd >= 0) {
                shutdown(conn->socket_fd, SHUT_RDWR);
                close(conn->socket_fd);
                conn->socket_fd = -1;
            }

            /* Wait for receiver thread to finish */
            if (was_running) {
                pthread_join(conn->recv_thread, NULL);
            }

            /* Notify callback */
            if (node->on_node_down) {
                node->on_node_down(node->callback_ctx, &conn->peer);
            }

            free(conn);
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
    if (!node || !count) {
        if (count) *count = 0;
        return NULL;
    }

    pthread_mutex_lock(&node->lock);

    /* Count connected peers */
    size_t n = 0;
    NodeConnection *conn = node->peers;
    while (conn) {
        if (conn->state == NODE_CONNECTED) {
            n++;
        }
        conn = conn->next;
    }

    if (n == 0) {
        pthread_mutex_unlock(&node->lock);
        *count = 0;
        return NULL;
    }

    /* Allocate array for peer IDs */
    NodeId *peers = malloc(sizeof(NodeId) * n);
    if (!peers) {
        pthread_mutex_unlock(&node->lock);
        *count = 0;
        return NULL;
    }

    /* Fill array with connected peer IDs */
    size_t i = 0;
    conn = node->peers;
    while (conn && i < n) {
        if (conn->state == NODE_CONNECTED) {
            peers[i++] = conn->peer;
        }
        conn = conn->next;
    }

    pthread_mutex_unlock(&node->lock);

    *count = i;
    return peers;
}

bool node_is_connected(DistributedNode *node, const char *peer_name) {
    NodeConnection *conn = node_get_peer(node, peer_name);
    return conn && conn->state == NODE_CONNECTED;
}

/* Messaging */

bool node_send(DistributedNode *node, const char *peer_name,
               Pid target_pid, Pid sender_pid, const void *data, size_t len) {
    NodeConnection *conn = node_get_peer(node, peer_name);
    if (!conn || conn->state != NODE_CONNECTED || conn->socket_fd < 0) {
        return false;
    }

    /* Message format: [type:1][length:4][target_pid:8][sender_pid:8][payload:...]
     * where length = 16 + payload_len (PIDs + payload) */
    uint8_t header[5];
    size_t hlen = 0;

    /* Message type */
    header[hlen++] = DIST_MSG_SEND;

    /* Message length (4 bytes big-endian): PIDs (16 bytes) + payload */
    uint32_t msg_len = 16 + (uint32_t)len;
    header[hlen++] = (msg_len >> 24) & 0xFF;
    header[hlen++] = (msg_len >> 16) & 0xFF;
    header[hlen++] = (msg_len >> 8) & 0xFF;
    header[hlen++] = msg_len & 0xFF;

    /* Send message header */
    if (!socket_write_exact(conn->socket_fd, header, hlen)) {
        return false;
    }

    /* Send PIDs (16 bytes total) */
    uint8_t pids[16];
    for (int i = 7; i >= 0; i--) {
        pids[7 - i] = (target_pid >> (i * 8)) & 0xFF;
        pids[15 - i] = (sender_pid >> (i * 8)) & 0xFF;
    }
    if (!socket_write_exact(conn->socket_fd, pids, 16)) {
        return false;
    }

    /* Send payload */
    if (len > 0 && data) {
        if (!socket_write_exact(conn->socket_fd, data, len)) {
            return false;
        }
    }

    conn->messages_sent++;
    conn->bytes_sent += hlen + 16 + len;

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

/* Monitoring */

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

/* Queries */

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
