/*
 * Agim - End-to-End Hot Code Reloading Tests
 *
 * Tests the hot code reloading infrastructure including module versioning,
 * upgrade triggering, state migration, and rollback capabilities. Validates
 * Erlang-style code replacement semantics.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "../test_common.h"
#include "runtime/module.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "vm/bytecode.h"
#include "vm/value.h"

/*
 * Helper: Create simple bytecode with version identifier
 */
static Bytecode *make_versioned_bytecode(int version)
{
	Bytecode *code = bytecode_new();
	Value *val = value_int(version);
	size_t idx = chunk_add_constant(code->main, val);

	chunk_write_opcode(code->main, OP_CONST, 1);
	chunk_write_arg(code->main, (uint16_t)idx, 1);  /* 2-byte index */
	chunk_write_opcode(code->main, OP_RETURN, 1);

	return code;
}

/* Test 1: Module registry creation */
void test_registry_creation(void)
{
	ModuleRegistry *reg = module_registry_new();
	ASSERT(reg != NULL);
	ASSERT_EQ(0, reg->count);

	module_registry_free(reg);
}

/* Test 2: Load module */
void test_load_module(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code = make_versioned_bytecode(1);
	ModuleVersion *ver = module_load(reg, "test_module", code);

	ASSERT(ver != NULL);
	ASSERT_STR_EQ("test_module", ver->name);
	ASSERT_EQ(1, ver->version);
	ASSERT(ver->code == code);

	/* Module should be in registry */
	ASSERT_EQ(1, reg->count);

	/* Can retrieve module */
	ModuleVersion *found = module_get(reg, "test_module");
	ASSERT(found == ver);

	bytecode_release(code);
	module_registry_free(reg);
}

/* Test 3: Load multiple modules */
void test_load_multiple_modules(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code1 = make_versioned_bytecode(1);
	Bytecode *code2 = make_versioned_bytecode(1);
	Bytecode *code3 = make_versioned_bytecode(1);

	module_load(reg, "module_a", code1);
	module_load(reg, "module_b", code2);
	module_load(reg, "module_c", code3);

	ASSERT_EQ(3, reg->count);

	/* Retrieve each */
	ASSERT(module_get(reg, "module_a") != NULL);
	ASSERT(module_get(reg, "module_b") != NULL);
	ASSERT(module_get(reg, "module_c") != NULL);

	/* Non-existent returns NULL */
	ASSERT(module_get(reg, "nonexistent") == NULL);

	bytecode_release(code1);
	bytecode_release(code2);
	bytecode_release(code3);
	module_registry_free(reg);
}

/* Test 4: Module version upgrade */
void test_module_upgrade(void)
{
	ModuleRegistry *reg = module_registry_new();

	/* Load version 1 */
	Bytecode *code_v1 = make_versioned_bytecode(1);
	ModuleVersion *v1 = module_load(reg, "upgradable", code_v1);
	ASSERT_EQ(1, v1->version);

	/* Load version 2 (upgrade) */
	Bytecode *code_v2 = make_versioned_bytecode(2);
	ModuleVersion *v2 = module_load(reg, "upgradable", code_v2);
	ASSERT_EQ(2, v2->version);

	/* Current version should be v2 */
	ModuleVersion *current = module_get(reg, "upgradable");
	ASSERT(current == v2);
	ASSERT_EQ(2, current->version);

	/* Previous version linked */
	ASSERT(v2->prev_version == v1);

	bytecode_release(code_v1);
	bytecode_release(code_v2);
	module_registry_free(reg);
}

/* Test 5: Get specific version */
void test_get_specific_version(void)
{
	ModuleRegistry *reg = module_registry_new();

	/* Load multiple versions */
	Bytecode *code_v1 = make_versioned_bytecode(1);
	Bytecode *code_v2 = make_versioned_bytecode(2);
	Bytecode *code_v3 = make_versioned_bytecode(3);

	module_load(reg, "versioned", code_v1);
	module_load(reg, "versioned", code_v2);
	module_load(reg, "versioned", code_v3);

	/* Get specific versions */
	ModuleVersion *v1 = module_get_version(reg, "versioned", 1);
	ModuleVersion *v2 = module_get_version(reg, "versioned", 2);
	ModuleVersion *v3 = module_get_version(reg, "versioned", 3);

	ASSERT(v1 != NULL);
	ASSERT(v2 != NULL);
	ASSERT(v3 != NULL);

	ASSERT_EQ(1, v1->version);
	ASSERT_EQ(2, v2->version);
	ASSERT_EQ(3, v3->version);

	/* Non-existent version returns NULL */
	ASSERT(module_get_version(reg, "versioned", 99) == NULL);

	bytecode_release(code_v1);
	bytecode_release(code_v2);
	bytecode_release(code_v3);
	module_registry_free(reg);
}

