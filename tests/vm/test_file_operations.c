/*
 * Agim - File Operations Tests
 *
 * Tests for sandbox-based file I/O operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "../test_common.h"
#include "vm/sandbox.h"
#include "vm/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* Test directory for file operations */
static const char *TEST_DIR = "/tmp/agim_file_tests";

/* Setup/Teardown helpers */

static void setup_test_dir(void) {
    mkdir(TEST_DIR, 0755);
}

static void cleanup_test_dir(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    int ret = system(cmd);
    (void)ret;
}

static void create_test_file(const char *name, const char *content) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, name);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

/* Sandbox Lifecycle Tests */

void test_sandbox_new(void) {
    Sandbox *sb = sandbox_new();

    ASSERT(sb != NULL);
    ASSERT(sb->read_count == 0);
    ASSERT(sb->write_count == 0);
    ASSERT(!sb->allow_all);

    sandbox_free(sb);
}

void test_sandbox_new_permissive(void) {
    Sandbox *sb = sandbox_new_permissive();

    ASSERT(sb != NULL);
    ASSERT(sb->allow_all);

    sandbox_free(sb);
}

void test_sandbox_free_null(void) {
    /* Should not crash */
    sandbox_free(NULL);
    ASSERT(1);
}

/* Sandbox Configuration Tests */

void test_sandbox_allow_read(void) {
    Sandbox *sb = sandbox_new();

    ASSERT(sandbox_allow_read(sb, TEST_DIR));
    ASSERT(sb->read_count == 1);

    sandbox_free(sb);
}

void test_sandbox_allow_write(void) {
    Sandbox *sb = sandbox_new();

    ASSERT(sandbox_allow_write(sb, TEST_DIR));
    ASSERT(sb->write_count == 1);

    sandbox_free(sb);
}

void test_sandbox_allow_multiple(void) {
    Sandbox *sb = sandbox_new();

    ASSERT(sandbox_allow_read(sb, "/tmp"));
    ASSERT(sandbox_allow_read(sb, "/var"));
    ASSERT(sandbox_allow_write(sb, "/tmp"));

    ASSERT(sb->read_count == 2);
    ASSERT(sb->write_count == 1);

    sandbox_free(sb);
}

void test_sandbox_allow_cwd(void) {
    Sandbox *sb = sandbox_new();

    ASSERT(!sb->allow_cwd_read);
    ASSERT(!sb->allow_cwd_write);

    sandbox_allow_cwd(sb, true, false);
    ASSERT(sb->allow_cwd_read);
    ASSERT(!sb->allow_cwd_write);

    sandbox_allow_cwd(sb, true, true);
    ASSERT(sb->allow_cwd_read);
    ASSERT(sb->allow_cwd_write);

    sandbox_free(sb);
}

void test_sandbox_disable_enable(void) {
    Sandbox *sb = sandbox_new();

    sandbox_allow_read(sb, TEST_DIR);
    ASSERT(sandbox_check_read(sb, TEST_DIR));

    sandbox_disable(sb);
    ASSERT(sb->allow_all);

    sandbox_enable(sb);
    ASSERT(!sb->allow_all);

    sandbox_free(sb);
}

/* Path Validation Tests */

void test_sandbox_check_read_allowed(void) {
    setup_test_dir();
    create_test_file("test.txt", "content");

    Sandbox *sb = sandbox_new();
    sandbox_allow_read(sb, TEST_DIR);

    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", TEST_DIR);

    ASSERT(sandbox_check_read(sb, path));

    sandbox_free(sb);
    cleanup_test_dir();
}

void test_sandbox_check_read_denied(void) {
    Sandbox *sb = sandbox_new();

    /* No paths allowed */
    ASSERT(!sandbox_check_read(sb, "/etc/passwd"));
    ASSERT(!sandbox_check_read(sb, "/tmp/something"));

    sandbox_free(sb);
}

void test_sandbox_check_write_allowed(void) {
    setup_test_dir();

    Sandbox *sb = sandbox_new();
    sandbox_allow_write(sb, TEST_DIR);

    char path[256];
    snprintf(path, sizeof(path), "%s/newfile.txt", TEST_DIR);

    ASSERT(sandbox_check_write(sb, path));

    sandbox_free(sb);
    cleanup_test_dir();
}

void test_sandbox_check_write_denied(void) {
    Sandbox *sb = sandbox_new();

    /* No paths allowed for writing */
    ASSERT(!sandbox_check_write(sb, "/etc/passwd"));
    ASSERT(!sandbox_check_write(sb, "/tmp/something"));

    sandbox_free(sb);
}

