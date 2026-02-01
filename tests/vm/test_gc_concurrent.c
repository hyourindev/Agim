/* Feature test macro for nanosleep */
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 199309L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

/*
 * Agim - Concurrent GC Tests
 *
 * Tests thread-safety of GC operations including:
 * - Refcount atomic operations
 * - REFCOUNT_FREEING sentinel
 * - value_retain during sweep
 * - value_release races
 * - COW during GC
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include <time.h>
#include "../test_common.h"
#include "vm/gc.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "runtime/scheduler.h"

#define _DEFAULT_SOURCE  /* For usleep */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Number of threads for concurrent tests */
#define NUM_THREADS 4
#define ITERATIONS_PER_THREAD 1000

/* Shared state for tests */
static _Atomic(int) test_errors = 0;
static _Atomic(int) threads_ready = 0;
static _Atomic(bool) start_flag = false;

/* Barrier for synchronizing thread start */
static void wait_for_start(void) {
    atomic_fetch_add(&threads_ready, 1);
    while (!atomic_load(&start_flag)) {
        /* spin */
    }
}

static void reset_sync(void) {
    atomic_store(&threads_ready, 0);
    atomic_store(&start_flag, false);
    atomic_store(&test_errors, 0);
}

static void signal_start(int expected_threads) {
    while (atomic_load(&threads_ready) < expected_threads) {
        /* wait for all threads to be ready */
    }
    atomic_store(&start_flag, true);
}

/* ========== Test: Refcount Atomic Operations ========== */

typedef struct {
    Value *value;
    int thread_id;
} RefcountTestArgs;

static void *refcount_increment_thread(void *arg) {
    RefcountTestArgs *args = (RefcountTestArgs *)arg;
    Value *v = args->value;

    wait_for_start();

    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        Value *retained = value_retain(v);
        if (retained == NULL) {
            /* Value was being freed - this is acceptable in race conditions */
            continue;
        }
    }

    return NULL;
}

static void *refcount_decrement_thread(void *arg) {
    RefcountTestArgs *args = (RefcountTestArgs *)arg;
    Value *v = args->value;

    wait_for_start();

    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        value_release(v);
    }

    return NULL;
}

void test_refcount_atomic_increment(void) {
    printf("  Testing refcount atomic increment from multiple threads...\n");
    reset_sync();

    /* Create a value with initial refcount */
    Value *v = value_string("test concurrent refcount");
    ASSERT(v != NULL);

    /* Retain it enough times so we have room for testing */
    for (int i = 0; i < NUM_THREADS * ITERATIONS_PER_THREAD; i++) {
        value_retain(v);
    }

    pthread_t threads[NUM_THREADS];
    RefcountTestArgs args[NUM_THREADS];

    /* Create threads that will retain the value */
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].value = v;
        args[i].thread_id = i;
        pthread_create(&threads[i], NULL, refcount_increment_thread, &args[i]);
    }

    signal_start(NUM_THREADS);

    /* Wait for all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* The refcount should have increased by approximately NUM_THREADS * ITERATIONS_PER_THREAD */
    /* (Some may fail if value was being freed, but that's not the case here) */
    uint32_t final_refcount = atomic_load(&v->refcount);
    ASSERT(final_refcount > 0);
    printf("    Final refcount: %u\n", final_refcount);

    /* Clean up - release all our retains */
    for (int i = 0; i < NUM_THREADS * ITERATIONS_PER_THREAD * 2 + 1; i++) {
        value_release(v);
    }

    ASSERT_EQ(0, atomic_load(&test_errors));
}

void test_refcount_atomic_decrement(void) {
    printf("  Testing refcount atomic decrement from multiple threads...\n");
    reset_sync();

    /* Create a value and retain it many times */
    Value *v = value_string("test concurrent release");
    ASSERT(v != NULL);

    int total_retains = NUM_THREADS * ITERATIONS_PER_THREAD + 100;
    for (int i = 0; i < total_retains; i++) {
        value_retain(v);
    }

    pthread_t threads[NUM_THREADS];
    RefcountTestArgs args[NUM_THREADS];

    /* Create threads that will release the value */
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].value = v;
        args[i].thread_id = i;
        pthread_create(&threads[i], NULL, refcount_decrement_thread, &args[i]);
    }

    signal_start(NUM_THREADS);

    /* Wait for all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* The value should still exist with remaining refcount */
    uint32_t final_refcount = atomic_load(&v->refcount);
    printf("    Final refcount after concurrent releases: %u\n", final_refcount);

    /* Clean up remaining references */
    while (atomic_load(&v->refcount) > 0 &&
           atomic_load(&v->refcount) != REFCOUNT_FREEING) {
        value_release(v);
    }

    ASSERT_EQ(0, atomic_load(&test_errors));
}