/* Test 6: Module list */
void test_module_list(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code1 = make_versioned_bytecode(1);
	Bytecode *code2 = make_versioned_bytecode(1);

	module_load(reg, "alpha", code1);
	module_load(reg, "beta", code2);

	size_t count;
	const Module **modules = module_list(reg, &count);

	ASSERT_EQ(2, count);
	ASSERT(modules != NULL);

	/* Verify modules in list */
	bool found_alpha = false, found_beta = false;
	for (size_t i = 0; i < count; i++) {
		if (strcmp(modules[i]->name, "alpha") == 0) found_alpha = true;
		if (strcmp(modules[i]->name, "beta") == 0) found_beta = true;
	}
	ASSERT(found_alpha && found_beta);

	bytecode_release(code1);
	bytecode_release(code2);
	module_registry_free(reg);
}

/* Test 7: Register block with module */
void test_register_block(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code = make_versioned_bytecode(1);
	module_load(reg, "tracked", code);

	/* Register blocks */
	ASSERT(module_register_block(reg, "tracked", 100));
	ASSERT(module_register_block(reg, "tracked", 200));
	ASSERT(module_register_block(reg, "tracked", 300));

	/* Non-existent module fails */
	ASSERT(!module_register_block(reg, "nonexistent", 400));

	bytecode_release(code);
	module_registry_free(reg);
}

/* Test 8: Trigger upgrade */
void test_trigger_upgrade(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code_v1 = make_versioned_bytecode(1);
	Bytecode *code_v2 = make_versioned_bytecode(2);

	module_load(reg, "upgrading", code_v1);
	module_register_block(reg, "upgrading", 100);

	/* Load new version */
	module_load(reg, "upgrading", code_v2);

	/* Trigger upgrade */
	UpgradeConfig config = {
		.require_migrate = false,
		.rollback_on_error = true,
		.timeout_ms = 5000,
	};

	ASSERT(module_trigger_upgrade(reg, "upgrading", &config));

	/* Block should have pending upgrade */
	ASSERT(module_has_pending_upgrade(reg, "upgrading", 100));

	bytecode_release(code_v1);
	bytecode_release(code_v2);
	module_registry_free(reg);
}

/* Test 9: Apply upgrade */
void test_apply_upgrade(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code_v1 = make_versioned_bytecode(1);
	Bytecode *code_v2 = make_versioned_bytecode(2);

	module_load(reg, "applying", code_v1);
	module_register_block(reg, "applying", 100);
	module_load(reg, "applying", code_v2);

	UpgradeConfig config = {
		.require_migrate = false,
		.rollback_on_error = false,
		.timeout_ms = 5000,
	};
	module_trigger_upgrade(reg, "applying", &config);

	/* Apply upgrade with state */
	Value *old_state = value_int(42);
	Value *new_state = NULL;

	ASSERT(module_apply_upgrade(reg, "applying", 100, old_state, &new_state));

	/* No migrate function, state should be passed through */
	ASSERT(new_state == old_state);

	/* Pending upgrade should be cleared */
	ASSERT(!module_has_pending_upgrade(reg, "applying", 100));

	bytecode_release(code_v1);
	bytecode_release(code_v2);
	module_registry_free(reg);
}

/* Test 10: Module rollback */
void test_module_rollback(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code_v1 = make_versioned_bytecode(1);
	Bytecode *code_v2 = make_versioned_bytecode(2);

	module_load(reg, "rollbackable", code_v1);
	module_load(reg, "rollbackable", code_v2);

	/* Current is v2 */
	ASSERT_EQ(2, module_get(reg, "rollbackable")->version);

	/* Rollback to v1 */
	ASSERT(module_rollback(reg, "rollbackable"));

	/* Current should now be v1 */
	ASSERT_EQ(1, module_get(reg, "rollbackable")->version);

	/* Can't rollback if no previous version */
	ASSERT(!module_rollback(reg, "rollbackable"));

	bytecode_release(code_v1);
	bytecode_release(code_v2);
	module_registry_free(reg);
}

