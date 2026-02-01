/*
 * Agim - Worker Deque Tests
 *
 * P1.1.5.1: Tests for Chase-Lev work-stealing deque.
 * - deque_init initializes correctly
 * - deque_push adds items
 * - deque_pop removes items (LIFO for owner)
 * - deque_steal removes items (FIFO for thief)
 * - deque_empty/deque_size track state
 * - deque_grow handles capacity
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/worker.h"
#include "runtime/block.h"

#include <stdlib.h>

/*
 * Test: deque_init initializes correctly
 */
void test_deque_init(void) {
    WorkDeque deque;
    deque_init(&deque);

    ASSERT(deque_empty(&deque));
    ASSERT_EQ(0, deque_size(&deque));

    deque_free(&deque);
}

/*
 * Test: deque_push adds single item
 */
void test_deque_push_single(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    deque_push(&deque, block);

    ASSERT(!deque_empty(&deque));
    ASSERT_EQ(1, deque_size(&deque));

    /* Cleanup */
    Block *popped = deque_pop(&deque);
    ASSERT_EQ(block, popped);
    block_free(block);
    deque_free(&deque);
}

/*
 * Test: deque_push adds multiple items
 */
void test_deque_push_multiple(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *blocks[5];
    for (int i = 0; i < 5; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        ASSERT(blocks[i] != NULL);
        deque_push(&deque, blocks[i]);
    }

    ASSERT(!deque_empty(&deque));
    ASSERT_EQ(5, deque_size(&deque));

    /* Cleanup - pop in reverse order (LIFO) */
    for (int i = 4; i >= 0; i--) {
        Block *popped = deque_pop(&deque);
        ASSERT_EQ(blocks[i], popped);
        block_free(popped);
    }
    deque_free(&deque);
}

/*
 * Test: deque_pop returns NULL on empty deque
 */
void test_deque_pop_empty(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *result = deque_pop(&deque);
    ASSERT(result == NULL);

    deque_free(&deque);
}

/*
 * Test: deque_pop follows LIFO order (stack semantics for owner)
 */
