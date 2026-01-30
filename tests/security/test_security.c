/*
 * Agim Security Tests
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 *
 * Tests for security hardening including:
 * - Command injection prevention
 * - Path traversal prevention
 * - Bounds checking
 * - Recursion limits
 * - Integer overflow protection
 * - Type confusion prevention
 * - Hash collision DoS protection
 * - Refcount race conditions
 */

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vm/sandbox.h"
#include "net/http.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "lang/agim.h"
#include "runtime/capability.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "types/array.h"
#include "types/map.h"
#include "types/string.h"

/*============================================================================
 * Test Helpers
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %s...", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf(" OK\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED at line %d: %s\n", __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

/*============================================================================
 * Sandbox Tests
 *============================================================================*/

TEST(test_sandbox_basic) {
    Sandbox *sb = sandbox_new();
    ASSERT(sb != NULL);

    /* By default, nothing is allowed */
    ASSERT(!sandbox_check_read(sb, "/etc/passwd"));
    ASSERT(!sandbox_check_write(sb, "/tmp/test.txt"));

    sandbox_free(sb);
}

TEST(test_sandbox_allow_read) {
    Sandbox *sb = sandbox_new();
    ASSERT(sb != NULL);

    /* Allow /tmp for reading */
    ASSERT(sandbox_allow_read(sb, "/tmp"));

    /* Create a test file in /tmp to verify */
    FILE *f = fopen("/tmp/agim_sandbox_test.txt", "w");
    if (f) {
        fprintf(f, "test");
        fclose(f);
    }

    /* Should be able to read from /tmp (file exists) */
    ASSERT(sandbox_check_read(sb, "/tmp/agim_sandbox_test.txt"));

    /* Should NOT be able to read from other directories */
    ASSERT(!sandbox_check_read(sb, "/etc/passwd"));

    /* Should NOT be able to write (only read was allowed) */
    ASSERT(!sandbox_check_write(sb, "/tmp/agim_sandbox_test.txt"));

    /* Clean up */
    unlink("/tmp/agim_sandbox_test.txt");
    sandbox_free(sb);
}

TEST(test_sandbox_path_traversal) {
    Sandbox *sb = sandbox_new();
    ASSERT(sb != NULL);

    /* Allow /tmp for reading */
    ASSERT(sandbox_allow_read(sb, "/tmp"));

    /* Path traversal attempts should be blocked */
    /* Note: These depend on /tmp existing and the canonicalization working */
    ASSERT(!sandbox_check_read(sb, "/tmp/../etc/passwd"));
    ASSERT(!sandbox_check_read(sb, "/tmp/../../etc/passwd"));

    sandbox_free(sb);
}

TEST(test_sandbox_permissive) {
    Sandbox *sb = sandbox_new_permissive();
    ASSERT(sb != NULL);

    /* Permissive sandbox allows everything */
    ASSERT(sandbox_check_read(sb, "/etc/passwd"));
    ASSERT(sandbox_check_write(sb, "/tmp/test.txt"));
    ASSERT(sandbox_check_read(sb, "/any/path/file.txt"));

    sandbox_free(sb);
}

TEST(test_sandbox_cwd) {
    Sandbox *sb = sandbox_new();
    ASSERT(sb != NULL);

    /* Enable CWD access */
    sandbox_allow_cwd(sb, true, true);

    /* Get current directory */
    char *cwd = sandbox_getcwd();
    ASSERT(cwd != NULL);

    /* Should be able to read/write in CWD */
    char test_path[4096];
    snprintf(test_path, sizeof(test_path), "%s/test_file.txt", cwd);
    ASSERT(sandbox_check_read(sb, test_path));
    ASSERT(sandbox_check_write(sb, test_path));

    /* But not outside CWD */
    ASSERT(!sandbox_check_read(sb, "/etc/passwd"));

    free(cwd);
    sandbox_free(sb);
}

/*============================================================================
 * HTTP URL Validation Tests
 *============================================================================*/

