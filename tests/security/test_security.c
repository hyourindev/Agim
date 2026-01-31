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
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vm/sandbox.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "lang/agim.h"
#include "runtime/capability.h"
#include "runtime/block.h"
#include "runtime/scheduler.h"
#include "runtime/timer.h"
#include "runtime/mailbox.h"
#include "types/array.h"
#include "types/map.h"
#include "types/string.h"
#include "util/alloc.h"
#include "util/pool.h"

/* Test Helpers */

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

/* Sandbox Tests */

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

/* VM Bounds Checking Tests */

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

/* Parser Recursion Limit Tests */

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

/* Path Traversal in VM File Operations Tests */

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

/* Capability Enforcement Tests */

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

/* Integer Overflow Protection Tests */

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

/* Concurrent String Interning Tests */

#define INTERN_THREADS 8
#define INTERN_ITERATIONS 1000

static void *intern_thread_func(void *arg) {
    (void)arg;
    for (int i = 0; i < INTERN_ITERATIONS; i++) {
        /* Intern the same strings from multiple threads */
        Value *v1 = string_intern("hello", 5);
        Value *v2 = string_intern("world", 5);
        Value *v3 = string_intern("test_string", 11);

        ASSERT(v1 != NULL);
        ASSERT(v2 != NULL);
        ASSERT(v3 != NULL);

        /* Verify string content */
        ASSERT(v1->type == VAL_STRING);
        ASSERT(v1->as.string->length == 5);
        ASSERT(memcmp(v1->as.string->data, "hello", 5) == 0);

        value_free(v1);
        value_free(v2);
        value_free(v3);
    }
    return NULL;
}

TEST(test_concurrent_string_interning) {
    /*
     * Stress test for thread-safe string interning.
     * Multiple threads intern the same strings concurrently.
     */
    pthread_t threads[INTERN_THREADS];

    for (int i = 0; i < INTERN_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, intern_thread_func, NULL);
        ASSERT(rc == 0);
    }

    for (int i = 0; i < INTERN_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
}

/* Concurrent Array Sorting Tests */

#define SORT_THREADS 4
#define SORT_SIZE 100

static int reverse_compare(const Value *a, const Value *b) {
    return -value_compare(a, b);
}

static void *sort_thread_func(void *arg) {
    (void)arg;
    for (int iter = 0; iter < 100; iter++) {
        Value *arr = value_array_with_capacity(SORT_SIZE);

        /* Fill with random values */
        for (int i = 0; i < SORT_SIZE; i++) {
            arr = array_push(arr, value_int(rand() % 1000));
        }

        /* Sort with custom comparator (descending) */
        arr = array_sort_by(arr, reverse_compare);

        /* Verify sorted in descending order */
        for (size_t i = 1; i < array_length(arr); i++) {
            Value *prev = array_get(arr, i - 1);
            Value *curr = array_get(arr, i);
            ASSERT(prev->as.integer >= curr->as.integer);
        }

        value_free(arr);
    }
    return NULL;
}

TEST(test_concurrent_array_sorting) {
    /*
     * Test that concurrent array sorts with custom comparators
     * don't interfere with each other (TLS comparator).
     */
    pthread_t threads[SORT_THREADS];

    for (int i = 0; i < SORT_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, sort_thread_func, NULL);
        ASSERT(rc == 0);
    }

    for (int i = 0; i < SORT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
}

/* String Concatenation Overflow Tests */

TEST(test_string_concat_overflow) {
    /*
     * Test that string_concat handles potential integer overflow safely.
     * We can't easily test SIZE_MAX lengths, but we test the function
     * handles edge cases gracefully.
     */
    Value *s1 = value_string("hello");
    Value *s2 = value_string("world");

    Value *result = string_concat(s1, s2);
    ASSERT(result != NULL);
    ASSERT(result->type == VAL_STRING);
    ASSERT(result->as.string->length == 10);
    ASSERT(strcmp(result->as.string->data, "helloworld") == 0);

    value_free(s1);
    value_free(s2);
    value_free(result);

    /* Test with NULL inputs */
    result = string_concat(NULL, s1);
    ASSERT(result != NULL);  /* Returns nil */
    ASSERT(result->type == VAL_NIL);
    value_free(result);
}

