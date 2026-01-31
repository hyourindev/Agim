/*
 * Agim - End-to-End Checkpointing Tests
 *
 * Tests the state checkpointing infrastructure including snapshot creation,
 * serialization, persistence, and process restoration. Validates state
 * preservation for fault tolerance.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "../test_common.h"
#include "runtime/checkpoint.h"
#include "runtime/block.h"
#include "runtime/mailbox.h"
#include "runtime/scheduler.h"
#include "runtime/serialize.h"
#include "vm/bytecode.h"
#include "vm/value.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

/*
 * Helper: Create test directory for checkpoints
 */
static const char *test_checkpoint_dir = "/tmp/agim_test_checkpoints";

static void setup_checkpoint_dir(void)
{
	mkdir(test_checkpoint_dir, 0755);
}

static void cleanup_checkpoint_dir(void)
{
	/* Remove test files */
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", test_checkpoint_dir);
	system(cmd);
}

/* Test 1: Create checkpoint from block */
void test_checkpoint_create(void)
{
	Block *block = block_new(42, "checkpointed", NULL);
	block_grant(block, CAP_SEND | CAP_RECEIVE);

	Checkpoint *cp = checkpoint_create(block);
	ASSERT(cp != NULL);
	ASSERT_EQ(42, cp->original_pid);
	ASSERT_STR_EQ("checkpointed", cp->name);
	ASSERT(cp->timestamp_ms > 0);
	ASSERT(cp->checkpoint_id > 0);

	checkpoint_free(cp);
	block_free(block);
}

/* Test 2: Checkpoint captures block state */
void test_checkpoint_captures_state(void)
{
	Block *block = block_new(100, "stateful", NULL);
	block_grant(block, CAP_SPAWN | CAP_SEND | CAP_RECEIVE);

	/* Add some links */
	block_link(block, 200);
	block_link(block, 300);

	/* Set counters */
	block->counters.messages_sent = 50;
	block->counters.messages_received = 30;
	block->counters.reductions = 1000;

	Checkpoint *cp = checkpoint_create(block);
	ASSERT(cp != NULL);

	/* Verify state captured */
	ASSERT_EQ(100, cp->original_pid);
	ASSERT_EQ(50, cp->messages_sent);
	ASSERT_EQ(30, cp->messages_received);
	ASSERT_EQ(1000, cp->reductions);
	ASSERT_EQ(2, cp->link_count);

	/* Verify links captured */
	bool has_200 = false, has_300 = false;
	for (size_t i = 0; i < cp->link_count; i++) {
		if (cp->links[i] == 200) has_200 = true;
		if (cp->links[i] == 300) has_300 = true;
	}
	ASSERT(has_200 && has_300);

	checkpoint_free(cp);
	block_free(block);
}

/* Test 3: Checkpoint serialization */
void test_checkpoint_serialize(void)
{
	Block *block = block_new(1, "serialized", NULL);
	Checkpoint *cp = checkpoint_create(block);

	SerialBuffer buf;
	serial_buffer_init(&buf);

	ASSERT(checkpoint_serialize(cp, &buf));
	ASSERT(buf.size > 0);

	serial_buffer_free(&buf);
	checkpoint_free(cp);
	block_free(block);
}

/* Test 4: Checkpoint deserialization */
void test_checkpoint_deserialize(void)
{
	Block *block = block_new(42, "roundtrip", NULL);
	block->counters.reductions = 500;
	Checkpoint *original = checkpoint_create(block);

	/* Serialize */
	SerialBuffer buf;
	serial_buffer_init(&buf);
	checkpoint_serialize(original, &buf);

	/* Reset buffer for reading */
	buf.read_pos = 0;

	/* Deserialize */
	Checkpoint *restored = checkpoint_deserialize(&buf);
	ASSERT(restored != NULL);

	/* Verify data matches */
	ASSERT_EQ(original->original_pid, restored->original_pid);
	ASSERT_STR_EQ(original->name, restored->name);
	ASSERT_EQ(original->reductions, restored->reductions);
	ASSERT_EQ(original->checkpoint_id, restored->checkpoint_id);

	serial_buffer_free(&buf);
	checkpoint_free(original);
	checkpoint_free(restored);
	block_free(block);
}

/* Test 5: Save checkpoint to file */
void test_checkpoint_save(void)
{
	setup_checkpoint_dir();

	Block *block = block_new(1, "saved", NULL);
	Checkpoint *cp = checkpoint_create(block);

	char path[256];
	snprintf(path, sizeof(path), "%s/test_save.checkpoint", test_checkpoint_dir);

	ASSERT(checkpoint_save(cp, path));

	/* Verify file exists */
	struct stat st;
	ASSERT(stat(path, &st) == 0);
	ASSERT(st.st_size > 0);

	checkpoint_free(cp);
	block_free(block);
	cleanup_checkpoint_dir();
}