void test_deque_pop_lifo(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *blocks[3];
    for (int i = 0; i < 3; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    /* Pop should return in reverse order: blocks[2], blocks[1], blocks[0] */
    ASSERT_EQ(blocks[2], deque_pop(&deque));
    ASSERT_EQ(blocks[1], deque_pop(&deque));
    ASSERT_EQ(blocks[0], deque_pop(&deque));
    ASSERT(deque_pop(&deque) == NULL);

    for (int i = 0; i < 3; i++) {
        block_free(blocks[i]);
    }
    deque_free(&deque);
}

/*
 * Test: deque_steal returns NULL on empty deque
 */
void test_deque_steal_empty(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *result = deque_steal(&deque);
    ASSERT(result == NULL);

    deque_free(&deque);
}

/*
 * Test: deque_steal follows FIFO order (queue semantics for thief)
 */
void test_deque_steal_fifo(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *blocks[3];
    for (int i = 0; i < 3; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    /* Steal should return in order: blocks[0], blocks[1], blocks[2] */
    ASSERT_EQ(blocks[0], deque_steal(&deque));
    ASSERT_EQ(blocks[1], deque_steal(&deque));
    ASSERT_EQ(blocks[2], deque_steal(&deque));
    ASSERT(deque_steal(&deque) == NULL);

    for (int i = 0; i < 3; i++) {
        block_free(blocks[i]);
    }
    deque_free(&deque);
}

/*
 * Test: deque_empty returns correct state
 */
void test_deque_empty_state(void) {
    WorkDeque deque;
    deque_init(&deque);

    ASSERT(deque_empty(&deque));

    Block *block = block_new(1, "test", NULL);
    deque_push(&deque, block);
    ASSERT(!deque_empty(&deque));

    deque_pop(&deque);
    ASSERT(deque_empty(&deque));

    block_free(block);
    deque_free(&deque);
}

/*
 * Test: deque_size returns correct count
 */
void test_deque_size_count(void) {
    WorkDeque deque;
    deque_init(&deque);

    ASSERT_EQ(0, deque_size(&deque));

    Block *blocks[5];
    for (int i = 0; i < 5; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
        ASSERT_EQ((size_t)(i + 1), deque_size(&deque));
    }

    for (int i = 4; i >= 0; i--) {
        deque_pop(&deque);
        ASSERT_EQ((size_t)i, deque_size(&deque));
    }

    for (int i = 0; i < 5; i++) {
        block_free(blocks[i]);
    }
    deque_free(&deque);
}

/*
 * Test: Deque grows when capacity exceeded
 */
void test_deque_grow(void) {
    WorkDeque deque;
    deque_init(&deque);

    /* Initial capacity is DEQUE_INITIAL_CAPACITY (64) */
    /* Push more than capacity to trigger growth */
    Block *blocks[100];
    for (int i = 0; i < 100; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    ASSERT_EQ(100, deque_size(&deque));

    /* Verify all items can be popped correctly (LIFO) */
    for (int i = 99; i >= 0; i--) {
        Block *popped = deque_pop(&deque);
        ASSERT_EQ(blocks[i], popped);
    }

    ASSERT(deque_empty(&deque));

    for (int i = 0; i < 100; i++) {
        block_free(blocks[i]);
    }
    deque_free(&deque);
}

/*
 * Test: Interleaved push and pop
 */
void test_deque_interleaved_push_pop(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *b1 = block_new(1, "test", NULL);
    Block *b2 = block_new(2, "test", NULL);
    Block *b3 = block_new(3, "test", NULL);
    Block *b4 = block_new(4, "test", NULL);

    deque_push(&deque, b1);  /* [b1] */
    deque_push(&deque, b2);  /* [b1, b2] */
    ASSERT_EQ(b2, deque_pop(&deque));  /* [b1] */

    deque_push(&deque, b3);  /* [b1, b3] */
    ASSERT_EQ(b3, deque_pop(&deque));  /* [b1] */
    ASSERT_EQ(b1, deque_pop(&deque));  /* [] */

    ASSERT(deque_empty(&deque));

    deque_push(&deque, b4);  /* [b4] */
    ASSERT_EQ(b4, deque_pop(&deque));  /* [] */

    block_free(b1);
    block_free(b2);
    block_free(b3);
    block_free(b4);
    deque_free(&deque);
}

/*
 * Test: Interleaved push and steal
 */
void test_deque_interleaved_push_steal(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *b1 = block_new(1, "test", NULL);
    Block *b2 = block_new(2, "test", NULL);
    Block *b3 = block_new(3, "test", NULL);
    Block *b4 = block_new(4, "test", NULL);

    deque_push(&deque, b1);  /* [b1] */
    deque_push(&deque, b2);  /* [b1, b2] */
    ASSERT_EQ(b1, deque_steal(&deque));  /* [b2] */

    deque_push(&deque, b3);  /* [b2, b3] */
    ASSERT_EQ(b2, deque_steal(&deque));  /* [b3] */
    ASSERT_EQ(b3, deque_steal(&deque));  /* [] */

    ASSERT(deque_empty(&deque));

    deque_push(&deque, b4);  /* [b4] */
    ASSERT_EQ(b4, deque_steal(&deque));  /* [] */

    block_free(b1);
    block_free(b2);
    block_free(b3);
    block_free(b4);
    deque_free(&deque);
}

/*
 * Test: Pop and steal compete for last item
 * Only one should succeed when there's one item
 */
void test_deque_pop_steal_single(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *block = block_new(1, "test", NULL);
    deque_push(&deque, block);

    /* Either pop or steal should get the block, not both */
    Block *popped = deque_pop(&deque);
    if (popped) {
        /* Pop won */
        ASSERT(deque_steal(&deque) == NULL);
    } else {
        /* Pop lost - this can happen if steal ran first (but here it's sequential) */
        /* In sequential execution, pop should succeed */
        ASSERT(0);  /* Pop should not fail in sequential test */
    }

    ASSERT(deque_empty(&deque));

    block_free(block);
    deque_free(&deque);
}

/*
 * Test: Push after emptying
 */
void test_deque_push_after_empty(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *b1 = block_new(1, "test", NULL);
    Block *b2 = block_new(2, "test", NULL);

    deque_push(&deque, b1);
    deque_pop(&deque);
    ASSERT(deque_empty(&deque));

    deque_push(&deque, b2);
    ASSERT(!deque_empty(&deque));
    ASSERT_EQ(1, deque_size(&deque));
    ASSERT_EQ(b2, deque_pop(&deque));

    block_free(b1);
    block_free(b2);
    deque_free(&deque);
}

/*
 * Test: Large scale push and pop
 */
void test_deque_large_scale_push_pop(void) {
    WorkDeque deque;
    deque_init(&deque);

    const int COUNT = 1000;
    Block **blocks = malloc(COUNT * sizeof(Block *));
    ASSERT(blocks != NULL);

    /* Push all */
    for (int i = 0; i < COUNT; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    ASSERT_EQ((size_t)COUNT, deque_size(&deque));

    /* Pop all - verify LIFO order */
    for (int i = COUNT - 1; i >= 0; i--) {
        Block *popped = deque_pop(&deque);
        ASSERT_EQ(blocks[i], popped);
    }

    ASSERT(deque_empty(&deque));

    for (int i = 0; i < COUNT; i++) {
        block_free(blocks[i]);
    }
    free(blocks);
    deque_free(&deque);
}

/*
 * Test: Large scale push and steal
 */
void test_deque_large_scale_push_steal(void) {
    WorkDeque deque;
    deque_init(&deque);

    const int COUNT = 1000;
    Block **blocks = malloc(COUNT * sizeof(Block *));
    ASSERT(blocks != NULL);

    /* Push all */
    for (int i = 0; i < COUNT; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    ASSERT_EQ((size_t)COUNT, deque_size(&deque));

    /* Steal all - verify FIFO order */
    for (int i = 0; i < COUNT; i++) {
        Block *stolen = deque_steal(&deque);
        ASSERT_EQ(blocks[i], stolen);
    }

    ASSERT(deque_empty(&deque));

    for (int i = 0; i < COUNT; i++) {
        block_free(blocks[i]);
    }
    free(blocks);
    deque_free(&deque);
}

/*
 * Test: Mixed pop and steal operations
 */
void test_deque_mixed_pop_steal(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *blocks[10];
    for (int i = 0; i < 10; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    /* Steal from front, pop from back */
    ASSERT_EQ(blocks[0], deque_steal(&deque));  /* Steal first */
    ASSERT_EQ(blocks[9], deque_pop(&deque));    /* Pop last */
    ASSERT_EQ(blocks[1], deque_steal(&deque));  /* Steal next */
    ASSERT_EQ(blocks[8], deque_pop(&deque));    /* Pop next from back */

    ASSERT_EQ(6, deque_size(&deque));

    /* Pop remaining */
    for (int i = 7; i >= 2; i--) {
        Block *popped = deque_pop(&deque);
        ASSERT_EQ(blocks[i], popped);
    }

    ASSERT(deque_empty(&deque));

    for (int i = 0; i < 10; i++) {
        block_free(blocks[i]);
    }
    deque_free(&deque);
}

/*
 * Test: Deque free cleans up retired buffers
 */
void test_deque_free_cleanup(void) {
    WorkDeque deque;
    deque_init(&deque);

    /* Push enough to trigger growth and create retired buffers */
    Block **blocks = malloc(200 * sizeof(Block *));
    ASSERT(blocks != NULL);

    for (int i = 0; i < 200; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    /* Pop all */
    for (int i = 199; i >= 0; i--) {
        deque_pop(&deque);
    }

    /* Free should clean up retired buffers without leaking */
    deque_free(&deque);

    for (int i = 0; i < 200; i++) {
        block_free(blocks[i]);
    }
    free(blocks);
}

/*
 * Test: Size after steal operations
 */
void test_deque_size_after_steal(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *blocks[5];
    for (int i = 0; i < 5; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    ASSERT_EQ(5, deque_size(&deque));

    deque_steal(&deque);
    ASSERT_EQ(4, deque_size(&deque));

    deque_steal(&deque);
    ASSERT_EQ(3, deque_size(&deque));

    deque_pop(&deque);
    ASSERT_EQ(2, deque_size(&deque));

    deque_steal(&deque);
    ASSERT_EQ(1, deque_size(&deque));

    deque_pop(&deque);
    ASSERT_EQ(0, deque_size(&deque));

    for (int i = 0; i < 5; i++) {
        block_free(blocks[i]);
    }
    deque_free(&deque);
}

/*
 * Test: Empty after exact number of operations
 */
void test_deque_empty_exact(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *b1 = block_new(1, "test", NULL);
    Block *b2 = block_new(2, "test", NULL);

    /* Push 2, pop 1, steal 1 */
    deque_push(&deque, b1);
    deque_push(&deque, b2);
    ASSERT(!deque_empty(&deque));

    deque_pop(&deque);  /* Removes b2 */
    ASSERT(!deque_empty(&deque));

    deque_steal(&deque);  /* Removes b1 */
    ASSERT(deque_empty(&deque));

    block_free(b1);
    block_free(b2);
    deque_free(&deque);
}

/*
 * Test: Multiple grow operations
 */
void test_deque_multiple_grows(void) {
    WorkDeque deque;
    deque_init(&deque);

    /* Initial capacity is 64, so push 256 to trigger multiple grows */
    const int COUNT = 256;
    Block **blocks = malloc(COUNT * sizeof(Block *));
    ASSERT(blocks != NULL);

    for (int i = 0; i < COUNT; i++) {
        blocks[i] = block_new((Pid)(i + 1), "test", NULL);
        deque_push(&deque, blocks[i]);
    }

    ASSERT_EQ((size_t)COUNT, deque_size(&deque));

    /* Verify integrity after multiple grows */
    for (int i = COUNT - 1; i >= 0; i--) {
        Block *popped = deque_pop(&deque);
        ASSERT_EQ(blocks[i], popped);
    }

    for (int i = 0; i < COUNT; i++) {
        block_free(blocks[i]);
    }
    free(blocks);
    deque_free(&deque);
}

/*
 * Test: Repeated push/pop cycles
 */
void test_deque_repeated_cycles(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *block = block_new(1, "test", NULL);

    for (int cycle = 0; cycle < 100; cycle++) {
        deque_push(&deque, block);
        ASSERT(!deque_empty(&deque));

        Block *popped = deque_pop(&deque);
        ASSERT_EQ(block, popped);
        ASSERT(deque_empty(&deque));
    }

    block_free(block);
    deque_free(&deque);
}

/*
 * Test: Repeated push/steal cycles
 */
void test_deque_repeated_steal_cycles(void) {
    WorkDeque deque;
    deque_init(&deque);

    Block *block = block_new(1, "test", NULL);

    for (int cycle = 0; cycle < 100; cycle++) {
        deque_push(&deque, block);
        ASSERT(!deque_empty(&deque));

        Block *stolen = deque_steal(&deque);
        ASSERT_EQ(block, stolen);
        ASSERT(deque_empty(&deque));
    }

    block_free(block);
    deque_free(&deque);
}

int main(void) {
    printf("Running worker deque tests...\n");

    printf("\nInitialization tests:\n");
    RUN_TEST(test_deque_init);

    printf("\nPush tests:\n");
    RUN_TEST(test_deque_push_single);
    RUN_TEST(test_deque_push_multiple);

    printf("\nPop tests:\n");
    RUN_TEST(test_deque_pop_empty);
    RUN_TEST(test_deque_pop_lifo);

    printf("\nSteal tests:\n");
    RUN_TEST(test_deque_steal_empty);
    RUN_TEST(test_deque_steal_fifo);

    printf("\nState tracking tests:\n");
    RUN_TEST(test_deque_empty_state);
    RUN_TEST(test_deque_size_count);

    printf("\nGrow tests:\n");
    RUN_TEST(test_deque_grow);
    RUN_TEST(test_deque_multiple_grows);

    printf("\nInterleaved operations tests:\n");
    RUN_TEST(test_deque_interleaved_push_pop);
    RUN_TEST(test_deque_interleaved_push_steal);
    RUN_TEST(test_deque_mixed_pop_steal);

    printf("\nEdge case tests:\n");
    RUN_TEST(test_deque_pop_steal_single);
    RUN_TEST(test_deque_push_after_empty);
    RUN_TEST(test_deque_empty_exact);

    printf("\nLarge scale tests:\n");
    RUN_TEST(test_deque_large_scale_push_pop);
    RUN_TEST(test_deque_large_scale_push_steal);

    printf("\nCycle tests:\n");
    RUN_TEST(test_deque_repeated_cycles);
    RUN_TEST(test_deque_repeated_steal_cycles);

    printf("\nCleanup tests:\n");
    RUN_TEST(test_deque_free_cleanup);
    RUN_TEST(test_deque_size_after_steal);

    return TEST_RESULT();
}
