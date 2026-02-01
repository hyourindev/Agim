/*
 * Agim - Block Linking Tests
 *
 * P1.1.4.3: Tests for block linking operations.
 * - block_link adds link
 * - block_link idempotent
 * - block_unlink removes link
 * - block_unlink missing no-op
 * - block_get_links returns links
 * - link array growth
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "runtime/block.h"

/*
 * Test: Block starts with no links
 */
void test_linking_initially_empty(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    size_t count;
    const Pid *links = block_get_links(block, &count);

    ASSERT_EQ(0, count);
    ASSERT(links == NULL);

    block_free(block);
}

/*
 * Test: block_link adds single link
 */
void test_linking_add_single(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    bool linked = block_link(block, 2);
    ASSERT(linked);

    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(1, count);
    ASSERT_EQ(2, links[0]);

    block_free(block);
}

/*
 * Test: block_link adds multiple links
 */
void test_linking_add_multiple(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 3);
    block_link(block, 4);

    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(3, count);

    /* Verify all links are present (order may vary) */
    int found_2 = 0, found_3 = 0, found_4 = 0;
    for (size_t i = 0; i < count; i++) {
        if (links[i] == 2) found_2 = 1;
        if (links[i] == 3) found_3 = 1;
        if (links[i] == 4) found_4 = 1;
    }
    ASSERT(found_2);
    ASSERT(found_3);
    ASSERT(found_4);

    block_free(block);
}

/*
 * Test: block_link is idempotent
 */
void test_linking_idempotent(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 2);  /* Link again */
    block_link(block, 2);  /* And again */

    size_t count;
    block_get_links(block, &count);
    ASSERT_EQ(1, count);  /* Should only have one link */

    block_free(block);
}

/*
 * Test: block_link with NULL block fails
 */
void test_linking_null_block(void) {
    bool linked = block_link(NULL, 2);
    ASSERT(!linked);
}

/*
 * Test: block_link with PID_INVALID fails
 */
void test_linking_invalid_pid(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    bool linked = block_link(block, PID_INVALID);
    ASSERT(!linked);

    size_t count;
    block_get_links(block, &count);
    ASSERT_EQ(0, count);

    block_free(block);
}

/*
 * Test: block_unlink removes link
 */
void test_unlink_removes(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 3);
    block_link(block, 4);

    block_unlink(block, 3);

    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(2, count);

    /* Verify 3 is not present */
    for (size_t i = 0; i < count; i++) {
        ASSERT(links[i] != 3);
    }

    block_free(block);
}

/*
 * Test: block_unlink first element
 */
void test_unlink_first(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 3);
    block_link(block, 4);

    block_unlink(block, 2);

    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(2, count);

    /* Verify 2 is not present */
    for (size_t i = 0; i < count; i++) {
        ASSERT(links[i] != 2);
    }

    block_free(block);
}

/*
 * Test: block_unlink last element
 */
void test_unlink_last(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 3);
    block_link(block, 4);

    block_unlink(block, 4);

    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(2, count);

    /* Verify 4 is not present */
    for (size_t i = 0; i < count; i++) {
        ASSERT(links[i] != 4);
    }

    block_free(block);
}

/*
 * Test: block_unlink non-existent link is no-op
 */
void test_unlink_nonexistent(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 3);

    block_unlink(block, 99);  /* Non-existent */

    size_t count;
    block_get_links(block, &count);
    ASSERT_EQ(2, count);

    block_free(block);
}

/*
 * Test: block_unlink with NULL block is safe
 */
void test_unlink_null_block(void) {
    block_unlink(NULL, 2);  /* Should not crash */
    ASSERT(1);
}

/*
 * Test: block_unlink all links
 */
void test_unlink_all(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 3);
    block_link(block, 4);

    block_unlink(block, 2);
    block_unlink(block, 3);
    block_unlink(block, 4);

    size_t count;
    block_get_links(block, &count);
    ASSERT_EQ(0, count);

    block_free(block);
}

/*
 * Test: block_get_links with NULL block
 */
void test_get_links_null_block(void) {
    size_t count = 999;
    const Pid *links = block_get_links(NULL, &count);

    ASSERT(links == NULL);
    ASSERT_EQ(0, count);
}

/*
 * Test: block_get_links with NULL count pointer
 */
void test_get_links_null_count(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);

    /* Should not crash even with NULL count */
    const Pid *links = block_get_links(block, NULL);
    (void)links;
    ASSERT(1);

    block_free(block);
}

/*
 * Test: Link array grows automatically
 */
void test_linking_array_growth(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* Link many PIDs to trigger growth */
    for (Pid p = 2; p <= 20; p++) {
        bool linked = block_link(block, p);
        ASSERT(linked);
    }

    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(19, count);  /* 2 through 20 */

    /* Verify all links present */
    for (Pid p = 2; p <= 20; p++) {
        bool found = false;
        for (size_t i = 0; i < count; i++) {
            if (links[i] == p) {
                found = true;
                break;
            }
        }
        ASSERT(found);
    }

    block_free(block);
}