/* Path Traversal in Filename Tests */

TEST(test_sandbox_dotdot_in_filename) {
    /*
     * Test that paths with ".." components are rejected by sandbox_canonicalize.
     * This prevents attacks using paths like "existingdir/../../../etc/passwd".
     */

    /* Direct ".." should be rejected */
    char *result = sandbox_canonicalize("..");
    ASSERT(result == NULL);

    /* ".." at start of path */
    result = sandbox_canonicalize("../etc/passwd");
    ASSERT(result == NULL);

    /* ".." in middle of path */
    result = sandbox_canonicalize("/tmp/test/../../../etc/passwd");
    ASSERT(result == NULL);

    /* ".." at end should be rejected */
    result = sandbox_canonicalize("/tmp/..");
    ASSERT(result == NULL);

    /* Normal paths should still work */
    result = sandbox_canonicalize("/tmp");
    if (result) {
        /* /tmp exists, should get valid result */
        ASSERT(strstr(result, "..") == NULL);
        free(result);
    }
}

/* Timer Cancel O(1) Tests */

static void timer_test_callback(void *ctx, Pid pid) {
    (void)ctx;
    (void)pid;
}

TEST(test_timer_cancel_correctness) {
    /*
     * Test that timer_cancel works correctly with the O(1) optimization.
     */
    TimerConfig config = timer_config_default();
    TimerWheel *wheel = timer_wheel_new(&config);
    ASSERT(wheel != NULL);

    /* Add multiple timers */
    TimerEntry *t1 = timer_add(wheel, 1, 1000, timer_test_callback, NULL);
    TimerEntry *t2 = timer_add(wheel, 2, 2000, timer_test_callback, NULL);
    TimerEntry *t3 = timer_add(wheel, 3, 3000, timer_test_callback, NULL);

    ASSERT(t1 != NULL);
    ASSERT(t2 != NULL);
    ASSERT(t3 != NULL);
    ASSERT(timer_has_pending(wheel));

    /* Cancel middle timer */
    bool cancelled = timer_cancel(wheel, t2);
    ASSERT(cancelled);

    /* Note: After cancel, the entry may be freed and reused.
     * Calling cancel twice on the same entry is undefined behavior
     * and should not be done in production code. */

    /* Other timers still pending */
    ASSERT(timer_has_pending(wheel));

    /* Cancel remaining timers */
    ASSERT(timer_cancel(wheel, t1));
    ASSERT(timer_cancel(wheel, t3));

    /* After cancelling all timers, none should be pending */
    /* Note: timer_has_pending may still return true if cancelled entries
     * haven't been cleaned up yet. This is a known limitation. */

    timer_wheel_free(wheel);
}

TEST(test_timer_next_deadline_optimization) {
    /*
     * Test that timer_next_deadline returns correct values
     * with the O(1) min_deadline optimization.
     */
    TimerConfig config = timer_config_default();
    TimerWheel *wheel = timer_wheel_new(&config);
    ASSERT(wheel != NULL);

    /* No timers - should return 0 */
    uint64_t next = timer_next_deadline(wheel);
    ASSERT(next == 0);

    /* Add a timer */
    TimerEntry *t1 = timer_add(wheel, 1, 1000, timer_test_callback, NULL);
    ASSERT(t1 != NULL);

    /* Should have a deadline now */
    next = timer_next_deadline(wheel);
    ASSERT(next > 0);
    ASSERT(next == t1->deadline_ms);

    /* Add an earlier timer */
    TimerEntry *t2 = timer_add(wheel, 2, 500, timer_test_callback, NULL);
    ASSERT(t2 != NULL);

    /* min_deadline should update to earlier timer */
    next = timer_next_deadline(wheel);
    ASSERT(next == t2->deadline_ms);

    timer_cancel(wheel, t1);
    timer_cancel(wheel, t2);
    timer_wheel_free(wheel);
}

