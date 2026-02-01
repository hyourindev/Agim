/*
 * Agim - Closure Operations Tests
 *
 * Tests for closure and upvalue operations.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "types/closure.h"
#include "vm/value.h"
#include "vm/nanbox.h"
#include "util/alloc.h"
#include <string.h>

/* Helper to create a minimal function for testing */
static Function *create_test_function(const char *name) {
    Function *fn = agim_alloc(sizeof(Function));
    if (fn) {
        memset(fn, 0, sizeof(Function));
        char *name_copy = agim_alloc(strlen(name) + 1);
        if (name_copy) {
            strcpy(name_copy, name);
        }
        fn->name = name_copy;
        fn->arity = 0;
        fn->code_offset = 0;
        fn->locals_count = 0;
        fn->parent = NULL;
    }
    return fn;
}

static void free_test_function(Function *fn) {
    if (fn) {
        if (fn->name) agim_free((void *)fn->name);
        agim_free(fn);
    }
}

/* Closure Creation Tests */

void test_closure_new_basic(void) {
    Function *fn = create_test_function("test_fn");
    ASSERT(fn != NULL);

    Value *closure = value_closure(fn, 0);

    ASSERT(closure != NULL);
    ASSERT(value_is_closure(closure));
    ASSERT_EQ(0, closure_upvalue_count(closure));

    value_free(closure);
    free_test_function(fn);
}

void test_closure_new_with_upvalues(void) {
    Function *fn = create_test_function("with_upvalues");
    ASSERT(fn != NULL);

    Value *closure = value_closure(fn, 3);

    ASSERT(closure != NULL);
    ASSERT(value_is_closure(closure));
    ASSERT_EQ(3, closure_upvalue_count(closure));

    value_free(closure);
    free_test_function(fn);
}

void test_closure_function_accessor(void) {
    Function *fn = create_test_function("accessor_test");
    ASSERT(fn != NULL);

    Value *closure = value_closure(fn, 0);

    Function *retrieved = closure_function(closure);
    ASSERT(retrieved == fn);

    value_free(closure);
    free_test_function(fn);
}

void test_closure_is_closure(void) {
    Function *fn = create_test_function("is_closure_test");
    Value *closure = value_closure(fn, 0);
    Value *not_closure = value_int(42);

    ASSERT(value_is_closure(closure));
    ASSERT(!value_is_closure(not_closure));
    ASSERT(!value_is_closure(NULL));

    value_free(closure);
    value_free(not_closure);
    free_test_function(fn);
}

/* Upvalue Creation Tests */

void test_upvalue_new_open(void) {
    NanValue slot = nanbox_int(42);

    Upvalue *uv = upvalue_new(&slot);

    ASSERT(uv != NULL);
    ASSERT(upvalue_is_open(uv));

    upvalue_free(uv);
}

void test_upvalue_get_nan_open(void) {
    NanValue slot = nanbox_int(100);

    Upvalue *uv = upvalue_new(&slot);

    NanValue val = upvalue_get_nan(uv);
    ASSERT(nanbox_is_int(val));
    ASSERT_EQ(100, nanbox_to_int(val));

    upvalue_free(uv);
}

void test_upvalue_set_nan_open(void) {
    NanValue slot = nanbox_int(0);

    Upvalue *uv = upvalue_new(&slot);

    upvalue_set_nan(uv, nanbox_int(999));

    /* Should update the slot directly (open upvalue) */
    ASSERT_EQ(999, nanbox_to_int(slot));

    upvalue_free(uv);
}

/* Upvalue Close Tests */

void test_upvalue_close(void) {
    NanValue slot = nanbox_int(42);

    Upvalue *uv = upvalue_new(&slot);
    ASSERT(upvalue_is_open(uv));

    upvalue_close(uv);

    ASSERT(!upvalue_is_open(uv));

    /* Value should still be accessible */
    NanValue val = upvalue_get_nan(uv);
    ASSERT_EQ(42, nanbox_to_int(val));

    upvalue_free(uv);
}