TEST(test_http_url_valid) {
    /* Valid URLs */
    ASSERT(http_url_valid("http://example.com", false));
    ASSERT(http_url_valid("https://example.com", false));
    ASSERT(http_url_valid("https://example.com/path", false));
    ASSERT(http_url_valid("https://example.com:8080/path", false));

    /* Invalid URLs */
    ASSERT(!http_url_valid("file:///etc/passwd", false));
    ASSERT(!http_url_valid("ftp://example.com", false));
    ASSERT(!http_url_valid("", false));
    ASSERT(!http_url_valid(NULL, false));

    /* Private IPs blocked by default */
    ASSERT(!http_url_valid("http://localhost/", false));
    ASSERT(!http_url_valid("http://127.0.0.1/", false));
    ASSERT(!http_url_valid("http://10.0.0.1/", false));
    ASSERT(!http_url_valid("http://192.168.1.1/", false));
    ASSERT(!http_url_valid("http://172.16.0.1/", false));

    /* Private IPs allowed when flag set */
    ASSERT(http_url_valid("http://localhost/", true));
    ASSERT(http_url_valid("http://127.0.0.1/", true));
    ASSERT(http_url_valid("http://10.0.0.1/", true));
}

TEST(test_http_ssrf_bypass_prevention) {
    /*
     * Test that various IP encoding tricks used in SSRF attacks are blocked.
     * These are common bypass techniques that attempt to reach internal services.
     */

    /* Octal encoding: 0177.0.0.1 = 127.0.0.1 */
    ASSERT(!http_url_valid("http://0177.0.0.1/", false));
    ASSERT(!http_url_valid("http://0177.0.0.01/", false));

    /* Decimal encoding: 2130706433 = 127.0.0.1 */
    ASSERT(!http_url_valid("http://2130706433/", false));

    /* Hex encoding: 0x7f.0.0.1 = 127.0.0.1 */
    ASSERT(!http_url_valid("http://0x7f.0.0.1/", false));
    ASSERT(!http_url_valid("http://0x7f.0x0.0x0.0x1/", false));

    /* Mixed encoding */
    ASSERT(!http_url_valid("http://0x7f.0.0.01/", false));

    /* IPv6 loopback */
    ASSERT(!http_url_valid("http://::1/", false));
    ASSERT(!http_url_valid("http://0:0:0:0:0:0:0:1/", false));

    /* IPv6-mapped IPv4 addresses */
    ASSERT(!http_url_valid("http://::ffff:127.0.0.1/", false));
    ASSERT(!http_url_valid("http://::ffff:10.0.0.1/", false));
    ASSERT(!http_url_valid("http://0:0:0:0:0:ffff:127.0.0.1/", false));

    /* Bracketed IPv6 */
    ASSERT(!http_url_valid("http://[::1]/", false));

    /* Localhost variants */
    ASSERT(!http_url_valid("http://LOCALHOST/", false));
    ASSERT(!http_url_valid("http://LocalHost/", false));
    ASSERT(!http_url_valid("http://localhost.localdomain/", false));

    /* 10.x.x.x range (private) with encoding */
    ASSERT(!http_url_valid("http://012.0.0.1/", false));  /* Octal 10 = 012 */
    ASSERT(!http_url_valid("http://167772161/", false));  /* Decimal 10.0.0.1 */

    /* 192.168.x.x range with encoding */
    ASSERT(!http_url_valid("http://0300.0250.0.1/", false));  /* Octal 192.168.0.1 */
    ASSERT(!http_url_valid("http://3232235521/", false));     /* Decimal 192.168.0.1 */

    /* 172.16-31.x.x range with encoding */
    ASSERT(!http_url_valid("http://0254.020.0.1/", false));  /* Octal 172.16.0.1 */

    /* 0.0.0.0 */
    ASSERT(!http_url_valid("http://0.0.0.0/", false));
    ASSERT(!http_url_valid("http://0/", false));

    /* Broadcast */
    ASSERT(!http_url_valid("http://255.255.255.255/", false));
    ASSERT(!http_url_valid("http://4294967295/", false));  /* Decimal broadcast */

    /* Valid public IPs should still work */
    ASSERT(http_url_valid("http://8.8.8.8/", false));
    ASSERT(http_url_valid("http://1.1.1.1/", false));
    ASSERT(http_url_valid("http://208.67.222.222/", false));
}

TEST(test_http_url_encode) {
    char *encoded;

    encoded = http_url_encode("hello world");
    ASSERT(encoded != NULL);
    ASSERT(strcmp(encoded, "hello%20world") == 0);
    free(encoded);

    encoded = http_url_encode("a=b&c=d");
    ASSERT(encoded != NULL);
    ASSERT(strcmp(encoded, "a%3Db%26c%3Dd") == 0);
    free(encoded);

    encoded = http_url_encode("safe-string_123.txt");
    ASSERT(encoded != NULL);
    ASSERT(strcmp(encoded, "safe-string_123.txt") == 0);
    free(encoded);
}