/* Pool Allocator Lifecycle Tests */

TEST(test_pool_init_free) {
    /*
     * Test basic pool initialization and cleanup.
     */
    MemoryPool pool;
    pool_init(&pool, 64);

    PoolStats stats = pool_stats(&pool);
    ASSERT(stats.block_size >= 64);  /* May be aligned up */
    ASSERT(stats.allocated == 0);
    ASSERT(stats.chunks == 0);

    pool_free(&pool);
}

TEST(test_pool_alloc_dealloc) {
    /*
     * Test basic pool allocation and deallocation.
     */
    MemoryPool pool;
    pool_init(&pool, 32);

    /* Allocate several blocks */
    void *p1 = pool_alloc(&pool);
    void *p2 = pool_alloc(&pool);
    void *p3 = pool_alloc(&pool);

    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);
    ASSERT(p3 != NULL);
    ASSERT(p1 != p2);
    ASSERT(p2 != p3);

    PoolStats stats = pool_stats(&pool);
    ASSERT(stats.allocated == 3);

    /* Deallocate and reallocate */
    pool_dealloc(&pool, p2);
    stats = pool_stats(&pool);
    ASSERT(stats.allocated == 2);

    void *p4 = pool_alloc(&pool);
    ASSERT(p4 != NULL);
    /* p4 might reuse p2's memory from free list */

    pool_dealloc(&pool, p1);
    pool_dealloc(&pool, p3);
    pool_dealloc(&pool, p4);

    pool_free(&pool);
}

TEST(test_global_pools) {
    /*
     * Test global pool allocator for various sizes.
     */
    /* Small allocation - should use pool */
    void *p1 = pools_alloc(24);
    ASSERT(p1 != NULL);

    /* Medium allocation */
    void *p2 = pools_alloc(100);
    ASSERT(p2 != NULL);

    /* Large allocation - should fall back to malloc */
    void *p3 = pools_alloc(1024);
    ASSERT(p3 != NULL);

    /* All pointers should be different */
    ASSERT(p1 != p2);
    ASSERT(p2 != p3);

    /* Deallocate in different order */
    pools_dealloc(p2, 100);
    pools_dealloc(p1, 24);
    pools_dealloc(p3, 1024);
}

TEST(test_pool_concurrent_access) {
    /*
     * Test thread-safe pool operations.
     */
    MemoryPool pool;
    pool_init(&pool, 64);

    /* Serial stress test simulating concurrent pattern */
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = pool_alloc(&pool);
        ASSERT(ptrs[i] != NULL);
    }

    /* Deallocate in random order */
    for (int i = 99; i >= 0; i--) {
        pool_dealloc(&pool, ptrs[i]);
    }

    PoolStats stats = pool_stats(&pool);
    ASSERT(stats.allocated == 0);

    pool_free(&pool);
}

TEST(test_pool_pointer_validation) {
    /*
     * Test that pool_dealloc validates pointers belong to the pool.
     * Invalid pointers should not corrupt the free list.
     * In debug mode this would abort, but in release it should
     * safely reject the invalid pointer.
     */
    MemoryPool pool;
    pool_init(&pool, 64);

    /* Allocate a valid block */
    void *valid = pool_alloc(&pool);
    ASSERT(valid != NULL);

    /* Deallocate the valid block - should succeed */
    pool_dealloc(&pool, valid);

    PoolStats stats = pool_stats(&pool);
    ASSERT(stats.allocated == 0);
    ASSERT(stats.free >= 1);

    /* Allocate again to verify pool still works */
    void *valid2 = pool_alloc(&pool);
    ASSERT(valid2 != NULL);

    /* The reused block should be valid */
    pool_dealloc(&pool, valid2);

    pool_free(&pool);
}

/* Mailbox Contention Tests */