/* Test 6: Load checkpoint from file */
void test_checkpoint_load(void)
{
	setup_checkpoint_dir();

	Block *block = block_new(99, "loadable", NULL);
	block->counters.messages_sent = 42;
	Checkpoint *original = checkpoint_create(block);

	char path[256];
	snprintf(path, sizeof(path), "%s/test_load.checkpoint", test_checkpoint_dir);
	checkpoint_save(original, path);

	/* Load from file */
	Checkpoint *loaded = checkpoint_load(path);
	ASSERT(loaded != NULL);
	ASSERT_EQ(99, loaded->original_pid);
	ASSERT_STR_EQ("loadable", loaded->name);
	ASSERT_EQ(42, loaded->messages_sent);

	checkpoint_free(original);
	checkpoint_free(loaded);
	block_free(block);
	cleanup_checkpoint_dir();
}

/* Test 7: Restore process from checkpoint */
void test_checkpoint_restore(void)
{
	SchedulerConfig config = {
		.max_blocks = 100,
		.default_reductions = 1000,
		.num_workers = 0,
	};
	Scheduler *sched = scheduler_new(&config);

	/* Create and checkpoint a block */
	Block *original = block_new(50, "restorable", NULL);
	original->counters.reductions = 123;
	Checkpoint *cp = checkpoint_create(original);

	/* Restore to new process */
	Pid new_pid = checkpoint_restore(cp, sched);
	ASSERT(new_pid != 0);

	/* Get restored block */
	Block *restored = scheduler_get_block(sched, new_pid);
	ASSERT(restored != NULL);
	ASSERT_STR_EQ("restorable", restored->name);

	/* New PID should be different */
	ASSERT(new_pid != 50);

	checkpoint_free(cp);
	block_free(original);
	scheduler_free(sched);
}

/* Test 8: Checkpoint manager creation */
void test_checkpoint_manager_creation(void)
{
	setup_checkpoint_dir();

	CheckpointConfig config = {
		.enabled = true,
		.interval_ms = 5000,
		.checkpoint_on_exit = true,
		.storage_path = (char *)test_checkpoint_dir,
		.max_checkpoints = 10,
	};

	CheckpointManager *mgr = checkpoint_manager_new(&config);
	ASSERT(mgr != NULL);
	ASSERT(mgr->config.enabled);
	ASSERT_EQ(5000, mgr->config.interval_ms);

	checkpoint_manager_free(mgr);
	cleanup_checkpoint_dir();
}

/* Test 9: Managed checkpointing */
void test_managed_checkpoint(void)
{
	setup_checkpoint_dir();

	CheckpointConfig config = {
		.enabled = true,
		.interval_ms = 1000,
		.checkpoint_on_exit = false,
		.storage_path = (char *)test_checkpoint_dir,
		.max_checkpoints = 5,
	};

	CheckpointManager *mgr = checkpoint_manager_new(&config);
	Block *block = block_new(1, "managed", NULL);

	/* Create checkpoint through manager */
	Checkpoint *cp = checkpoint_manager_checkpoint(mgr, block);
	ASSERT(cp != NULL);
	ASSERT(cp->checkpoint_id > 0);

	checkpoint_free(cp);
	block_free(block);
	checkpoint_manager_free(mgr);
	cleanup_checkpoint_dir();
}

/* Test 10: Checkpoint with messages in mailbox */
void test_checkpoint_with_mailbox(void)
{
	Block *block = block_new(1, "mailbox_test", NULL);

	/* Add messages to mailbox */
	ASSERT(block_send(block, 10, value_int(100)));
	ASSERT(block_send(block, 20, value_string("hello")));

	/* Verify messages are in mailbox before checkpoint */
	ASSERT_EQ(2, mailbox_count(&block->mailbox));

	Checkpoint *cp = checkpoint_create(block);
	ASSERT(cp != NULL);
	ASSERT_EQ(2, cp->mailbox_count);

	/* Serialize and deserialize */
	SerialBuffer buf;
	serial_buffer_init(&buf);
	checkpoint_serialize(cp, &buf);
	buf.read_pos = 0;

	Checkpoint *restored = checkpoint_deserialize(&buf);
	ASSERT(restored != NULL);
	ASSERT_EQ(2, restored->mailbox_count);

	serial_buffer_free(&buf);
	checkpoint_free(cp);
	checkpoint_free(restored);
	block_free(block);
}