void test_sandbox_read_only_no_write(void) {
    setup_test_dir();

    Sandbox *sb = sandbox_new();
    sandbox_allow_read(sb, TEST_DIR);

    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", TEST_DIR);

    /* Can read but not write */
    ASSERT(sandbox_check_read(sb, path));
    ASSERT(!sandbox_check_write(sb, path));

    sandbox_free(sb);
    cleanup_test_dir();
}

/* Path Traversal Prevention Tests */

void test_sandbox_path_traversal_dots(void) {
    setup_test_dir();

    Sandbox *sb = sandbox_new();
    sandbox_allow_read(sb, TEST_DIR);

    /* Attempt path traversal with .. */
    char malicious[256];
    snprintf(malicious, sizeof(malicious), "%s/../../../etc/passwd", TEST_DIR);

    ASSERT(!sandbox_check_read(sb, malicious));

    sandbox_free(sb);
    cleanup_test_dir();
}

void test_sandbox_path_traversal_double_slash(void) {
    setup_test_dir();

    Sandbox *sb = sandbox_new();
    sandbox_allow_read(sb, TEST_DIR);

    /* Double slashes should be handled */
    char path[256];
    snprintf(path, sizeof(path), "%s//subdir", TEST_DIR);

    /* This should still be allowed (within TEST_DIR) */
    ASSERT(sandbox_check_read(sb, path));

    sandbox_free(sb);
    cleanup_test_dir();
}

void test_sandbox_symlink_escape(void) {
    setup_test_dir();

    Sandbox *sb = sandbox_new();
    sandbox_allow_read(sb, TEST_DIR);

    /* Create a symlink that points outside */
    char link_path[256];
    snprintf(link_path, sizeof(link_path), "%s/escape", TEST_DIR);
    int link_ret = symlink("/etc", link_path);
    (void)link_ret;

    /* Reading through symlink should be blocked */
    char malicious[256];
    snprintf(malicious, sizeof(malicious), "%s/escape/passwd", TEST_DIR);

    ASSERT(!sandbox_check_read(sb, malicious));

    /* Cleanup */
    unlink(link_path);
    sandbox_free(sb);
    cleanup_test_dir();
}

/* Path Resolution Tests */

void test_sandbox_resolve_read_valid(void) {
    setup_test_dir();
    create_test_file("test.txt", "content");

    Sandbox *sb = sandbox_new();
    sandbox_allow_read(sb, TEST_DIR);

    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", TEST_DIR);

    char *resolved = sandbox_resolve_read(sb, path);
    ASSERT(resolved != NULL);
    free(resolved);

    sandbox_free(sb);
    cleanup_test_dir();
}

void test_sandbox_resolve_read_invalid(void) {
    Sandbox *sb = sandbox_new();

    char *resolved = sandbox_resolve_read(sb, "/etc/passwd");
    ASSERT(resolved == NULL);

    sandbox_free(sb);
}

void test_sandbox_resolve_write_valid(void) {
    setup_test_dir();

    Sandbox *sb = sandbox_new();
    sandbox_allow_write(sb, TEST_DIR);

    char path[256];
    snprintf(path, sizeof(path), "%s/newfile.txt", TEST_DIR);

    char *resolved = sandbox_resolve_write(sb, path);
    ASSERT(resolved != NULL);
    free(resolved);

    sandbox_free(sb);
    cleanup_test_dir();
}

void test_sandbox_resolve_write_invalid(void) {
    Sandbox *sb = sandbox_new();

    char *resolved = sandbox_resolve_write(sb, "/etc/passwd");
    ASSERT(resolved == NULL);

    sandbox_free(sb);
}

/* Path Utilities Tests */

void test_sandbox_canonicalize(void) {
    setup_test_dir();
    create_test_file("test.txt", "content");

    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", TEST_DIR);

    char *canonical = sandbox_canonicalize(path);
    ASSERT(canonical != NULL);

    /* Should be an absolute path */
    ASSERT(canonical[0] == '/');
    /* Should not contain /./  or /../ */
    ASSERT(strstr(canonical, "/./") == NULL);
    ASSERT(strstr(canonical, "/../") == NULL);

    free(canonical);
    cleanup_test_dir();
}

void test_sandbox_canonicalize_rejects_traversal(void) {
    setup_test_dir();

    char path[256];
    snprintf(path, sizeof(path), "%s/../../../etc/passwd", TEST_DIR);

    /* Paths with .. components should be rejected for security */
    char *canonical = sandbox_canonicalize(path);
    ASSERT(canonical == NULL);

    cleanup_test_dir();
}

void test_sandbox_path_within_true(void) {
    ASSERT(sandbox_path_within("/tmp", "/tmp/subdir/file.txt"));
    ASSERT(sandbox_path_within("/tmp", "/tmp/subdir"));
    ASSERT(sandbox_path_within("/var/log", "/var/log/messages"));
    /* Exact match should also work */
    ASSERT(sandbox_path_within("/tmp", "/tmp"));
}