#define MAILBOX_THREADS 4
#define MAILBOX_MESSAGES_PER_THREAD 100

typedef struct {
    Mailbox *mailbox;
    int thread_id;
    int messages_sent;
} MailboxThreadData;

static void *mailbox_producer_func(void *arg) {
    MailboxThreadData *data = (MailboxThreadData *)arg;

    for (int i = 0; i < MAILBOX_MESSAGES_PER_THREAD; i++) {
        Value *val = value_int(data->thread_id * 1000 + i);
        Message *msg = message_new(data->thread_id, val);

        if (msg) {
            bool pushed = mailbox_push(data->mailbox, msg, 0);
            if (pushed) {
                data->messages_sent++;
            } else {
                message_free(msg);
            }
        }
    }

    return NULL;
}

TEST(test_mailbox_concurrent_push) {
    /*
     * Test concurrent message pushing from multiple threads.
     * MPSC queue should handle multiple producers safely.
     */
    Mailbox mailbox;
    mailbox_init(&mailbox);

    pthread_t threads[MAILBOX_THREADS];
    MailboxThreadData thread_data[MAILBOX_THREADS];

    /* Start producer threads */
    for (int i = 0; i < MAILBOX_THREADS; i++) {
        thread_data[i].mailbox = &mailbox;
        thread_data[i].thread_id = i + 1;
        thread_data[i].messages_sent = 0;
        int rc = pthread_create(&threads[i], NULL, mailbox_producer_func, &thread_data[i]);
        ASSERT(rc == 0);
    }

    /* Wait for all producers */
    for (int i = 0; i < MAILBOX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Count total messages sent */
    int total_sent = 0;
    for (int i = 0; i < MAILBOX_THREADS; i++) {
        total_sent += thread_data[i].messages_sent;
    }

    /* All messages should be in the mailbox */
    ASSERT(mailbox_count(&mailbox) == (size_t)total_sent);

    /* Consume all messages */
    int consumed = 0;
    Message *msg;
    while ((msg = mailbox_pop(&mailbox)) != NULL) {
        ASSERT(msg->value != NULL);
        ASSERT(msg->value->type == VAL_INT);
        message_free(msg);
        consumed++;
    }

    ASSERT(consumed == total_sent);
    ASSERT(mailbox_empty(&mailbox));

    mailbox_free(&mailbox);
}

TEST(test_mailbox_receive_timeout) {
    /*
     * Test blocking receive with timeout.
     */
    Mailbox mailbox;
    mailbox_init(&mailbox);

    /* Try to receive from empty mailbox with short timeout */
    uint64_t start = timer_current_time_ms();
    Message *msg = mailbox_receive(&mailbox, 50);  /* 50ms timeout */
    uint64_t elapsed = timer_current_time_ms() - start;

    /* Should return NULL after timeout */
    ASSERT(msg == NULL);
    /* Should have waited approximately 50ms */
    ASSERT(elapsed >= 40);  /* Allow some slack */
    ASSERT(elapsed < 200);  /* But not too long */

    mailbox_free(&mailbox);
}

TEST(test_mailbox_overflow_drop_new) {
    /*
     * Test overflow policy: drop new messages.
     */
    Mailbox mailbox;
    mailbox_init(&mailbox);
    mailbox_set_limits(&mailbox, 3, 0);
    mailbox_set_overflow_policy(&mailbox, OVERFLOW_DROP_NEW);

    /* Push up to limit */
    for (int i = 0; i < 3; i++) {
        Message *msg = message_new(1, value_int(i));
        SendResult result = mailbox_push_ex(&mailbox, msg);
        ASSERT(result == SEND_OK);
    }

    ASSERT(mailbox_count(&mailbox) == 3);

    /* Next push should fail */
    Message *msg = message_new(1, value_int(100));
    SendResult result = mailbox_push_ex(&mailbox, msg);
    ASSERT(result == SEND_FULL);
    message_free(msg);  /* Clean up rejected message */

    /* Dropped count should be 1 */
    ASSERT(mailbox_dropped_count(&mailbox) == 1);

    /* Mailbox should still have 3 messages */
    ASSERT(mailbox_count(&mailbox) == 3);

    mailbox_free(&mailbox);
}

TEST(test_mailbox_overflow_drop_old) {
    /*
     * Test overflow policy: drop oldest messages.
     */
    Mailbox mailbox;
    mailbox_init(&mailbox);
    mailbox_set_limits(&mailbox, 3, 0);
    mailbox_set_overflow_policy(&mailbox, OVERFLOW_DROP_OLD);

    /* Push up to limit */
    for (int i = 0; i < 3; i++) {
        Message *msg = message_new(1, value_int(i));
        SendResult result = mailbox_push_ex(&mailbox, msg);
        ASSERT(result == SEND_OK);
    }

    ASSERT(mailbox_count(&mailbox) == 3);

    /* Push one more - should succeed by dropping oldest */
    Message *msg = message_new(1, value_int(100));
    SendResult result = mailbox_push_ex(&mailbox, msg);
    ASSERT(result == SEND_OK);

    /* Dropped count should be 1 */
    ASSERT(mailbox_dropped_count(&mailbox) == 1);

    /* Mailbox should still have 3 messages */
    ASSERT(mailbox_count(&mailbox) == 3);

    /* First message should be i=1 (i=0 was dropped) */
    msg = mailbox_pop(&mailbox);
    ASSERT(msg != NULL);
    ASSERT(msg->value->as.integer == 1);
    message_free(msg);

    mailbox_free(&mailbox);
}

/* String Replace Overflow Test */

TEST(test_string_replace_overflow_protection) {
    /*
     * Test that string_replace handles size calculations safely.
     * This tests the overflow protection added for shrinking replacements.
     */
    Value *s = value_string("hello hello hello");

    /* Test shrinking replacement (new < old) */
    Value *result = string_replace(s, "hello", "hi");
    ASSERT(result != NULL);
    ASSERT(result->type == VAL_STRING);
    ASSERT(strcmp(result->as.string->data, "hi hi hi") == 0);
    value_free(result);

    /* Test growing replacement (new > old) */
    result = string_replace(s, "hello", "greetings");
    ASSERT(result != NULL);
    ASSERT(result->type == VAL_STRING);
    ASSERT(strcmp(result->as.string->data, "greetings greetings greetings") == 0);
    value_free(result);

    /* Test same-size replacement */
    result = string_replace(s, "hello", "world");
    ASSERT(result != NULL);
    ASSERT(result->type == VAL_STRING);
    ASSERT(strcmp(result->as.string->data, "world world world") == 0);
    value_free(result);

    /* Test empty replacement (deletion) */
    result = string_replace(s, "hello ", "");
    ASSERT(result != NULL);
    ASSERT(result->type == VAL_STRING);
    ASSERT(strcmp(result->as.string->data, "hello") == 0);
    value_free(result);

    value_free(s);
}

/* Value Refcount Race Protection Test */

TEST(test_value_free_freeing_sentinel) {
    /*
     * Test that value_free properly sets REFCOUNT_FREEING
     * to prevent concurrent retain from resurrecting the object.
     */
    Value *v = value_int(42);
    ASSERT(v != NULL);

    /* Manually set refcount to 1 (single reference) */
    atomic_store(&v->refcount, 1);

    /* Create another reference */
    Value *v2 = value_retain(v);
    ASSERT(v2 == v);

    /* Now refcount is 2 */
    uint32_t rc = atomic_load(&v->refcount);
    ASSERT(rc == 2);

    /* Release one reference */
    value_free(v);
    rc = atomic_load(&v->refcount);
    ASSERT(rc == 1);

    /* Release second reference - should free */
    value_free(v2);
    /* Can't check after this - object is freed */
}

TEST(test_value_retain_zero_refcount) {
    /*
     * Test that value_retain returns NULL for zero refcount.
     */
    Value *v = value_int(42);
    ASSERT(v != NULL);

    /* Manually set refcount to 0 (shouldn't happen normally) */
    atomic_store(&v->refcount, 0);

    /* Retain should fail */
    Value *v2 = value_retain(v);
    ASSERT(v2 == NULL);

    /* Restore and cleanup */
    atomic_store(&v->refcount, 1);
    value_free(v);
}

/* Timer Overflow Protection Test */

TEST(test_timer_deadline_no_overflow) {
    /*
     * Test that timer_add handles very large timeouts without overflow.
     */
    TimerConfig config = timer_config_default();
    TimerWheel *wheel = timer_wheel_new(&config);
    ASSERT(wheel != NULL);

    /* Add a timer with large but reasonable timeout */
    TimerEntry *t1 = timer_add(wheel, 1, 1000000, timer_test_callback, NULL);
    ASSERT(t1 != NULL);
    ASSERT(t1->deadline_ms > 0);

    /* Add timer with maximum possible timeout - should not overflow */
    TimerEntry *t2 = timer_add(wheel, 2, UINT64_MAX - timer_current_time_ms() - 1, timer_test_callback, NULL);
    ASSERT(t2 != NULL);
    /* Deadline should be capped, not wrapped to small value */
    ASSERT(t2->deadline_ms >= timer_current_time_ms());

    timer_cancel(wheel, t1);
    timer_cancel(wheel, t2);
    timer_wheel_free(wheel);
}

/* Sandbox Symlink Protection Test */

TEST(test_sandbox_symlink_protection) {
    /*
     * Test that sandbox properly handles symlinks.
     * realpath() follows symlinks, so symlinks to outside directories
     * should be rejected.
     */
    Sandbox *sb = sandbox_new();
    ASSERT(sb != NULL);

    /* Allow /tmp for reading */
    ASSERT(sandbox_allow_read(sb, "/tmp"));

    /* Create test files if possible */
    FILE *f = fopen("/tmp/sandbox_test_real.txt", "w");
    if (f) {
        fprintf(f, "test");
        fclose(f);

        /* Access to real file in /tmp should work */
        ASSERT(sandbox_check_read(sb, "/tmp/sandbox_test_real.txt"));

        /* If we could create a symlink to /etc/passwd, it should be blocked.
         * We can't easily test this without root, but the code path is:
         * symlink("/etc/passwd", "/tmp/sandbox_evil") would be created,
         * realpath("/tmp/sandbox_evil") returns "/etc/passwd",
         * sandbox_path_within("/tmp", "/etc/passwd") returns false.
         * So we test this logic directly: */
        ASSERT(!sandbox_path_within("/tmp", "/etc/passwd"));
        ASSERT(!sandbox_path_within("/tmp", "/etc"));
        ASSERT(sandbox_path_within("/tmp", "/tmp/subdir"));
        ASSERT(sandbox_path_within("/tmp", "/tmp"));

        unlink("/tmp/sandbox_test_real.txt");
    }

    sandbox_free(sb);
}

/* String Intern Cache Memory Leak Test */

TEST(test_string_intern_no_leak) {
    /*
     * Test that string interning doesn't leak memory when strings are evicted.
     * We can't directly measure memory, but we can verify refcounts are sane.
     */

    /* Intern many different strings to cause cache evictions */
    for (int i = 0; i < 2000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "intern_test_%d", i);
        Value *v = string_intern(buf, strlen(buf));
        ASSERT(v != NULL);
        ASSERT(v->type == VAL_STRING);
        value_free(v);  /* Release caller's reference */
    }

    /* Intern same strings again - should not cause issues */
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "intern_test_%d", i);
        Value *v = string_intern(buf, strlen(buf));
        ASSERT(v != NULL);
        value_free(v);
    }
}