/* Test 11: Checkpoint capabilities preserved */
void test_checkpoint_capabilities(void)
{
	Block *block = block_new(1, "caps", NULL);
	CapabilitySet caps = CAP_SPAWN | CAP_SEND | CAP_INFER;
	block_grant(block, caps);

	Checkpoint *cp = checkpoint_create(block);
	ASSERT_EQ(caps, cp->capabilities);

	checkpoint_free(cp);
	block_free(block);
}

/* Test 12: Checkpoint parent preserved */
void test_checkpoint_parent(void)
{
	Block *block = block_new(1, "child", NULL);
	block->parent = 999;

	Checkpoint *cp = checkpoint_create(block);
	ASSERT_EQ(999, cp->parent);

	checkpoint_free(cp);
	block_free(block);
}

/* Test 13: Checkpoint version */
void test_checkpoint_version(void)
{
	Block *block = block_new(1, "versioned", NULL);
	Checkpoint *cp = checkpoint_create(block);

	ASSERT_EQ(CHECKPOINT_VERSION, cp->version);

	checkpoint_free(cp);
	block_free(block);
}

/* Test 14: Multiple checkpoints for same block */
void test_multiple_checkpoints(void)
{
	setup_checkpoint_dir();

	CheckpointConfig config = {
		.enabled = true,
		.interval_ms = 100,
		.checkpoint_on_exit = false,
		.storage_path = (char *)test_checkpoint_dir,
		.max_checkpoints = 10,
	};

	CheckpointManager *mgr = checkpoint_manager_new(&config);
	Block *block = block_new(1, "multi", NULL);

	/* Create multiple checkpoints */
	Checkpoint *cp1 = checkpoint_manager_checkpoint(mgr, block);
	uint64_t id1 = cp1->checkpoint_id;
	checkpoint_free(cp1);

	block->counters.reductions = 100;
	Checkpoint *cp2 = checkpoint_manager_checkpoint(mgr, block);
	uint64_t id2 = cp2->checkpoint_id;
	checkpoint_free(cp2);

	block->counters.reductions = 200;
	Checkpoint *cp3 = checkpoint_manager_checkpoint(mgr, block);
	uint64_t id3 = cp3->checkpoint_id;
	checkpoint_free(cp3);

	/* IDs should be unique and increasing */
	ASSERT(id2 > id1);
	ASSERT(id3 > id2);

	block_free(block);
	checkpoint_manager_free(mgr);
	cleanup_checkpoint_dir();
}

/* Test 15: Checkpoint cleanup (max checkpoints) */
void test_checkpoint_cleanup(void)
{
	setup_checkpoint_dir();

	CheckpointConfig config = {
		.enabled = true,
		.interval_ms = 100,
		.checkpoint_on_exit = false,
		.storage_path = (char *)test_checkpoint_dir,
		.max_checkpoints = 3,  /* Keep only 3 */
	};

	CheckpointManager *mgr = checkpoint_manager_new(&config);
	Block *block = block_new(1, "cleanup_test", NULL);

	/* Create 5 checkpoints */
	for (int i = 0; i < 5; i++) {
		Checkpoint *cp = checkpoint_manager_checkpoint(mgr, block);
		checkpoint_free(cp);
		block->counters.reductions += 10;
	}

	/* Request cleanup */
	checkpoint_manager_cleanup(mgr, "cleanup_test");

	/* List remaining checkpoints */
	size_t count;
	uint64_t *ids = checkpoint_manager_list(mgr, "cleanup_test", &count);

	/* Should have at most max_checkpoints */
	ASSERT(count <= 3);

	free(ids);
	block_free(block);
	checkpoint_manager_free(mgr);
	cleanup_checkpoint_dir();
}

int main(void)
{
	printf("=== E2E Checkpointing Tests ===\n\n");

	RUN_TEST(test_checkpoint_create);
	RUN_TEST(test_checkpoint_captures_state);
	RUN_TEST(test_checkpoint_serialize);
	RUN_TEST(test_checkpoint_deserialize);
	RUN_TEST(test_checkpoint_save);
	RUN_TEST(test_checkpoint_load);
	RUN_TEST(test_checkpoint_restore);
	RUN_TEST(test_checkpoint_manager_creation);
	RUN_TEST(test_managed_checkpoint);
	RUN_TEST(test_checkpoint_with_mailbox);
	RUN_TEST(test_checkpoint_capabilities);
	RUN_TEST(test_checkpoint_parent);
	RUN_TEST(test_checkpoint_version);
	RUN_TEST(test_multiple_checkpoints);
	RUN_TEST(test_checkpoint_cleanup);

	return TEST_RESULT();
}
