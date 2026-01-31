/*
 * Agim - End-to-End Distributed Node Tests
 *
 * Tests the distributed node infrastructure including node identity,
 * peer connections, message passing, and cluster management. Validates
 * Erlang-style distributed communication semantics.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "../test_common.h"
#include "dist/node.h"
#include "runtime/timer.h"
#include "vm/value.h"

#include <unistd.h>
#include <string.h>

/* Callback tracking */
static int node_up_count = 0;
static int node_down_count = 0;
static int message_count = 0;
static char last_node_name[NODE_NAME_MAX] = {0};
static Pid last_target_pid = 0;

static void reset_callbacks(void)
{
	node_up_count = 0;
	node_down_count = 0;
	message_count = 0;
	last_node_name[0] = '\0';
	last_target_pid = 0;
}

static void on_node_up_callback(void *ctx, const NodeId *node)
{
	(void)ctx;
	node_up_count++;
	if (node && node->name[0]) {
		strncpy(last_node_name, node->name, NODE_NAME_MAX - 1);
	}
}

static void on_node_down_callback(void *ctx, const NodeId *node)
{
	(void)ctx;
	node_down_count++;
	if (node && node->name[0]) {
		strncpy(last_node_name, node->name, NODE_NAME_MAX - 1);
	}
}

static void on_message_callback(void *ctx, const NodeId *from, Pid target,
				void *msg, size_t len)
{
	(void)ctx;
	(void)from;
	(void)msg;
	(void)len;
	message_count++;
	last_target_pid = target;
}

/* Test 1: Default node configuration */
void test_node_config_default(void)
{
	NodeConfig cfg = node_config_default();

	ASSERT_STR_EQ("node", cfg.name);
	ASSERT_STR_EQ("0.0.0.0", cfg.host);
	ASSERT_EQ(9000, cfg.port);
	ASSERT_EQ(0, cfg.cookie);
	ASSERT_EQ(5000, cfg.heartbeat_ms);
	ASSERT_EQ(10000, cfg.timeout_ms);
}

/* Test 2: Node creation */
void test_node_creation(void)
{
	NodeConfig cfg = node_config_default();
	strncpy(cfg.name, "test_node", NODE_NAME_MAX);
	cfg.port = 9100;
	cfg.cookie = 12345;

	DistributedNode *node = node_new(&cfg);
	ASSERT(node != NULL);

	ASSERT_STR_EQ("test_node", node->local.name);
	ASSERT_EQ(9100, node->local.port);
	ASSERT_EQ(12345, node->local.cookie);
	ASSERT(!node->running);
	ASSERT_EQ(0, node->peer_count);

	node_free(node);
}

/* Test 3: Node with default config */
void test_node_default_creation(void)
{
	DistributedNode *node = node_new(NULL);
	ASSERT(node != NULL);

	ASSERT_STR_EQ("node", node->local.name);
	ASSERT_EQ(9000, node->local.port);

	node_free(node);
}

/* Test 4: Node identity */
void test_node_identity(void)
{
	NodeConfig cfg = node_config_default();
	strncpy(cfg.name, "identity_test", NODE_NAME_MAX);
	cfg.port = 9101;

	DistributedNode *node = node_new(&cfg);

	const NodeId *self = node_self(node);
	ASSERT(self != NULL);
	ASSERT_STR_EQ("identity_test", self->name);
	ASSERT_EQ(9101, self->port);

	const char *name = node_name(node);
	ASSERT_STR_EQ("identity_test", name);

	node_free(node);
}

/* Test 5: Node start/stop */
void test_node_start_stop(void)
{
	NodeConfig cfg = node_config_default();
	strncpy(cfg.name, "start_stop", NODE_NAME_MAX);
	cfg.port = 9102;

	DistributedNode *node = node_new(&cfg);
	ASSERT(!node->running);

	ASSERT(node_start(node));
	ASSERT(node->running);
	ASSERT(node->listen_fd >= 0);

	node_stop(node);
	ASSERT(!node->running);

	node_free(node);
}

/* Test 6: Node reference parsing */
void test_node_parse_ref(void)
{
	char name[NODE_NAME_MAX];
	char host[NODE_HOST_MAX];
	uint16_t port;

	/* Valid reference */
	ASSERT(node_parse_ref("agent1@localhost:9000", name, host, &port));
	ASSERT_STR_EQ("agent1", name);
	ASSERT_STR_EQ("localhost", host);
	ASSERT_EQ(9000, port);

	/* Another valid reference */
	ASSERT(node_parse_ref("node2@192.168.1.100:8080", name, host, &port));
	ASSERT_STR_EQ("node2", name);
	ASSERT_STR_EQ("192.168.1.100", host);
	ASSERT_EQ(8080, port);

	/* Invalid references */
	ASSERT(!node_parse_ref("invalid", name, host, &port));
	ASSERT(!node_parse_ref("no_at_sign:9000", name, host, &port));
	ASSERT(!node_parse_ref("no@colon", name, host, &port));
}