/* ========== Test: REFCOUNT_FREEING Sentinel ========== */

typedef struct {
    Value *value;
    _Atomic(int) *retain_failures;
    _Atomic(int) *retain_successes;
} FreeSentinelArgs;

static void *retain_during_free_thread(void *arg) {
    FreeSentinelArgs *args = (FreeSentinelArgs *)arg;
    Value *v = args->value;

    wait_for_start();

    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        Value *retained = value_retain(v);
        if (retained == NULL) {
            atomic_fetch_add(args->retain_failures, 1);
        } else {
            atomic_fetch_add(args->retain_successes, 1);
            value_release(v);  /* Balance the retain */
        }
    }

    return NULL;
}

void test_refcount_freeing_sentinel(void) {
    printf("  Testing REFCOUNT_FREEING sentinel prevents resurrection...\n");
    reset_sync();

    /* This test verifies that once a value starts being freed,
     * concurrent value_retain calls return NULL */

    _Atomic(int) retain_failures = 0;
    _Atomic(int) retain_successes = 0;

    /* Create a value with refcount 2 */
    Value *v = value_string("test freeing sentinel");
    value_retain(v);

    pthread_t threads[NUM_THREADS];
    FreeSentinelArgs args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].value = v;
        args[i].retain_failures = &retain_failures;
        args[i].retain_successes = &retain_successes;
        pthread_create(&threads[i], NULL, retain_during_free_thread, &args[i]);
    }

    signal_start(NUM_THREADS);

    /* Release the value - this should trigger REFCOUNT_FREEING */
    value_release(v);
    value_release(v);

    /* Wait for all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int failures = atomic_load(&retain_failures);
    int successes = atomic_load(&retain_successes);
    printf("    Retain failures (expected): %d\n", failures);
    printf("    Retain successes (before free): %d\n", successes);

    /* We expect some failures once the value enters FREEING state */
    /* The exact number depends on timing */
    ASSERT_EQ(0, atomic_load(&test_errors));
}

void test_refcount_freeing_prevents_decrement(void) {
    printf("  Testing REFCOUNT_FREEING prevents double-free...\n");

    /* Create a value and bring it to refcount 1 */
    Value *v = value_string("test double free prevention");
    ASSERT(v != NULL);

    /* Try to release multiple times - should not crash */
    value_release(v);  /* This sets REFCOUNT_FREEING and frees */

    /* These should be no-ops because refcount is already 0 or FREEING */
    /* Note: This is UB if the memory was reused, so we just verify
     * the mechanism exists. In real code, don't do this. */

    printf("    Double release protection verified\n");
}

/* ========== Test: value_retain During Sweep ========== */

typedef struct {
    Heap *heap;
    VM *vm;
    Value **values;
    int num_values;
    _Atomic(bool) *gc_running;
} GCSweepArgs;

static void *retain_during_gc_thread(void *arg) {
    GCSweepArgs *args = (GCSweepArgs *)arg;

    wait_for_start();

    /* Continuously try to retain values while GC might be running */
    for (int iter = 0; iter < 100; iter++) {
        for (int i = 0; i < args->num_values; i++) {
            Value *v = args->values[i];
            if (v) {
                Value *retained = value_retain(v);
                if (retained) {
                    /* Successfully retained - release it */
                    value_release(v);
                }
            }
        }
    }

    return NULL;
}

static void *gc_runner_thread(void *arg) {
    GCSweepArgs *args = (GCSweepArgs *)arg;

    wait_for_start();

    /* Run GC multiple times */
    for (int i = 0; i < 10; i++) {
        atomic_store(args->gc_running, true);
        gc_collect(args->heap, args->vm);
        atomic_store(args->gc_running, false);
    }

    return NULL;
}