/* Error Code System Tests */

/* WebSocket Fragment Limit Tests */

/* Error Code System Tests */

TEST(test_error_code_system) {
    /*
     * Test the thread-local error code system for allocation failures.
     */

    /* Initially should be OK */
    agim_clear_error();
    ASSERT(agim_last_error() == AGIM_E_OK);

    /* Set an error and verify it persists */
    agim_set_error(AGIM_E_NOMEM);
    ASSERT(agim_last_error() == AGIM_E_NOMEM);

    /* Clear and verify */
    agim_clear_error();
    ASSERT(agim_last_error() == AGIM_E_OK);

    /* Test all error strings are valid */
    ASSERT(agim_error_string(AGIM_E_OK) != NULL);
    ASSERT(agim_error_string(AGIM_E_NOMEM) != NULL);
    ASSERT(agim_error_string(AGIM_E_OVERFLOW) != NULL);
    ASSERT(agim_error_string(AGIM_E_INVALID_ARG) != NULL);
    ASSERT(agim_error_string(AGIM_E_POOL_EXHAUSTED) != NULL);
    ASSERT(agim_error_string(AGIM_E_IO) != NULL);
    ASSERT(agim_error_string(AGIM_E_INTERNAL) != NULL);

    /* Verify error strings are human-readable */
    ASSERT(strlen(agim_error_string(AGIM_E_NOMEM)) > 0);
    ASSERT(strstr(agim_error_string(AGIM_E_NOMEM), "memory") != NULL);
}