void test_sandbox_path_within_false(void) {
    ASSERT(!sandbox_path_within("/tmp", "/var/file.txt"));
    ASSERT(!sandbox_path_within("/tmp/subdir", "/tmp/other"));
    ASSERT(!sandbox_path_within("/tmp", "/tmp2/file.txt"));
}

void test_sandbox_getcwd(void) {
    char *cwd = sandbox_getcwd();

    ASSERT(cwd != NULL);
    ASSERT(strlen(cwd) > 0);
    ASSERT(cwd[0] == '/'); /* Absolute path */

    free(cwd);
}

/* Global Sandbox Tests */

void test_sandbox_global(void) {
    Sandbox *global = sandbox_global();

    ASSERT(global != NULL);
    /* Global sandbox should be restrictive by default */
    ASSERT(!global->allow_all);
}

void test_sandbox_set_global(void) {
    /* Note: sandbox_set_global frees the old global sandbox,
     * so we can't save and restore it. Instead we verify the
     * mechanism works and leave a new sandbox as global. */
    Sandbox *new_sb = sandbox_new_permissive();

    sandbox_set_global(new_sb);

    Sandbox *current = sandbox_global();
    ASSERT(current == new_sb);
    ASSERT(current->allow_all);

    /* Set a restrictive sandbox as global for other tests */
    Sandbox *restrictive = sandbox_new();
    sandbox_set_global(restrictive);

    current = sandbox_global();
    ASSERT(current == restrictive);
    ASSERT(!current->allow_all);
}

/* Null Input Tests */

void test_sandbox_null_inputs(void) {
    /* All functions should handle NULL gracefully */
    ASSERT(!sandbox_check_read(NULL, "/tmp"));
    ASSERT(!sandbox_check_write(NULL, "/tmp"));
    ASSERT(sandbox_resolve_read(NULL, "/tmp") == NULL);
    ASSERT(sandbox_resolve_write(NULL, "/tmp") == NULL);

    Sandbox *sb = sandbox_new();
    ASSERT(!sandbox_check_read(sb, NULL));
    ASSERT(!sandbox_check_write(sb, NULL));
    ASSERT(sandbox_resolve_read(sb, NULL) == NULL);
    ASSERT(sandbox_resolve_write(sb, NULL) == NULL);
    sandbox_free(sb);

    /* Path utilities */
    ASSERT(sandbox_canonicalize(NULL) == NULL);
    ASSERT(!sandbox_path_within(NULL, "/tmp"));
    ASSERT(!sandbox_path_within("/tmp", NULL));
}

/* Main */

int main(void) {
    printf("Running file operations tests...\n\n");

    printf("Sandbox Lifecycle Tests:\n");
    RUN_TEST(test_sandbox_new);
    RUN_TEST(test_sandbox_new_permissive);
    RUN_TEST(test_sandbox_free_null);

    printf("\nSandbox Configuration Tests:\n");
    RUN_TEST(test_sandbox_allow_read);
    RUN_TEST(test_sandbox_allow_write);
    RUN_TEST(test_sandbox_allow_multiple);
    RUN_TEST(test_sandbox_allow_cwd);
    RUN_TEST(test_sandbox_disable_enable);

    printf("\nPath Validation Tests:\n");
    RUN_TEST(test_sandbox_check_read_allowed);
    RUN_TEST(test_sandbox_check_read_denied);
    RUN_TEST(test_sandbox_check_write_allowed);
    RUN_TEST(test_sandbox_check_write_denied);
    RUN_TEST(test_sandbox_read_only_no_write);

    printf("\nPath Traversal Prevention Tests:\n");
    RUN_TEST(test_sandbox_path_traversal_dots);
    RUN_TEST(test_sandbox_path_traversal_double_slash);
    RUN_TEST(test_sandbox_symlink_escape);

    printf("\nPath Resolution Tests:\n");
    RUN_TEST(test_sandbox_resolve_read_valid);
    RUN_TEST(test_sandbox_resolve_read_invalid);
    RUN_TEST(test_sandbox_resolve_write_valid);
    RUN_TEST(test_sandbox_resolve_write_invalid);

    printf("\nPath Utilities Tests:\n");
    RUN_TEST(test_sandbox_canonicalize);
    RUN_TEST(test_sandbox_canonicalize_rejects_traversal);
    RUN_TEST(test_sandbox_path_within_true);
    RUN_TEST(test_sandbox_path_within_false);
    RUN_TEST(test_sandbox_getcwd);

    printf("\nGlobal Sandbox Tests:\n");
    RUN_TEST(test_sandbox_global);
    RUN_TEST(test_sandbox_set_global);

    printf("\nNull Input Tests:\n");
    RUN_TEST(test_sandbox_null_inputs);

    return TEST_RESULT();
}