/* Test 7: Node reference formatting */
void test_node_format_ref(void)
{
	NodeId node_id = {0};
	strncpy(node_id.name, "test_node", NODE_NAME_MAX);
	strncpy(node_id.host, "127.0.0.1", NODE_HOST_MAX);
	node_id.port = 9000;

	char buf[256];
	node_format_ref(&node_id, buf, sizeof(buf));

	ASSERT_STR_EQ("test_node@127.0.0.1:9000", buf);
}

/* Test 8: Node with callbacks */
void test_node_callbacks(void)
{
	reset_callbacks();

	NodeConfig cfg = node_config_default();
	strncpy(cfg.name, "callback_node", NODE_NAME_MAX);
	cfg.port = 9103;

	DistributedNode *node = node_new(&cfg);
	node->on_node_up = on_node_up_callback;
	node->on_node_down = on_node_down_callback;
	node->on_message = on_message_callback;

	ASSERT(node->on_node_up == on_node_up_callback);
	ASSERT(node->on_node_down == on_node_down_callback);
	ASSERT(node->on_message == on_message_callback);

	node_free(node);
}

/* Test 9: Node monitoring */
void test_node_monitoring(void)
{
	NodeConfig cfg = node_config_default();
	strncpy(cfg.name, "monitor_node", NODE_NAME_MAX);
	cfg.port = 9104;

	DistributedNode *node = node_new(&cfg);

	/* Add monitor */
	ASSERT(node_monitor(node, 100, "peer1"));
	ASSERT_EQ(1, node->monitor_count);

	/* Add another monitor */
	ASSERT(node_monitor(node, 200, "peer2"));
	ASSERT_EQ(2, node->monitor_count);

	/* Remove monitor */
	node_demonitor(node, 100, "peer1");
	ASSERT_EQ(1, node->monitor_count);

	node_free(node);
}

/* Test 10: Empty peer list */
void test_empty_peer_list(void)
{
	NodeConfig cfg = node_config_default();
	cfg.port = 9105;

	DistributedNode *node = node_new(&cfg);

	size_t count = 0;
	const NodeId *peers = node_list_peers(node, &count);
	ASSERT_EQ(0, count);
	ASSERT(peers == NULL);

	ASSERT(!node_is_connected(node, "nonexistent"));

	node_free(node);
}

/* Test 11: Node unique ID */
void test_node_unique_id(void)
{
	NodeConfig cfg1 = node_config_default();
	strncpy(cfg1.name, "node1", NODE_NAME_MAX);
	cfg1.port = 9106;

	NodeConfig cfg2 = node_config_default();
	strncpy(cfg2.name, "node2", NODE_NAME_MAX);
	cfg2.port = 9107;

	DistributedNode *node1 = node_new(&cfg1);
	usleep(1000);  /* 1ms delay for different timestamp */
	DistributedNode *node2 = node_new(&cfg2);

	/* Node IDs should be unique */
	ASSERT(node1->local.node_id != node2->local.node_id);

	node_free(node1);
	node_free(node2);
}

/* Test 12: Peer-to-peer connection */
void test_peer_connection(void)
{
	reset_callbacks();

	/* Create server node */
	NodeConfig server_cfg = node_config_default();
	strncpy(server_cfg.name, "server", NODE_NAME_MAX);
	server_cfg.port = 9108;
	server_cfg.cookie = 0xDEADBEEF;

	DistributedNode *server = node_new(&server_cfg);
	server->on_node_up = on_node_up_callback;
	ASSERT(node_start(server));

	/* Create client node */
	NodeConfig client_cfg = node_config_default();
	strncpy(client_cfg.name, "client", NODE_NAME_MAX);
	client_cfg.port = 9109;
	client_cfg.cookie = 0xDEADBEEF;

	DistributedNode *client = node_new(&client_cfg);
	client->on_node_up = on_node_up_callback;
	ASSERT(node_start(client));

	/* Connect client to server */
	ASSERT(node_connect(client, "server", "127.0.0.1", 9108));

	/* Wait for connection */
	usleep(100000);  /* 100ms */

	/* Verify connection */
	ASSERT(node_is_connected(client, "server"));

	/* Check peer info */
	NodeConnection *peer = node_get_peer(client, "server");
	ASSERT(peer != NULL);
	ASSERT_EQ(NODE_CONNECTED, peer->state);

	/* Cleanup */
	node_disconnect(client, "server");
	usleep(50000);

	node_stop(server);
	node_stop(client);
	node_free(server);
	node_free(client);
}