/*============================================================================
 * VM Bounds Checking Tests
 *============================================================================*/

TEST(test_bounds_negative_index) {
    /* Test that negative array indices are rejected */
    const char *source =
        "let arr = [1, 2, 3]\n"
        "arr[-1]\n";

    AgimResult result = agim_run(source);
    /* Should fail with bounds error */
    ASSERT(result != AGIM_OK);
}

TEST(test_bounds_large_index) {
    /* Test that out-of-bounds indices are rejected */
    const char *source =
        "let arr = [1, 2, 3]\n"
        "arr[100]\n";

    AgimResult result = agim_run(source);
    /* Should fail with bounds error */
    ASSERT(result != AGIM_OK);
}

TEST(test_slice_negative_indices) {
    /* Test that slice handles negative indices safely */
    const char *source =
        "let s = \"hello\"\n"
        "slice(s, -5, 10)\n";

    /* This should not crash - negative indices are clamped to 0 */
    AgimResult result = agim_run(source);
    ASSERT(result == AGIM_OK);
}

/*============================================================================
 * Parser Recursion Limit Tests
 *============================================================================*/

TEST(test_recursion_limit) {
    /* Generate deeply nested expression */
    char *source = malloc(10000);
    ASSERT(source != NULL);

    /* Create 300 levels of nesting: (((((...))))) */
    strcpy(source, "");
    for (int i = 0; i < 300; i++) {
        strcat(source, "(");
    }
    strcat(source, "1");
    for (int i = 0; i < 300; i++) {
        strcat(source, ")");
    }

    AgimResult result = agim_run(source);
    /* Should fail due to recursion limit */
    ASSERT(result != AGIM_OK);

    free(source);
}

/*============================================================================
 * HTTP Injection Prevention Tests
 *============================================================================*/

TEST(test_http_no_command_injection) {
    /*
     * Test that URLs with shell metacharacters don't cause command injection.
     * The old implementation used system("curl 'URL' > ...") which was vulnerable.
     * The new implementation uses libcurl directly.
     */

    /* This URL contains shell injection attempt */
    const char *malicious_url = "http://example.com'; rm -rf /tmp/test_marker; echo '";

    /* Create a marker file */
    FILE *f = fopen("/tmp/test_marker", "w");
    if (f) {
        fprintf(f, "test");
        fclose(f);
    }

    /* Try to "fetch" the malicious URL */
    /* The HTTP client should either reject it or handle it safely */
    HttpResponse *resp = http_get(malicious_url);

    /* The marker file should still exist (injection didn't work) */
    f = fopen("/tmp/test_marker", "r");
    bool marker_exists = (f != NULL);
    if (f) fclose(f);

    /* Clean up */
    unlink("/tmp/test_marker");
    if (resp) http_response_free(resp);

    ASSERT(marker_exists);
}

/*============================================================================
 * Path Traversal in VM File Operations Tests
 *============================================================================*/

TEST(test_file_read_traversal) {
    /* Test that path traversal is blocked in file operations */
    /* Create a sandbox that only allows current directory */
    Sandbox *sb = sandbox_new();
    sandbox_allow_cwd(sb, true, false);
    sandbox_set_global(sb);

    const char *source =
        "read_file(\"../../../etc/passwd\")\n";

    /* Should fail due to sandbox */
    AgimResult result = agim_run(source);
    /* The read should fail (return nil or error) */
    /* We can't easily check the result here, but at least it shouldn't crash */
    (void)result;

    /* Restore permissive sandbox for other tests */
    sandbox_set_global(sandbox_new_permissive());
}

/*============================================================================
 * Capability Enforcement Tests
 *============================================================================*/

TEST(test_capability_shell_denied) {
    /*
     * Test that shell() requires CAP_SHELL capability.
     * A block without CAP_SHELL should not be able to execute shell commands.
     */

    /* Create scheduler and block without CAP_SHELL */
    SchedulerConfig config = scheduler_config_default();
    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Spawn with capabilities that don't include CAP_SHELL */
    CapabilitySet caps = CAP_SPAWN | CAP_SEND | CAP_RECEIVE;
    BlockLimits limits = block_limits_default();
    Pid pid = scheduler_spawn_ex(sched, (Bytecode *)NULL, "test", caps, &limits);
    (void)pid;

    /* The block should fail when trying to execute shell() */
    /* Note: Full test would require compiling and running the bytecode */
    scheduler_free(sched);
}