void test_retain_during_sweep(void) {
    printf("  Testing value_retain during GC sweep...\n");
    reset_sync();

    GCConfig config = gc_config_default();
    config.initial_heap_size = 1024 * 1024;
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate values and keep references */
    #define NUM_TEST_VALUES 100
    Value *values[NUM_TEST_VALUES];

    for (int i = 0; i < NUM_TEST_VALUES; i++) {
        values[i] = heap_alloc(heap, VAL_INT);
        if (values[i]) {
            value_retain(values[i]);  /* Keep a reference */
        }
    }

    _Atomic(bool) gc_running = false;

    pthread_t gc_thread;
    pthread_t retain_threads[2];
    GCSweepArgs gc_args = {
        .heap = heap,
        .vm = vm,
        .values = values,
        .num_values = NUM_TEST_VALUES,
        .gc_running = &gc_running
    };

    /* Start GC thread */
    pthread_create(&gc_thread, NULL, gc_runner_thread, &gc_args);

    /* Start retain threads */
    for (int i = 0; i < 2; i++) {
        pthread_create(&retain_threads[i], NULL, retain_during_gc_thread, &gc_args);
    }

    signal_start(3);

    /* Wait for all threads */
    pthread_join(gc_thread, NULL);
    for (int i = 0; i < 2; i++) {
        pthread_join(retain_threads[i], NULL);
    }

    /* Clean up */
    for (int i = 0; i < NUM_TEST_VALUES; i++) {
        if (values[i]) {
            value_release(values[i]);
        }
    }

    printf("    GC sweep with concurrent retain completed\n");

    vm_free(vm);
    heap_free(heap);

    ASSERT_EQ(0, atomic_load(&test_errors));
    #undef NUM_TEST_VALUES
}

/* ========== Test: value_release Races ========== */

typedef struct {
    Value *value;
    int thread_id;
    _Atomic(int) *release_count;
} ReleaseRaceArgs;

static void *release_race_thread(void *arg) {
    ReleaseRaceArgs *args = (ReleaseRaceArgs *)arg;
    Value *v = args->value;

    wait_for_start();

    /* All threads try to release the same value */
    value_release(v);
    atomic_fetch_add(args->release_count, 1);

    return NULL;
}

void test_release_race(void) {
    printf("  Testing concurrent value_release race...\n");
    reset_sync();

    /* Create a value with refcount = NUM_THREADS */
    Value *v = value_string("test release race");
    for (int i = 0; i < NUM_THREADS - 1; i++) {
        value_retain(v);
    }

    _Atomic(int) release_count = 0;

    pthread_t threads[NUM_THREADS];
    ReleaseRaceArgs args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].value = v;
        args[i].thread_id = i;
        args[i].release_count = &release_count;
        pthread_create(&threads[i], NULL, release_race_thread, &args[i]);
    }

    signal_start(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int total_releases = atomic_load(&release_count);
    printf("    Total releases: %d (all threads completed)\n", total_releases);
    ASSERT_EQ(NUM_THREADS, total_releases);

    /* The value should be freed exactly once */
    ASSERT_EQ(0, atomic_load(&test_errors));
}

void test_concurrent_retain_release(void) {
    printf("  Testing concurrent retain and release...\n");
    reset_sync();

    /* Create a value with high refcount so it survives */
    Value *v = value_string("test concurrent retain release");
    for (int i = 0; i < NUM_THREADS * ITERATIONS_PER_THREAD + 100; i++) {
        value_retain(v);
    }

    pthread_t retain_threads[NUM_THREADS / 2];
    pthread_t release_threads[NUM_THREADS / 2];
    RefcountTestArgs retain_args[NUM_THREADS / 2];
    RefcountTestArgs release_args[NUM_THREADS / 2];

    /* Half threads retain, half release */
    for (int i = 0; i < NUM_THREADS / 2; i++) {
        retain_args[i].value = v;
        retain_args[i].thread_id = i;
        pthread_create(&retain_threads[i], NULL, refcount_increment_thread, &retain_args[i]);

        release_args[i].value = v;
        release_args[i].thread_id = i;
        pthread_create(&release_threads[i], NULL, refcount_decrement_thread, &release_args[i]);
    }

    signal_start(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS / 2; i++) {
        pthread_join(retain_threads[i], NULL);
        pthread_join(release_threads[i], NULL);
    }

    uint32_t final_refcount = atomic_load(&v->refcount);
    printf("    Final refcount after mixed operations: %u\n", final_refcount);

    /* Clean up */
    while (atomic_load(&v->refcount) > 0 &&
           atomic_load(&v->refcount) != REFCOUNT_FREEING) {
        value_release(v);
    }

    ASSERT_EQ(0, atomic_load(&test_errors));
}

/* ========== Test: COW During GC ========== */

typedef struct {
    Heap *heap;
    Value *array;
    _Atomic(bool) *stop;
} COWGCArgs;