void test_upvalue_close_preserves_value(void) {
    NanValue slot = nanbox_double(3.14);

    Upvalue *uv = upvalue_new(&slot);
    upvalue_close(uv);

    NanValue val = upvalue_get_nan(uv);
    ASSERT(nanbox_is_double(val));
    double d = nanbox_as_double(val);
    ASSERT(d > 3.13);
    ASSERT(d < 3.15);

    upvalue_free(uv);
}

void test_upvalue_close_then_set(void) {
    NanValue slot = nanbox_int(1);

    Upvalue *uv = upvalue_new(&slot);
    upvalue_close(uv);

    /* Setting after close should update internal value, not slot */
    upvalue_set_nan(uv, nanbox_int(999));

    NanValue val = upvalue_get_nan(uv);
    ASSERT_EQ(999, nanbox_to_int(val));

    /* Original slot should be unchanged */
    ASSERT_EQ(1, nanbox_to_int(slot));

    upvalue_free(uv);
}

/* Upvalue Is Open Tests */

void test_upvalue_is_open_true(void) {
    NanValue slot = NANBOX_NIL;

    Upvalue *uv = upvalue_new(&slot);

    ASSERT(upvalue_is_open(uv));

    upvalue_free(uv);
}

void test_upvalue_is_open_false_after_close(void) {
    NanValue slot = NANBOX_NIL;

    Upvalue *uv = upvalue_new(&slot);
    upvalue_close(uv);

    ASSERT(!upvalue_is_open(uv));

    upvalue_free(uv);
}

/* Closure Upvalue Access Tests */

void test_closure_set_get_upvalue(void) {
    Function *fn = create_test_function("upvalue_access");
    Value *closure = value_closure(fn, 2);

    NanValue slot1 = nanbox_int(10);
    NanValue slot2 = nanbox_int(20);
    Upvalue *uv1 = upvalue_new(&slot1);
    Upvalue *uv2 = upvalue_new(&slot2);

    closure_set_upvalue(closure, 0, uv1);
    closure_set_upvalue(closure, 1, uv2);

    Upvalue *retrieved1 = closure_get_upvalue(closure, 0);
    Upvalue *retrieved2 = closure_get_upvalue(closure, 1);

    ASSERT(retrieved1 == uv1);
    ASSERT(retrieved2 == uv2);

    value_free(closure);
    free_test_function(fn);
}

void test_closure_get_upvalue_out_of_bounds(void) {
    Function *fn = create_test_function("bounds_test");
    Value *closure = value_closure(fn, 1);

    Upvalue *uv = closure_get_upvalue(closure, 10);

    ASSERT(uv == NULL);

    value_free(closure);
    free_test_function(fn);
}

void test_closure_upvalue_count(void) {
    Function *fn0 = create_test_function("zero");
    Function *fn5 = create_test_function("five");

    Value *c0 = value_closure(fn0, 0);
    Value *c5 = value_closure(fn5, 5);

    ASSERT_EQ(0, closure_upvalue_count(c0));
    ASSERT_EQ(5, closure_upvalue_count(c5));

    value_free(c0);
    value_free(c5);
    free_test_function(fn0);
    free_test_function(fn5);
}

/* Upvalue Value API Tests */

void test_upvalue_get_value(void) {
    Value *v = value_int(42);
    NanValue slot = nanbox_obj(v);

    Upvalue *uv = upvalue_new(&slot);

    Value *retrieved = upvalue_get(uv);
    ASSERT(retrieved != NULL);
    ASSERT_EQ(VAL_INT, retrieved->type);
    ASSERT_EQ(42, retrieved->as.integer);

    upvalue_free(uv);
    value_free(v);
}