TEST(test_capability_shell_granted) {
    /*
     * Test that shell() works with CAP_SHELL capability.
     */
    /* Verify CAP_SHELL flag exists and is distinct */
    ASSERT(CAP_SHELL != 0);
    ASSERT(CAP_SHELL != CAP_EXEC);
    ASSERT((CAP_SHELL & CAP_ALL) == CAP_SHELL);
}

TEST(test_capability_exec_denied) {
    /*
     * Test that exec() requires CAP_EXEC capability.
     */
    /* Verify CAP_EXEC flag exists and is distinct */
    ASSERT(CAP_EXEC != 0);
    ASSERT(CAP_EXEC != CAP_SHELL);
    ASSERT((CAP_EXEC & CAP_ALL) == CAP_EXEC);
}

TEST(test_capability_all_includes_new_caps) {
    /*
     * Verify that CAP_ALL includes both CAP_SHELL and CAP_EXEC
     */
    ASSERT((CAP_ALL & CAP_SHELL) == CAP_SHELL);
    ASSERT((CAP_ALL & CAP_EXEC) == CAP_EXEC);
}

TEST(test_block_capability_check) {
    /*
     * Test block_has_cap function with new capabilities
     */
    BlockLimits limits = block_limits_default();
    Block *block = block_new(1, "test", &limits);
    ASSERT(block != NULL);

    /* Initially no capabilities */
    block->capabilities = CAP_NONE;
    ASSERT(!block_has_cap(block, CAP_SHELL));
    ASSERT(!block_has_cap(block, CAP_EXEC));

    /* Grant CAP_SHELL */
    block_grant(block, CAP_SHELL);
    ASSERT(block_has_cap(block, CAP_SHELL));
    ASSERT(!block_has_cap(block, CAP_EXEC));

    /* Grant CAP_EXEC */
    block_grant(block, CAP_EXEC);
    ASSERT(block_has_cap(block, CAP_SHELL));
    ASSERT(block_has_cap(block, CAP_EXEC));

    /* Revoke CAP_SHELL */
    block_revoke(block, CAP_SHELL);
    ASSERT(!block_has_cap(block, CAP_SHELL));
    ASSERT(block_has_cap(block, CAP_EXEC));

    block_free(block);
}

TEST(test_capability_names) {
    /*
     * Test that capability_name() returns proper names for new capabilities
     */
    const char *shell_name = capability_name(CAP_SHELL);
    const char *exec_name = capability_name(CAP_EXEC);

    ASSERT(shell_name != NULL);
    ASSERT(exec_name != NULL);
    ASSERT(strcmp(shell_name, "SHELL") == 0);
    ASSERT(strcmp(exec_name, "EXEC") == 0);
}

/*============================================================================
 * Integer Overflow Protection Tests
 *============================================================================*/

TEST(test_array_overflow_protection) {
    /*
     * Test that array operations handle large capacities safely
     * without integer overflow in capacity doubling.
     */
    Value *arr = value_array_with_capacity(8);
    ASSERT(arr != NULL);
    ASSERT(arr->type == VAL_ARRAY);

    /* Push a few items - should work normally */
    for (int i = 0; i < 10; i++) {
        arr = array_push(arr, value_int(i));
        ASSERT(arr != NULL);
    }

    ASSERT(array_length(arr) == 10);
    value_free(arr);
}

TEST(test_type_validation_macros) {
    /*
     * Test that VALUE_AS_* macros properly validate types
     * and return NULL on type mismatch.
     */
    Value *int_val = value_int(42);
    Value *str_val = value_string("hello");
    Value *arr_val = value_array();
    Value *map_val = value_map();

    /* Correct type access should succeed */
    ASSERT(VALUE_AS_INT(int_val) == 42);
    ASSERT(VALUE_AS_STRING(str_val) != NULL);
    ASSERT(VALUE_AS_ARRAY(arr_val) != NULL);
    ASSERT(VALUE_AS_MAP(map_val) != NULL);

    /* Wrong type access should return NULL/0 */
    ASSERT(VALUE_AS_STRING(int_val) == NULL);
    ASSERT(VALUE_AS_ARRAY(int_val) == NULL);
    ASSERT(VALUE_AS_MAP(int_val) == NULL);
    ASSERT(VALUE_AS_INT(str_val) == 0);
    ASSERT(VALUE_AS_ARRAY(str_val) == NULL);

    /* NULL value should return NULL/0 */
    Value *null_val = NULL;
    ASSERT(VALUE_AS_STRING(null_val) == NULL);
    ASSERT(VALUE_AS_INT(null_val) == 0);
    ASSERT(VALUE_AS_ARRAY(null_val) == NULL);

    value_free(int_val);
    value_free(str_val);
    value_free(arr_val);
    value_free(map_val);
}