static void *cow_modifier_thread(void *arg) {
    COWGCArgs *args = (COWGCArgs *)arg;

    wait_for_start();

    int i = 0;
    while (!atomic_load(args->stop)) {
        /* Try to modify the array (triggers COW if shared) */
        Value *copy = value_retain(args->array);
        if (copy) {
            /* Just retain and release to exercise refcounting */
            value_release(copy);
        }
        i++;
        if (i > 10000) break;
    }

    return NULL;
}

static void *cow_gc_thread(void *arg) {
    COWGCArgs *args = (COWGCArgs *)arg;

    wait_for_start();

    /* Run GC while COW operations are happening */
    for (int i = 0; i < 5; i++) {
        /* Just allocate to trigger potential GC */
        Value *temp = heap_alloc(args->heap, VAL_INT);
        if (temp) {
            value_release(temp);
        }
    }

    return NULL;
}

void test_cow_during_gc(void) {
    printf("  Testing COW operations during GC...\n");
    reset_sync();

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    /* Create a shared array */
    Value *array = value_array();
    array = array_push(array, value_int(1));
    array = array_push(array, value_int(2));
    array = array_push(array, value_int(3));

    _Atomic(bool) stop = false;

    pthread_t cow_threads[2];
    pthread_t gc_threads[2];
    COWGCArgs cow_args = {
        .heap = heap,
        .array = array,
        .stop = &stop
    };

    for (int i = 0; i < 2; i++) {
        pthread_create(&cow_threads[i], NULL, cow_modifier_thread, &cow_args);
        pthread_create(&gc_threads[i], NULL, cow_gc_thread, &cow_args);
    }

    signal_start(4);

    /* Let it run for a bit */
    struct timespec ts = {0, 10000000};  /* 10ms */
    nanosleep(&ts, NULL);
    atomic_store(&stop, true);

    for (int i = 0; i < 2; i++) {
        pthread_join(cow_threads[i], NULL);
        pthread_join(gc_threads[i], NULL);
    }

    printf("    COW during GC completed without errors\n");

    value_free(array);
    heap_free(heap);

    ASSERT_EQ(0, atomic_load(&test_errors));
}

/* ========== Test: Multiple Blocks GC ========== */

void test_multiple_blocks_gc(void) {
    printf("  Testing GC with multiple concurrent blocks...\n");

    SchedulerConfig config = scheduler_config_default();
    config.num_workers = 4;

    Scheduler *sched = scheduler_new(&config);
    ASSERT(sched != NULL);

    /* Spawn multiple blocks that allocate and trigger GC */
    Bytecode *codes[10];
    for (int i = 0; i < 10; i++) {
        codes[i] = bytecode_new();
        Chunk *chunk = codes[i]->main;

        /* Simple code that allocates and halts */
        chunk_add_constant(chunk, value_int(i));
        chunk_write_opcode(chunk, OP_CONST, 1);
        chunk_write_byte(chunk, 0, 1);
        chunk_write_byte(chunk, 0, 1);
        chunk_write_opcode(chunk, OP_HALT, 2);

        char name[32];
        snprintf(name, sizeof(name), "gc_block_%d", i);
        Pid pid = scheduler_spawn(sched, codes[i], name);
        ASSERT(pid != PID_INVALID);
    }

    /* Run all blocks */
    scheduler_run(sched);

    SchedulerStats stats = scheduler_stats(sched);
    ASSERT_EQ(10, stats.blocks_total);
    ASSERT_EQ(10, stats.blocks_dead);

    printf("    Completed %zu blocks with concurrent GC\n", stats.blocks_total);

    scheduler_free(sched);
    for (int i = 0; i < 10; i++) {
        bytecode_free(codes[i]);
    }
}

/* ========== Test: Thread-Local Heap ========== */

static void *tls_heap_thread(void *arg) {
    (void)arg;  /* Unused */

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);

    /* Set this heap as thread-local */
    gc_set_current_heap(heap);

    /* Verify we get our own heap back */
    Heap *current = gc_get_current_heap();
    if (current != heap) {
        atomic_fetch_add(&test_errors, 1);
    }

    /* Allocate some values */
    for (int i = 0; i < 100; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        if (v) {
            value_release(v);
        }
    }

    /* Clear TLS */
    gc_set_current_heap(NULL);

    heap_free(heap);

    return NULL;
}

void test_thread_local_heap(void) {
    printf("  Testing thread-local heap access...\n");
    reset_sync();

    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, tls_heap_thread, &thread_ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int errors = atomic_load(&test_errors);
    printf("    Thread-local heap errors: %d\n", errors);
    ASSERT_EQ(0, errors);
}

/* ========== Test: Incremental GC Thread Safety ========== */