void test_upvalue_set_value(void) {
    Value *v1 = value_int(1);
    NanValue slot = nanbox_obj(v1);

    Upvalue *uv = upvalue_new(&slot);

    Value *v2 = value_string("hello");
    upvalue_set(uv, v2);

    Value *retrieved = upvalue_get(uv);
    ASSERT_EQ(VAL_STRING, retrieved->type);
    ASSERT_STR_EQ("hello", retrieved->as.string->data);

    upvalue_free(uv);
    value_free(v1);
    value_free(v2);
}

/* Upvalue Chain Tests */

void test_upvalue_next_chain(void) {
    NanValue slot1 = nanbox_int(1);
    NanValue slot2 = nanbox_int(2);
    NanValue slot3 = nanbox_int(3);

    Upvalue *uv1 = upvalue_new(&slot1);
    Upvalue *uv2 = upvalue_new(&slot2);
    Upvalue *uv3 = upvalue_new(&slot3);

    /* Chain them */
    uv1->next = uv2;
    uv2->next = uv3;
    uv3->next = NULL;

    /* Walk the chain */
    Upvalue *current = uv1;
    int count = 0;
    while (current) {
        count++;
        current = current->next;
    }

    ASSERT_EQ(3, count);

    upvalue_free(uv1);
    upvalue_free(uv2);
    upvalue_free(uv3);
}

/* Closure Free Tests */

void test_closure_free_basic(void) {
    Function *fn = create_test_function("free_test");
    Value *closure = value_closure(fn, 0);

    /* Should not crash */
    value_free(closure);
    free_test_function(fn);

    ASSERT(1); /* Reached here = success */
}

void test_closure_free_with_upvalues(void) {
    Function *fn = create_test_function("free_upvalues");
    Value *closure = value_closure(fn, 2);

    NanValue slot1 = nanbox_int(1);
    NanValue slot2 = nanbox_int(2);
    Upvalue *uv1 = upvalue_new(&slot1);
    Upvalue *uv2 = upvalue_new(&slot2);

    closure_set_upvalue(closure, 0, uv1);
    closure_set_upvalue(closure, 1, uv2);

    /* Should free closure and its upvalues */
    value_free(closure);
    free_test_function(fn);

    ASSERT(1);
}

/* Null Input Tests */

void test_closure_null_inputs(void) {
    ASSERT(!value_is_closure(NULL));
    ASSERT(closure_function(NULL) == NULL);
    ASSERT(closure_get_upvalue(NULL, 0) == NULL);
    ASSERT_EQ(0, closure_upvalue_count(NULL));

    /* closure_set_upvalue with NULL should not crash */
    closure_set_upvalue(NULL, 0, NULL);
    ASSERT(1);
}

void test_upvalue_null_inputs(void) {
    /* upvalue_is_open with NULL should return false */
    ASSERT(!upvalue_is_open(NULL));

    /* upvalue_get_nan returns NANBOX_NIL for NULL - defensive behavior */
    NanValue nil_val = upvalue_get_nan(NULL);
    ASSERT(nanbox_is_nil(nil_val));

    /* upvalue_get returns nil value for NULL - defensive behavior */
    Value *result = upvalue_get(NULL);
    ASSERT(result != NULL);
    ASSERT(value_is_nil(result));
    value_free(result);
}

/* Multiple Closures Sharing Function Tests */

void test_multiple_closures_same_function(void) {
    Function *fn = create_test_function("shared");

    Value *c1 = value_closure(fn, 1);
    Value *c2 = value_closure(fn, 1);

    /* Both should reference the same function */
    ASSERT(closure_function(c1) == closure_function(c2));

    /* But have separate upvalue arrays */
    NanValue slot1 = nanbox_int(100);
    NanValue slot2 = nanbox_int(200);
    Upvalue *uv1 = upvalue_new(&slot1);
    Upvalue *uv2 = upvalue_new(&slot2);

    closure_set_upvalue(c1, 0, uv1);
    closure_set_upvalue(c2, 0, uv2);

    ASSERT(closure_get_upvalue(c1, 0) != closure_get_upvalue(c2, 0));

    value_free(c1);
    value_free(c2);
    free_test_function(fn);
}