/* Test 11: Version reference counting */
void test_version_refcount(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code = make_versioned_bytecode(1);
	ModuleVersion *ver = module_load(reg, "refcounted", code);

	/* Initial refcount should be 1 */
	ASSERT_EQ(1, atomic_load(&ver->ref_count));

	bytecode_release(code);
	module_registry_free(reg);
}

/* Test 12: Module loaded timestamp */
void test_module_timestamp(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code = make_versioned_bytecode(1);
	ModuleVersion *ver = module_load(reg, "timestamped", code);

	/* Should have valid timestamp */
	ASSERT(ver->loaded_at > 0);

	bytecode_release(code);
	module_registry_free(reg);
}

/* Test 13: Multiple blocks tracking same module */
void test_multiple_blocks_same_module(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code_v1 = make_versioned_bytecode(1);
	module_load(reg, "shared", code_v1);

	/* Multiple blocks use this module */
	module_register_block(reg, "shared", 10);
	module_register_block(reg, "shared", 20);
	module_register_block(reg, "shared", 30);

	/* Load new version */
	Bytecode *code_v2 = make_versioned_bytecode(2);
	module_load(reg, "shared", code_v2);

	UpgradeConfig config = {
		.require_migrate = false,
		.rollback_on_error = false,
		.timeout_ms = 5000,
	};
	module_trigger_upgrade(reg, "shared", &config);

	/* All blocks should have pending upgrade */
	ASSERT(module_has_pending_upgrade(reg, "shared", 10));
	ASSERT(module_has_pending_upgrade(reg, "shared", 20));
	ASSERT(module_has_pending_upgrade(reg, "shared", 30));

	bytecode_release(code_v1);
	bytecode_release(code_v2);
	module_registry_free(reg);
}

/* Test 14: Concurrent version access */
void test_concurrent_version_access(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code_v1 = make_versioned_bytecode(1);
	Bytecode *code_v2 = make_versioned_bytecode(2);

	module_load(reg, "concurrent", code_v1);

	/* Block 1 uses v1 */
	module_register_block(reg, "concurrent", 100);
	ModuleVersion *v1 = module_get(reg, "concurrent");
	ASSERT_EQ(1, v1->version);

	/* Load v2 */
	module_load(reg, "concurrent", code_v2);

	/* Block 1 can still access v1 */
	ModuleVersion *still_v1 = module_get_version(reg, "concurrent", 1);
	ASSERT(still_v1 == v1);
	ASSERT_EQ(1, still_v1->version);

	/* New queries get v2 */
	ModuleVersion *v2 = module_get(reg, "concurrent");
	ASSERT_EQ(2, v2->version);

	bytecode_release(code_v1);
	bytecode_release(code_v2);
	module_registry_free(reg);
}

/* Test 15: Upgrade with migration function index */
void test_migration_function_index(void)
{
	ModuleRegistry *reg = module_registry_new();

	Bytecode *code = make_versioned_bytecode(1);
	ModuleVersion *ver = module_load(reg, "migratable", code);

	/* Set migration function index */
	ver->migrate_func_index = 42;
	ASSERT_EQ(42, ver->migrate_func_index);

	bytecode_release(code);
	module_registry_free(reg);
}

int main(void)
{
	printf("=== E2E Hot Code Reloading Tests ===\n\n");

	RUN_TEST(test_registry_creation);
	RUN_TEST(test_load_module);
	RUN_TEST(test_load_multiple_modules);
	RUN_TEST(test_module_upgrade);
	RUN_TEST(test_get_specific_version);
	RUN_TEST(test_module_list);
	RUN_TEST(test_register_block);
	RUN_TEST(test_trigger_upgrade);
	RUN_TEST(test_apply_upgrade);
	RUN_TEST(test_module_rollback);
	RUN_TEST(test_version_refcount);
	RUN_TEST(test_module_timestamp);
	RUN_TEST(test_multiple_blocks_same_module);
	RUN_TEST(test_concurrent_version_access);
	RUN_TEST(test_migration_function_index);

	return TEST_RESULT();
}