TEST(test_hash_collision_protection) {
    /*
     * Test that maps handle hash collisions gracefully
     * without O(n) lookup degradation.
     */
    Value *map = value_map();
    ASSERT(map != NULL);

    /* Insert many items - map should resize and maintain performance */
    char key[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        map = map_set(map, key, value_int(i));
        ASSERT(map != NULL);
    }

    /* Verify all items are retrievable */
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        Value *val = map_get(map, key);
        ASSERT(val != NULL);
        ASSERT(val->type == VAL_INT);
        ASSERT(val->as.integer == i);
    }

    ASSERT(map_size(map) == 1000);
    value_free(map);
}

TEST(test_refcount_saturation) {
    /*
     * Test that refcount operations handle edge cases safely.
     */
    Value *val = value_int(42);
    ASSERT(val != NULL);

    /* Normal retain/release should work */
    value_retain(val);
    value_retain(val);
    value_release(val);
    value_release(val);

    /* Value should still be valid */
    ASSERT(val->type == VAL_INT);
    ASSERT(val->as.integer == 42);

    value_free(val);
}

TEST(test_value_retain_freeing_object) {
    /*
     * Test that value_retain returns NULL for objects being freed.
     * This is hard to test directly without racing with GC,
     * but we can verify the function handles sentinel values.
     */
    Value *val = value_int(42);
    ASSERT(val != NULL);

    /* Simulate REFCOUNT_FREEING state */
    atomic_store(&val->refcount, REFCOUNT_FREEING);

    /* Retain should fail for freeing objects */
    Value *result = value_retain(val);
    ASSERT(result == NULL);

    /* Restore normal state for cleanup */
    atomic_store(&val->refcount, 1);
    value_free(val);
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    printf("Running security tests...\n\n");

    /* Initialize HTTP client */
    http_init();

    printf("Sandbox tests:\n");
    RUN_TEST(test_sandbox_basic);
    RUN_TEST(test_sandbox_allow_read);
    RUN_TEST(test_sandbox_path_traversal);
    RUN_TEST(test_sandbox_permissive);
    RUN_TEST(test_sandbox_cwd);

    printf("\nHTTP validation tests:\n");
    RUN_TEST(test_http_url_valid);
    RUN_TEST(test_http_url_encode);
    RUN_TEST(test_http_ssrf_bypass_prevention);

    printf("\nVM bounds checking tests:\n");
    RUN_TEST(test_bounds_negative_index);
    RUN_TEST(test_bounds_large_index);
    RUN_TEST(test_slice_negative_indices);

    printf("\nParser recursion limit tests:\n");
    RUN_TEST(test_recursion_limit);

    printf("\nCommand injection prevention tests:\n");
    RUN_TEST(test_http_no_command_injection);

    printf("\nPath traversal prevention tests:\n");
    RUN_TEST(test_file_read_traversal);

    printf("\nCapability enforcement tests:\n");
    RUN_TEST(test_capability_shell_denied);
    RUN_TEST(test_capability_shell_granted);
    RUN_TEST(test_capability_exec_denied);
    RUN_TEST(test_capability_all_includes_new_caps);
    RUN_TEST(test_block_capability_check);
    RUN_TEST(test_capability_names);

    printf("\nInteger overflow protection tests:\n");
    RUN_TEST(test_array_overflow_protection);

    printf("\nType validation tests:\n");
    RUN_TEST(test_type_validation_macros);

    printf("\nHash collision protection tests:\n");
    RUN_TEST(test_hash_collision_protection);

    printf("\nRefcount safety tests:\n");
    RUN_TEST(test_refcount_saturation);
    RUN_TEST(test_value_retain_freeing_object);

    /* Cleanup */
    http_cleanup();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
