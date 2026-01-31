/*
 * Agim - Distributed Node Management
 *
 * Node identity, discovery, and cluster membership for distributed agents.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_DIST_NODE_H
#define AGIM_DIST_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "runtime/mailbox.h"

/*============================================================================
 * Node Identity
 *============================================================================*/

#define NODE_NAME_MAX 64
#define NODE_HOST_MAX 256

/**
 * Node identifier for distributed communication.
 */
typedef struct NodeId {
    char name[NODE_NAME_MAX];   /* Node name (e.g., "agent1") */
    char host[NODE_HOST_MAX];   /* Hostname or IP address */
    uint16_t port;              /* Port number */
    uint64_t cookie;            /* Authentication cookie (shared secret) */
    uint64_t node_id;           /* Unique numeric ID for fast comparison */
} NodeId;

/**
 * Extended block identifier for distributed blocks.
 */
typedef struct GlobalBlockId {
    Pid local_pid;              /* Local PID (within node) */
    NodeId *node;               /* Node (NULL = local node) */
} GlobalBlockId;

/*============================================================================
 * Node State
 *============================================================================*/

typedef enum NodeState {
    NODE_DISCONNECTED,          /* Not connected */
    NODE_CONNECTING,            /* Connection in progress */
    NODE_CONNECTED,             /* Active connection */
    NODE_FAILED,                /* Connection failed */
} NodeState;

/**
 * Connection to a peer node.
 */
typedef struct NodeConnection {
    NodeId peer;                /* Peer node identity */
    NodeState state;            /* Connection state */
    int socket_fd;              /* TCP socket file descriptor */
    uint64_t connected_at;      /* Connection timestamp */
    uint64_t last_heartbeat;    /* Last heartbeat received */

    /* Statistics */
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;

    /* Threading */
    pthread_t recv_thread;      /* Receiver thread */
    bool recv_running;          /* Receiver thread running */

    struct NodeConnection *next; /* Linked list */
} NodeConnection;

/*============================================================================
 * Distributed Node
 *============================================================================*/

/**
 * Configuration for a distributed node.
 */
typedef struct NodeConfig {
    char name[NODE_NAME_MAX];   /* This node's name */
    char host[NODE_HOST_MAX];   /* Listen address */
    uint16_t port;              /* Listen port */
    uint64_t cookie;            /* Authentication cookie */
    uint32_t heartbeat_ms;      /* Heartbeat interval (default: 5000) */
    uint32_t timeout_ms;        /* Connection timeout (default: 10000) */
} NodeConfig;

/**
 * Local distributed node.
 */
typedef struct DistributedNode {
    /* Local identity */
    NodeId local;

    /* Configuration */
    NodeConfig config;

    /* Peer connections */
    NodeConnection *peers;      /* Linked list of peers */
    size_t peer_count;

    /* Listening socket */
    int listen_fd;
    pthread_t accept_thread;
    bool accept_running;

    /* Node monitors (processes watching for node down) */
    struct NodeMonitor *monitors;
    size_t monitor_count;

    /* Synchronization */
    pthread_mutex_t lock;

    /* State */
    bool running;

    /* Callbacks */
    void *callback_ctx;
    void (*on_node_up)(void *ctx, const NodeId *node);
    void (*on_node_down)(void *ctx, const NodeId *node);
    void (*on_message)(void *ctx, const NodeId *from, Pid target, void *msg, size_t len);
} DistributedNode;

/**
 * Node monitor entry.
 */
typedef struct NodeMonitor {
    Pid watcher_pid;            /* PID watching for node events */
    char node_name[NODE_NAME_MAX]; /* Node being watched (empty = all nodes) */
    struct NodeMonitor *next;
} NodeMonitor;

/*============================================================================
 * Node API - Lifecycle
 *============================================================================*/

/**
 * Get default node configuration.
 */
NodeConfig node_config_default(void);

/**
 * Create a distributed node.
 */
DistributedNode *node_new(const NodeConfig *config);

/**
 * Free a distributed node and close all connections.
 */
void node_free(DistributedNode *node);

/**
 * Start the node (begin accepting connections).
 */
bool node_start(DistributedNode *node);

/**
 * Stop the node.
 */
void node_stop(DistributedNode *node);

/*============================================================================
 * Node API - Connections
 *============================================================================*/

/**
 * Connect to a peer node.
 * Returns true if connection initiated successfully.
 */
bool node_connect(DistributedNode *node, const char *peer_name,
                  const char *host, uint16_t port);

/**
 * Disconnect from a peer node.
 */
void node_disconnect(DistributedNode *node, const char *peer_name);

/**
 * Get connection to a peer.
 */
NodeConnection *node_get_peer(DistributedNode *node, const char *peer_name);

/**
 * List all connected peers.
 */
const NodeId *node_list_peers(DistributedNode *node, size_t *count);

/**
 * Check if a peer is connected.
 */
bool node_is_connected(DistributedNode *node, const char *peer_name);

/*============================================================================
 * Node API - Messaging
 *============================================================================*/

/**
 * Send a message to a remote block.
 * Returns true if message was queued for sending.
 */
bool node_send(DistributedNode *node, const char *peer_name,
               Pid target_pid, Pid sender_pid, const void *data, size_t len);

/**
 * Send a message using serialized Value.
 */
bool node_send_value(DistributedNode *node, const char *peer_name,
                     Pid target_pid, Pid sender_pid, struct Value *value);

/*============================================================================
 * Node API - Monitoring
 *============================================================================*/

/**
 * Monitor a node (receive nodedown message on disconnect).
 */
bool node_monitor(DistributedNode *node, Pid watcher_pid, const char *peer_name);

/**
 * Stop monitoring a node.
 */
void node_demonitor(DistributedNode *node, Pid watcher_pid, const char *peer_name);

/*============================================================================
 * Node API - Queries
 *============================================================================*/

/**
 * Get this node's identity.
 */
const NodeId *node_self(DistributedNode *node);

/**
 * Get this node's name.
 */
const char *node_name(DistributedNode *node);

/**
 * Parse a node reference string ("name@host:port").
 */
bool node_parse_ref(const char *ref, char *name, char *host, uint16_t *port);

/**
 * Format a node reference string.
 */
void node_format_ref(const NodeId *node, char *buf, size_t buf_size);

/*============================================================================
 * Distribution Protocol
 *============================================================================*/

/* Message types */
#define DIST_MSG_HANDSHAKE  0x01
#define DIST_MSG_HEARTBEAT  0x02
#define DIST_MSG_SEND       0x03
#define DIST_MSG_LINK       0x04
#define DIST_MSG_UNLINK     0x05
#define DIST_MSG_EXIT       0x06
#define DIST_MSG_MONITOR    0x07
#define DIST_MSG_DEMONITOR  0x08
#define DIST_MSG_DOWN       0x09

/* Protocol version */
#define DIST_PROTOCOL_VERSION 1

#endif /* AGIM_DIST_NODE_H */