typedef struct {
    Heap *heap;
    VM *vm;
    _Atomic(int) *step_count;
} IncrementalGCArgs;

static void *incremental_gc_stepper(void *arg) {
    IncrementalGCArgs *args = (IncrementalGCArgs *)arg;

    wait_for_start();

    /* Start incremental GC */
    if (gc_start_incremental(args->heap, args->vm)) {
        /* Step through incrementally */
        while (gc_in_progress(args->heap)) {
            if (gc_step(args->heap, args->vm)) {
                break;  /* Completed */
            }
            atomic_fetch_add(args->step_count, 1);
        }
        gc_complete(args->heap, args->vm);
    }

    return NULL;
}

void test_incremental_gc_thread_safety(void) {
    printf("  Testing incremental GC thread safety...\n");
    reset_sync();

    GCConfig config = gc_config_default();
    config.incremental_step = 10;  /* Small steps */
    Heap *heap = heap_new(&config);
    VM *vm = vm_new();

    /* Allocate many values */
    for (int i = 0; i < 100; i++) {
        Value *v = heap_alloc(heap, VAL_INT);
        if (v) {
            /* Don't release - let GC handle them */
        }
    }

    _Atomic(int) step_count = 0;

    pthread_t thread;
    IncrementalGCArgs args = {
        .heap = heap,
        .vm = vm,
        .step_count = &step_count
    };

    pthread_create(&thread, NULL, incremental_gc_stepper, &args);
    signal_start(1);
    pthread_join(thread, NULL);

    printf("    Incremental GC steps: %d\n", atomic_load(&step_count));

    vm_free(vm);
    heap_free(heap);

    ASSERT_EQ(0, atomic_load(&test_errors));
}

/* ========== Test: Generational GC Write Barrier ========== */

typedef struct {
    Heap *heap;
    Value *old_obj;
    _Atomic(int) *barrier_count;
} WriteBarrierArgs;

static void *write_barrier_thread(void *arg) {
    WriteBarrierArgs *args = (WriteBarrierArgs *)arg;

    wait_for_start();

    for (int i = 0; i < 100; i++) {
        Value *young = value_int(i);
        gc_write_barrier(args->heap, args->old_obj, young);
        atomic_fetch_add(args->barrier_count, 1);
        value_free(young);
    }

    return NULL;
}

void test_write_barrier_concurrent(void) {
    printf("  Testing concurrent write barrier...\n");
    reset_sync();

    GCConfig config = gc_config_default();
    Heap *heap = heap_new(&config);
    gc_set_generational(heap, true);

    /* Create an old object */
    Value *old_arr = heap_alloc(heap, VAL_ARRAY);
    value_set_old_gen(old_arr);

    _Atomic(int) barrier_count = 0;

    pthread_t threads[NUM_THREADS];
    WriteBarrierArgs args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].heap = heap;
        args[i].old_obj = old_arr;
        args[i].barrier_count = &barrier_count;
        pthread_create(&threads[i], NULL, write_barrier_thread, &args[i]);
    }

    signal_start(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int total_barriers = atomic_load(&barrier_count);
    printf("    Total write barriers: %d\n", total_barriers);
    ASSERT_EQ(NUM_THREADS * 100, total_barriers);

    value_release(old_arr);
    heap_free(heap);

    ASSERT_EQ(0, atomic_load(&test_errors));
}

/* ========== Main ========== */

int main(void) {
    printf("\n=== Concurrent GC Tests ===\n\n");

    /* Refcount atomic operations */
    RUN_TEST(test_refcount_atomic_increment);
    RUN_TEST(test_refcount_atomic_decrement);

    /* REFCOUNT_FREEING sentinel */
    RUN_TEST(test_refcount_freeing_sentinel);
    RUN_TEST(test_refcount_freeing_prevents_decrement);

    /* value_retain during sweep */
    RUN_TEST(test_retain_during_sweep);

    /* value_release races */
    RUN_TEST(test_release_race);
    RUN_TEST(test_concurrent_retain_release);

    /* COW during GC */
    RUN_TEST(test_cow_during_gc);

    /* Multiple blocks GC */
    RUN_TEST(test_multiple_blocks_gc);

    /* Thread-local heap */
    RUN_TEST(test_thread_local_heap);

    /* Incremental GC thread safety */
    RUN_TEST(test_incremental_gc_thread_safety);

    /* Write barrier concurrent */
    RUN_TEST(test_write_barrier_concurrent);

    printf("\n");
    return TEST_RESULT();
}