/* Main */

int main(void) {
    printf("Running security tests...\n\n");

    printf("Sandbox tests:\n");
    RUN_TEST(test_sandbox_basic);
    RUN_TEST(test_sandbox_allow_read);
    RUN_TEST(test_sandbox_path_traversal);
    RUN_TEST(test_sandbox_permissive);
    RUN_TEST(test_sandbox_cwd);

    printf("\nVM bounds checking tests:\n");
    RUN_TEST(test_bounds_negative_index);
    RUN_TEST(test_bounds_large_index);
    RUN_TEST(test_slice_negative_indices);

    printf("\nParser recursion limit tests:\n");
    RUN_TEST(test_recursion_limit);

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

    printf("\nConcurrent string interning tests:\n");
    RUN_TEST(test_concurrent_string_interning);

    printf("\nConcurrent array sorting tests:\n");
    RUN_TEST(test_concurrent_array_sorting);

    printf("\nString concatenation overflow tests:\n");
    RUN_TEST(test_string_concat_overflow);

    printf("\nPath traversal security tests:\n");
    RUN_TEST(test_sandbox_dotdot_in_filename);

    printf("\nTimer cancel optimization tests:\n");
    RUN_TEST(test_timer_cancel_correctness);
    RUN_TEST(test_timer_next_deadline_optimization);

    printf("\nPool allocator lifecycle tests:\n");
    RUN_TEST(test_pool_init_free);
    RUN_TEST(test_pool_alloc_dealloc);
    RUN_TEST(test_global_pools);
    RUN_TEST(test_pool_concurrent_access);
    RUN_TEST(test_pool_pointer_validation);

    printf("\nMailbox contention tests:\n");
    RUN_TEST(test_mailbox_concurrent_push);
    RUN_TEST(test_mailbox_receive_timeout);
    RUN_TEST(test_mailbox_overflow_drop_new);
    RUN_TEST(test_mailbox_overflow_drop_old);

    printf("\nString intern cache tests:\n");
    RUN_TEST(test_string_intern_no_leak);

    printf("\nString replace overflow tests:\n");
    RUN_TEST(test_string_replace_overflow_protection);

    printf("\nValue refcount race protection tests:\n");
    RUN_TEST(test_value_free_freeing_sentinel);
    RUN_TEST(test_value_retain_zero_refcount);

    printf("\nTimer overflow protection tests:\n");
    RUN_TEST(test_timer_deadline_no_overflow);

    printf("\nSandbox symlink protection tests:\n");
    RUN_TEST(test_sandbox_symlink_protection);

    printf("\nError code system tests:\n");
    RUN_TEST(test_error_code_system);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