/*
 * Test: Link and unlink interleaved
 */
void test_linking_interleaved(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 3);
    block_unlink(block, 2);
    block_link(block, 4);
    block_unlink(block, 3);
    block_link(block, 5);

    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(2, count);

    /* Should have 4 and 5 */
    int found_4 = 0, found_5 = 0;
    for (size_t i = 0; i < count; i++) {
        if (links[i] == 4) found_4 = 1;
        if (links[i] == 5) found_5 = 1;
    }
    ASSERT(found_4);
    ASSERT(found_5);

    block_free(block);
}

/*
 * Test: Link count accuracy
 */
void test_linking_count_accuracy(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(0, block->link_count);

    block_link(block, 2);
    ASSERT_EQ(1, block->link_count);

    block_link(block, 3);
    ASSERT_EQ(2, block->link_count);

    block_link(block, 3);  /* Duplicate - should not increase */
    ASSERT_EQ(2, block->link_count);

    block_unlink(block, 2);
    ASSERT_EQ(1, block->link_count);

    block_unlink(block, 99);  /* Non-existent */
    ASSERT_EQ(1, block->link_count);

    block_unlink(block, 3);
    ASSERT_EQ(0, block->link_count);

    block_free(block);
}

/*
 * Test: Link capacity grows
 */
void test_linking_capacity_growth(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    ASSERT_EQ(0, block->link_capacity);

    block_link(block, 2);
    ASSERT(block->link_capacity >= 1);

    /* Add more links to trigger growth */
    for (Pid p = 3; p <= 10; p++) {
        block_link(block, p);
    }
    ASSERT(block->link_capacity >= 9);

    block_free(block);
}

/*
 * Test: Self-link (block linking to itself)
 */
void test_linking_self(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* Self-linking is technically allowed at the block level */
    bool linked = block_link(block, 1);
    ASSERT(linked);

    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(1, count);
    ASSERT_EQ(1, links[0]);

    block_free(block);
}

/*
 * Test: Links preserved after unlink operations
 */
void test_linking_preserved_after_unlink(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    block_link(block, 2);
    block_link(block, 3);
    block_link(block, 4);
    block_link(block, 5);

    /* Remove middle elements */
    block_unlink(block, 3);
    block_unlink(block, 4);

    /* Remaining links should still be valid */
    size_t count;
    const Pid *links = block_get_links(block, &count);
    ASSERT_EQ(2, count);

    /* Verify 2 and 5 are present */
    int found_2 = 0, found_5 = 0;
    for (size_t i = 0; i < count; i++) {
        if (links[i] == 2) found_2 = 1;
        if (links[i] == 5) found_5 = 1;
        ASSERT(links[i] != 3);
        ASSERT(links[i] != 4);
    }
    ASSERT(found_2);
    ASSERT(found_5);

    block_free(block);
}

/*
 * Test: Large number of links
 */
void test_linking_many(void) {
    Block *block = block_new(1, "test", NULL);
    ASSERT(block != NULL);

    /* Add 100 links */
    for (Pid p = 2; p <= 101; p++) {
        bool linked = block_link(block, p);
        ASSERT(linked);
    }

    size_t count;
    block_get_links(block, &count);
    ASSERT_EQ(100, count);

    block_free(block);
}

int main(void) {
    printf("Running block linking tests...\n");

    printf("\nInitial state tests:\n");
    RUN_TEST(test_linking_initially_empty);

    printf("\nblock_link tests:\n");
    RUN_TEST(test_linking_add_single);
    RUN_TEST(test_linking_add_multiple);
    RUN_TEST(test_linking_idempotent);
    RUN_TEST(test_linking_null_block);
    RUN_TEST(test_linking_invalid_pid);

    printf("\nblock_unlink tests:\n");
    RUN_TEST(test_unlink_removes);
    RUN_TEST(test_unlink_first);
    RUN_TEST(test_unlink_last);
    RUN_TEST(test_unlink_nonexistent);
    RUN_TEST(test_unlink_null_block);
    RUN_TEST(test_unlink_all);

    printf("\nblock_get_links tests:\n");
    RUN_TEST(test_get_links_null_block);
    RUN_TEST(test_get_links_null_count);

    printf("\nArray growth tests:\n");
    RUN_TEST(test_linking_array_growth);
    RUN_TEST(test_linking_capacity_growth);

    printf("\nMixed operations tests:\n");
    RUN_TEST(test_linking_interleaved);
    RUN_TEST(test_linking_count_accuracy);
    RUN_TEST(test_linking_preserved_after_unlink);
    RUN_TEST(test_linking_self);
    RUN_TEST(test_linking_many);

    return TEST_RESULT();
}