/* Test 13: Message sending */
void test_message_sending(void)
{
	reset_callbacks();

	/* Create server node */
	NodeConfig server_cfg = node_config_default();
	strncpy(server_cfg.name, "msg_server", NODE_NAME_MAX);
	server_cfg.port = 9110;
	server_cfg.cookie = 0xCAFEBABE;

	DistributedNode *server = node_new(&server_cfg);
	server->on_message = on_message_callback;
	ASSERT(node_start(server));

	/* Create client node */
	NodeConfig client_cfg = node_config_default();
	strncpy(client_cfg.name, "msg_client", NODE_NAME_MAX);
	client_cfg.port = 9111;
	client_cfg.cookie = 0xCAFEBABE;

	DistributedNode *client = node_new(&client_cfg);
	ASSERT(node_start(client));

	/* Connect */
	ASSERT(node_connect(client, "msg_server", "127.0.0.1", 9110));
	usleep(100000);

	/* Send message */
	const char *payload = "Hello, distributed world!";
	ASSERT(node_send(client, "msg_server", 42, 1, payload, strlen(payload)));

	/* Wait for message delivery */
	usleep(50000);

	/* Verify message received */
	ASSERT_EQ(1, message_count);
	ASSERT_EQ(42, last_target_pid);

	/* Cleanup */
	node_stop(server);
	node_stop(client);
	node_free(server);
	node_free(client);
}

/* Test 14: Multiple connections */
void test_multiple_connections(void)
{
	reset_callbacks();

	/* Create server */
	NodeConfig server_cfg = node_config_default();
	strncpy(server_cfg.name, "multi_server", NODE_NAME_MAX);
	server_cfg.port = 9112;
	server_cfg.cookie = 0x12345678;

	DistributedNode *server = node_new(&server_cfg);
	server->on_node_up = on_node_up_callback;
	ASSERT(node_start(server));

	/* Create multiple clients */
	NodeConfig client1_cfg = node_config_default();
	strncpy(client1_cfg.name, "client1", NODE_NAME_MAX);
	client1_cfg.port = 9113;
	client1_cfg.cookie = 0x12345678;
	DistributedNode *client1 = node_new(&client1_cfg);
	ASSERT(node_start(client1));

	NodeConfig client2_cfg = node_config_default();
	strncpy(client2_cfg.name, "client2", NODE_NAME_MAX);
	client2_cfg.port = 9114;
	client2_cfg.cookie = 0x12345678;
	DistributedNode *client2 = node_new(&client2_cfg);
	ASSERT(node_start(client2));

	/* Connect both clients */
	ASSERT(node_connect(client1, "multi_server", "127.0.0.1", 9112));
	ASSERT(node_connect(client2, "multi_server", "127.0.0.1", 9112));
	usleep(150000);

	/* Server should have 2 peers */
	ASSERT_EQ(2, server->peer_count);

	/* Cleanup */
	node_stop(server);
	node_stop(client1);
	node_stop(client2);
	node_free(server);
	node_free(client1);
	node_free(client2);
}

/* Test 15: Connection statistics */
void test_connection_statistics(void)
{
	NodeConfig server_cfg = node_config_default();
	strncpy(server_cfg.name, "stats_server", NODE_NAME_MAX);
	server_cfg.port = 9115;
	server_cfg.cookie = 0xABCDEF01;

	DistributedNode *server = node_new(&server_cfg);
	ASSERT(node_start(server));

	NodeConfig client_cfg = node_config_default();
	strncpy(client_cfg.name, "stats_client", NODE_NAME_MAX);
	client_cfg.port = 9116;
	client_cfg.cookie = 0xABCDEF01;

	DistributedNode *client = node_new(&client_cfg);
	ASSERT(node_start(client));

	ASSERT(node_connect(client, "stats_server", "127.0.0.1", 9115));
	usleep(100000);

	NodeConnection *peer = node_get_peer(client, "stats_server");
	ASSERT(peer != NULL);
	ASSERT(peer->connected_at > 0);
	/* Note: bytes_sent tracking not yet implemented */

	/* Send some data */
	node_send(client, "stats_server", 1, 2, "test", 4);
	usleep(50000);

	/* Check stats updated */
	ASSERT(peer->messages_sent > 0);

	node_stop(server);
	node_stop(client);
	node_free(server);
	node_free(client);
}

int main(void)
{
	printf("=== E2E Distributed Node Tests ===\n\n");

	RUN_TEST(test_node_config_default);
	RUN_TEST(test_node_creation);
	RUN_TEST(test_node_default_creation);
	RUN_TEST(test_node_identity);
	RUN_TEST(test_node_start_stop);
	RUN_TEST(test_node_parse_ref);
	RUN_TEST(test_node_format_ref);
	RUN_TEST(test_node_callbacks);
	RUN_TEST(test_node_monitoring);
	RUN_TEST(test_empty_peer_list);
	RUN_TEST(test_node_unique_id);
	RUN_TEST(test_peer_connection);
	RUN_TEST(test_message_sending);
	RUN_TEST(test_multiple_connections);
	RUN_TEST(test_connection_statistics);

	return TEST_RESULT();
}