/* Upvalue with Different NaN-boxed Types */

void test_upvalue_with_nil(void) {
    NanValue slot = NANBOX_NIL;
    Upvalue *uv = upvalue_new(&slot);

    NanValue val = upvalue_get_nan(uv);
    ASSERT(nanbox_is_nil(val));

    upvalue_free(uv);
}

void test_upvalue_with_bool(void) {
    NanValue slot = nanbox_bool(true);
    Upvalue *uv = upvalue_new(&slot);

    NanValue val = upvalue_get_nan(uv);
    ASSERT(nanbox_is_bool(val));
    ASSERT(nanbox_as_bool(val) == true);

    upvalue_free(uv);
}

void test_upvalue_with_double(void) {
    NanValue slot = nanbox_double(-123.456);
    Upvalue *uv = upvalue_new(&slot);

    NanValue val = upvalue_get_nan(uv);
    ASSERT(nanbox_is_double(val));
    double d = nanbox_as_double(val);
    ASSERT(d < -123.4 && d > -123.5);

    upvalue_free(uv);
}

void test_upvalue_with_object(void) {
    Value *obj = value_string("test object");
    NanValue slot = nanbox_obj(obj);
    Upvalue *uv = upvalue_new(&slot);

    NanValue val = upvalue_get_nan(uv);
    ASSERT(nanbox_is_obj(val));

    Value *retrieved = (Value *)nanbox_as_obj(val);
    ASSERT_EQ(VAL_STRING, retrieved->type);
    ASSERT_STR_EQ("test object", retrieved->as.string->data);

    upvalue_free(uv);
    value_free(obj);
}

/* Main */

int main(void) {
    printf("Running closure operations tests...\n\n");

    printf("Closure Creation Tests:\n");
    RUN_TEST(test_closure_new_basic);
    RUN_TEST(test_closure_new_with_upvalues);
    RUN_TEST(test_closure_function_accessor);
    RUN_TEST(test_closure_is_closure);

    printf("\nUpvalue Creation Tests:\n");
    RUN_TEST(test_upvalue_new_open);
    RUN_TEST(test_upvalue_get_nan_open);
    RUN_TEST(test_upvalue_set_nan_open);

    printf("\nUpvalue Close Tests:\n");
    RUN_TEST(test_upvalue_close);
    RUN_TEST(test_upvalue_close_preserves_value);
    RUN_TEST(test_upvalue_close_then_set);

    printf("\nUpvalue Is Open Tests:\n");
    RUN_TEST(test_upvalue_is_open_true);
    RUN_TEST(test_upvalue_is_open_false_after_close);

    printf("\nClosure Upvalue Access Tests:\n");
    RUN_TEST(test_closure_set_get_upvalue);
    RUN_TEST(test_closure_get_upvalue_out_of_bounds);
    RUN_TEST(test_closure_upvalue_count);

    printf("\nUpvalue Value API Tests:\n");
    RUN_TEST(test_upvalue_get_value);
    RUN_TEST(test_upvalue_set_value);

    printf("\nUpvalue Chain Tests:\n");
    RUN_TEST(test_upvalue_next_chain);

    printf("\nClosure Free Tests:\n");
    RUN_TEST(test_closure_free_basic);
    RUN_TEST(test_closure_free_with_upvalues);

    printf("\nNull Input Tests:\n");
    RUN_TEST(test_closure_null_inputs);
    RUN_TEST(test_upvalue_null_inputs);

    printf("\nMultiple Closures Tests:\n");
    RUN_TEST(test_multiple_closures_same_function);

    printf("\nUpvalue with Different Types:\n");
    RUN_TEST(test_upvalue_with_nil);
    RUN_TEST(test_upvalue_with_bool);
    RUN_TEST(test_upvalue_with_double);
    RUN_TEST(test_upvalue_with_object);

    return TEST_RESULT();
}
