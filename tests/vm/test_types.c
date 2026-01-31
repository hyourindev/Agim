/*
 * Agim - Type System Tests
 *
 * Comprehensive tests for Option, Result, Struct, and Enum types.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "vm/value.h"
#include <string.h>

/*============================================================================
 * Option Type Tests
 *============================================================================*/

void test_option_some_basic(void) {
    Value *inner = value_int(42);
    Value *opt = value_some(inner);

    ASSERT(opt != NULL);
    ASSERT_EQ(VAL_OPTION, opt->type);
    ASSERT(value_is_option(opt));
    ASSERT(value_option_is_some(opt));
    ASSERT(!value_option_is_none(opt));

    value_free(opt);
}

void test_option_none_basic(void) {
    Value *opt = value_none();

    ASSERT(opt != NULL);
    ASSERT_EQ(VAL_OPTION, opt->type);
    ASSERT(value_is_option(opt));
    ASSERT(value_option_is_none(opt));
    ASSERT(!value_option_is_some(opt));

    value_free(opt);
}

void test_option_is_some(void) {
    Value *some_val = value_some(value_string("hello"));
    Value *none_val = value_none();
    Value *not_option = value_int(42);

    ASSERT(value_option_is_some(some_val));
    ASSERT(!value_option_is_some(none_val));
    ASSERT(!value_option_is_some(not_option));
    ASSERT(!value_option_is_some(NULL));

    value_free(some_val);
    value_free(none_val);
    value_free(not_option);
}

void test_option_is_none(void) {
    Value *some_val = value_some(value_int(100));
    Value *none_val = value_none();
    Value *not_option = value_string("test");

    ASSERT(!value_option_is_none(some_val));
    ASSERT(value_option_is_none(none_val));
    ASSERT(!value_option_is_none(not_option));
    ASSERT(!value_option_is_none(NULL));

    value_free(some_val);
    value_free(none_val);
    value_free(not_option);
}

void test_option_unwrap_some(void) {
    Value *inner = value_int(99);
    Value *opt = value_some(inner);

    Value *unwrapped = value_option_unwrap(opt);
    ASSERT(unwrapped != NULL);
    ASSERT_EQ(VAL_INT, unwrapped->type);
    ASSERT_EQ(99, unwrapped->as.integer);

    value_free(opt);
}

void test_option_unwrap_none(void) {
    Value *opt = value_none();

    Value *unwrapped = value_option_unwrap(opt);
    ASSERT(unwrapped == NULL);

    value_free(opt);
}

void test_option_unwrap_or_some(void) {
    Value *inner = value_int(42);
    Value *opt = value_some(inner);
    Value *default_val = value_int(0);

    Value *result = value_option_unwrap_or(opt, default_val);
    ASSERT(result != NULL);
    ASSERT_EQ(42, result->as.integer);

    value_free(opt);
    value_free(default_val);
}

void test_option_unwrap_or_none(void) {
    Value *opt = value_none();
    Value *default_val = value_int(999);

    Value *result = value_option_unwrap_or(opt, default_val);
    ASSERT(result != NULL);
    ASSERT_EQ(999, result->as.integer);

    value_free(opt);
    value_free(default_val);
}

void test_option_null_input(void) {
    /* All functions should handle NULL gracefully */
    ASSERT(!value_option_is_some(NULL));
    ASSERT(!value_option_is_none(NULL));
    ASSERT(value_option_unwrap(NULL) == NULL);

    Value *default_val = value_int(1);
    Value *result = value_option_unwrap_or(NULL, default_val);
    ASSERT_EQ(1, result->as.integer);
    value_free(default_val);
}

void test_option_non_option_type(void) {
    /* Non-Option types should return appropriate values */
    Value *not_opt = value_string("not an option");

    ASSERT(!value_option_is_some(not_opt));
    ASSERT(!value_option_is_none(not_opt));
    ASSERT(value_option_unwrap(not_opt) == NULL);

    value_free(not_opt);
}

void test_option_some_wrapping_nil(void) {
    /* Some can wrap nil value */
    Value *inner = value_nil();
    Value *opt = value_some(inner);

    ASSERT(value_option_is_some(opt));
    Value *unwrapped = value_option_unwrap(opt);
    ASSERT(unwrapped != NULL);
    ASSERT(value_is_nil(unwrapped));

    value_free(opt);
}

/*============================================================================
 * Result Type Tests
 *============================================================================*/

void test_result_ok_basic(void) {
    Value *inner = value_int(42);
    Value *result = value_result_ok(inner);

    ASSERT(result != NULL);
    ASSERT_EQ(VAL_RESULT, result->type);
    ASSERT(value_is_result(result));
    ASSERT(value_result_is_ok(result));
    ASSERT(!value_result_is_err(result));

    value_free(result);
}

void test_result_err_basic(void) {
    Value *error = value_string("something went wrong");
    Value *result = value_result_err(error);

    ASSERT(result != NULL);
    ASSERT_EQ(VAL_RESULT, result->type);
    ASSERT(value_is_result(result));
    ASSERT(!value_result_is_ok(result));
    ASSERT(value_result_is_err(result));

    value_free(result);
}

void test_result_is_ok(void) {
    Value *ok_result = value_result_ok(value_int(1));
    Value *err_result = value_result_err(value_string("error"));
    Value *not_result = value_float(3.14);

    ASSERT(value_result_is_ok(ok_result));
    ASSERT(!value_result_is_ok(err_result));
    ASSERT(!value_result_is_ok(not_result));
    ASSERT(!value_result_is_ok(NULL));

    value_free(ok_result);
    value_free(err_result);
    value_free(not_result);
}

void test_result_is_err(void) {
    Value *ok_result = value_result_ok(value_bool(true));
    Value *err_result = value_result_err(value_string("failed"));
    Value *not_result = value_array();

    ASSERT(!value_result_is_err(ok_result));
    ASSERT(value_result_is_err(err_result));
    ASSERT(!value_result_is_err(not_result));
    ASSERT(!value_result_is_err(NULL));

    value_free(ok_result);
    value_free(err_result);
    value_free(not_result);
}

void test_result_unwrap_ok(void) {
    Value *inner = value_int(123);
    Value *result = value_result_ok(inner);

    Value *unwrapped = value_result_unwrap(result);
    ASSERT(unwrapped != NULL);
    ASSERT_EQ(VAL_INT, unwrapped->type);
    ASSERT_EQ(123, unwrapped->as.integer);

    value_free(result);
}

void test_result_unwrap_err(void) {
    Value *error = value_string("error message");
    Value *result = value_result_err(error);

    Value *unwrapped = value_result_unwrap(result);
    ASSERT(unwrapped == NULL);

    value_free(result);
}

void test_result_unwrap_or_ok(void) {
    Value *ok_result = value_result_ok(value_int(50));
    Value *default_val = value_int(0);

    Value *unwrapped = value_result_unwrap_or(ok_result, default_val);
    ASSERT(unwrapped != NULL);
    ASSERT_EQ(50, unwrapped->as.integer);

    value_free(ok_result);
    value_free(default_val);
}

void test_result_unwrap_or_err(void) {
    Value *err_result = value_result_err(value_string("failed"));
    Value *default_val = value_int(100);

    Value *unwrapped = value_result_unwrap_or(err_result, default_val);
    ASSERT(unwrapped != NULL);
    ASSERT_EQ(100, unwrapped->as.integer);

    value_free(err_result);
    value_free(default_val);
}

void test_result_unwrap_err_function(void) {
    Value *err_result = value_result_err(value_string("error details"));
    Value *ok_result = value_result_ok(value_int(42));

    Value *err_val = value_result_unwrap_err(err_result);
    ASSERT(err_val != NULL);
    ASSERT_EQ(VAL_STRING, err_val->type);
    ASSERT_STR_EQ("error details", err_val->as.string->data);

    Value *no_err = value_result_unwrap_err(ok_result);
    ASSERT(no_err == NULL);

    value_free(err_result);
    value_free(ok_result);
}

void test_result_null_input(void) {
    ASSERT(!value_result_is_ok(NULL));
    ASSERT(!value_result_is_err(NULL));
    ASSERT(value_result_unwrap(NULL) == NULL);
    ASSERT(value_result_unwrap_err(NULL) == NULL);

    Value *default_val = value_int(5);
    Value *result = value_result_unwrap_or(NULL, default_val);
    ASSERT_EQ(5, result->as.integer);
    value_free(default_val);
}

void test_result_non_result_type(void) {
    Value *not_result = value_map();

    ASSERT(!value_result_is_ok(not_result));
    ASSERT(!value_result_is_err(not_result));
    ASSERT(value_result_unwrap(not_result) == NULL);
    ASSERT(value_result_unwrap_err(not_result) == NULL);

    value_free(not_result);
}

void test_result_ok_wrapping_nil(void) {
    Value *inner = value_nil();
    Value *result = value_result_ok(inner);

    ASSERT(value_result_is_ok(result));
    Value *unwrapped = value_result_unwrap(result);
    ASSERT(unwrapped != NULL);
    ASSERT(value_is_nil(unwrapped));

    value_free(result);
}

/*============================================================================
 * Struct Type Tests
 *============================================================================*/

void test_struct_new_basic(void) {
    Value *s = value_struct_new("Point", 2);

    ASSERT(s != NULL);
    ASSERT_EQ(VAL_STRUCT, s->type);
    ASSERT(value_is_struct(s));
    ASSERT_STR_EQ("Point", value_struct_type_name(s));

    value_free(s);
}

void test_struct_set_field(void) {
    Value *s = value_struct_new("Point", 2);

    value_struct_set_field(s, 0, "x", value_int(10));
    value_struct_set_field(s, 1, "y", value_int(20));

    Value *x = value_struct_get_field(s, "x");
    Value *y = value_struct_get_field(s, "y");

    ASSERT(x != NULL);
    ASSERT(y != NULL);
    ASSERT_EQ(10, x->as.integer);
    ASSERT_EQ(20, y->as.integer);

    value_free(s);
}

void test_struct_get_field_by_name(void) {
    Value *s = value_struct_new("User", 3);
    value_struct_set_field(s, 0, "name", value_string("Alice"));
    value_struct_set_field(s, 1, "age", value_int(30));
    value_struct_set_field(s, 2, "active", value_bool(true));

    Value *name = value_struct_get_field(s, "name");
    Value *age = value_struct_get_field(s, "age");
    Value *active = value_struct_get_field(s, "active");

    ASSERT_STR_EQ("Alice", name->as.string->data);
    ASSERT_EQ(30, age->as.integer);
    ASSERT(active->as.boolean);

    value_free(s);
}

void test_struct_get_field_by_index(void) {
    Value *s = value_struct_new("Vec2", 2);
    value_struct_set_field(s, 0, "x", value_float(1.5));
    value_struct_set_field(s, 1, "y", value_float(2.5));

    Value *x = value_struct_get_field_index(s, 0);
    Value *y = value_struct_get_field_index(s, 1);

    ASSERT(x != NULL);
    ASSERT(y != NULL);
    ASSERT(x->as.floating > 1.4 && x->as.floating < 1.6);
    ASSERT(y->as.floating > 2.4 && y->as.floating < 2.6);

    value_free(s);
}

void test_struct_type_name(void) {
    Value *s1 = value_struct_new("Rectangle", 4);
    Value *s2 = value_struct_new("Circle", 2);

    ASSERT_STR_EQ("Rectangle", value_struct_type_name(s1));
    ASSERT_STR_EQ("Circle", value_struct_type_name(s2));

    /* Non-struct returns NULL */
    Value *not_struct = value_int(42);
    ASSERT(value_struct_type_name(not_struct) == NULL);
    ASSERT(value_struct_type_name(NULL) == NULL);

    value_free(s1);
    value_free(s2);
    value_free(not_struct);
}

void test_struct_multiple_fields(void) {
    Value *s = value_struct_new("Config", 5);
    value_struct_set_field(s, 0, "host", value_string("localhost"));
    value_struct_set_field(s, 1, "port", value_int(8080));
    value_struct_set_field(s, 2, "secure", value_bool(true));
    value_struct_set_field(s, 3, "timeout", value_float(30.0));
    value_struct_set_field(s, 4, "data", value_nil());

    ASSERT_STR_EQ("localhost", value_struct_get_field(s, "host")->as.string->data);
    ASSERT_EQ(8080, value_struct_get_field(s, "port")->as.integer);
    ASSERT(value_struct_get_field(s, "secure")->as.boolean);
    ASSERT(value_is_nil(value_struct_get_field(s, "data")));

    value_free(s);
}

void test_struct_field_overwrite(void) {
    Value *s = value_struct_new("Counter", 1);
    value_struct_set_field(s, 0, "count", value_int(0));

    ASSERT_EQ(0, value_struct_get_field(s, "count")->as.integer);

    /* Overwrite the field */
    value_struct_set_field(s, 0, "count", value_int(100));
    ASSERT_EQ(100, value_struct_get_field(s, "count")->as.integer);

    value_free(s);
}

void test_struct_field_not_found(void) {
    Value *s = value_struct_new("Point", 2);
    value_struct_set_field(s, 0, "x", value_int(1));
    value_struct_set_field(s, 1, "y", value_int(2));

    Value *z = value_struct_get_field(s, "z");
    ASSERT(z == NULL);

    value_free(s);
}

void test_struct_null_inputs(void) {
    /* NULL value */
    ASSERT(value_struct_type_name(NULL) == NULL);
    ASSERT(value_struct_get_field(NULL, "x") == NULL);
    ASSERT(value_struct_get_field_index(NULL, 0) == NULL);

    /* Note: value_struct_get_field(s, NULL) would crash - implementation doesn't handle NULL field_name */
}

void test_struct_empty(void) {
    /* Zero fields */
    Value *s = value_struct_new("Empty", 0);

    ASSERT(s != NULL);
    ASSERT_STR_EQ("Empty", value_struct_type_name(s));
    ASSERT(value_struct_get_field(s, "anything") == NULL);
    ASSERT(value_struct_get_field_index(s, 0) == NULL);

    value_free(s);
}

void test_struct_index_out_of_bounds(void) {
    Value *s = value_struct_new("Small", 2);
    value_struct_set_field(s, 0, "a", value_int(1));
    value_struct_set_field(s, 1, "b", value_int(2));

    ASSERT(value_struct_get_field_index(s, 0) != NULL);
    ASSERT(value_struct_get_field_index(s, 1) != NULL);
    ASSERT(value_struct_get_field_index(s, 2) == NULL);
    ASSERT(value_struct_get_field_index(s, 100) == NULL);

    value_free(s);
}

/*============================================================================
 * Enum Type Tests
 *============================================================================*/

void test_enum_unit_basic(void) {
    Value *e = value_enum_unit("Color", "Red");

    ASSERT(e != NULL);
    ASSERT_EQ(VAL_ENUM, e->type);
    ASSERT(value_is_enum(e));
    ASSERT_STR_EQ("Color", value_enum_type_name(e));
    ASSERT_STR_EQ("Red", value_enum_variant_name(e));
    ASSERT(value_enum_payload(e) == NULL);

    value_free(e);
}

void test_enum_with_payload_basic(void) {
    Value *payload = value_string("file not found");
    Value *e = value_enum_with_payload("Result", "Error", payload);

    ASSERT(e != NULL);
    ASSERT_EQ(VAL_ENUM, e->type);
    ASSERT(value_is_enum(e));
    ASSERT_STR_EQ("Result", value_enum_type_name(e));
    ASSERT_STR_EQ("Error", value_enum_variant_name(e));

    Value *p = value_enum_payload(e);
    ASSERT(p != NULL);
    ASSERT_STR_EQ("file not found", p->as.string->data);

    value_free(e);
}

void test_enum_type_name(void) {
    Value *e1 = value_enum_unit("Status", "Ok");
    Value *e2 = value_enum_with_payload("Option", "Some", value_int(42));

    ASSERT_STR_EQ("Status", value_enum_type_name(e1));
    ASSERT_STR_EQ("Option", value_enum_type_name(e2));

    Value *not_enum = value_bool(true);
    ASSERT(value_enum_type_name(not_enum) == NULL);
    ASSERT(value_enum_type_name(NULL) == NULL);

    value_free(e1);
    value_free(e2);
    value_free(not_enum);
}

void test_enum_variant_name(void) {
    Value *e1 = value_enum_unit("Direction", "North");
    Value *e2 = value_enum_unit("Direction", "South");
    Value *e3 = value_enum_with_payload("Message", "Text", value_string("hello"));

    ASSERT_STR_EQ("North", value_enum_variant_name(e1));
    ASSERT_STR_EQ("South", value_enum_variant_name(e2));
    ASSERT_STR_EQ("Text", value_enum_variant_name(e3));

    ASSERT(value_enum_variant_name(NULL) == NULL);

    value_free(e1);
    value_free(e2);
    value_free(e3);
}

void test_enum_payload(void) {
    Value *unit = value_enum_unit("Status", "Pending");
    Value *with_data = value_enum_with_payload("Event", "Click", value_int(100));

    ASSERT(value_enum_payload(unit) == NULL);

    Value *p = value_enum_payload(with_data);
    ASSERT(p != NULL);
    ASSERT_EQ(100, p->as.integer);

    ASSERT(value_enum_payload(NULL) == NULL);

    value_free(unit);
    value_free(with_data);
}

void test_enum_is_variant(void) {
    Value *e = value_enum_unit("Color", "Blue");

    ASSERT(value_enum_is_variant(e, "Blue"));
    ASSERT(!value_enum_is_variant(e, "Red"));
    ASSERT(!value_enum_is_variant(e, "Green"));
    ASSERT(!value_enum_is_variant(e, "blue")); /* Case sensitive */

    value_free(e);
}

void test_enum_is_variant_wrong(void) {
    Value *e = value_enum_with_payload("Option", "Some", value_int(1));

    ASSERT(value_enum_is_variant(e, "Some"));
    ASSERT(!value_enum_is_variant(e, "None"));
    ASSERT(!value_enum_is_variant(e, ""));

    value_free(e);
}

void test_enum_unit_no_payload(void) {
    Value *e = value_enum_unit("Boolean", "True");

    /* Unit variants should always have NULL payload */
    ASSERT(value_enum_payload(e) == NULL);
    ASSERT(value_enum_is_variant(e, "True"));

    value_free(e);
}

void test_enum_null_inputs(void) {
    ASSERT(value_enum_type_name(NULL) == NULL);
    ASSERT(value_enum_variant_name(NULL) == NULL);
    ASSERT(value_enum_payload(NULL) == NULL);
    ASSERT(!value_enum_is_variant(NULL, "Test"));

    /* Note: value_enum_is_variant(e, NULL) would crash - implementation doesn't handle NULL variant_name */
}

void test_enum_non_enum_type(void) {
    Value *not_enum = value_string("not an enum");

    ASSERT(value_enum_type_name(not_enum) == NULL);
    ASSERT(value_enum_variant_name(not_enum) == NULL);
    ASSERT(value_enum_payload(not_enum) == NULL);
    ASSERT(!value_enum_is_variant(not_enum, "Test"));

    value_free(not_enum);
}

void test_enum_payload_with_nil(void) {
    /* Payload can be nil */
    Value *e = value_enum_with_payload("Maybe", "Just", value_nil());

    ASSERT(value_enum_is_variant(e, "Just"));
    Value *p = value_enum_payload(e);
    ASSERT(p != NULL);
    ASSERT(value_is_nil(p));

    value_free(e);
}

/*============================================================================
 * Memory Management Tests
 *============================================================================*/

void test_value_free_option(void) {
    /* Test that freeing Option doesn't leak */
    Value *opt = value_some(value_string("test string"));
    value_free(opt);
    /* No crash = success */
    ASSERT(1);
}

void test_value_free_result(void) {
    Value *ok = value_result_ok(value_int(42));
    Value *err = value_result_err(value_string("error"));

    value_free(ok);
    value_free(err);
    ASSERT(1);
}

void test_value_free_struct(void) {
    Value *s = value_struct_new("Complex", 3);
    value_struct_set_field(s, 0, "a", value_string("hello"));
    value_struct_set_field(s, 1, "b", value_array());
    value_struct_set_field(s, 2, "c", value_map());

    value_free(s);
    ASSERT(1);
}

void test_value_free_enum(void) {
    Value *unit = value_enum_unit("Type", "A");
    Value *with_payload = value_enum_with_payload("Type", "B", value_string("data"));

    value_free(unit);
    value_free(with_payload);
    ASSERT(1);
}

void test_value_copy_option(void) {
    Value *orig = value_some(value_int(42));
    Value *copy = value_copy(orig);

    ASSERT(copy != NULL);
    ASSERT(copy != orig);
    ASSERT(value_option_is_some(copy));

    Value *orig_inner = value_option_unwrap(orig);
    Value *copy_inner = value_option_unwrap(copy);
    ASSERT_EQ(orig_inner->as.integer, copy_inner->as.integer);

    value_free(orig);
    value_free(copy);
}

void test_value_copy_result(void) {
    Value *orig = value_result_ok(value_string("success"));
    Value *copy = value_copy(orig);

    ASSERT(copy != NULL);
    ASSERT(copy != orig);
    ASSERT(value_result_is_ok(copy));

    Value *orig_inner = value_result_unwrap(orig);
    Value *copy_inner = value_result_unwrap(copy);
    ASSERT_STR_EQ(orig_inner->as.string->data, copy_inner->as.string->data);

    value_free(orig);
    value_free(copy);
}

void test_value_copy_struct(void) {
    Value *orig = value_struct_new("Point", 2);
    value_struct_set_field(orig, 0, "x", value_int(10));
    value_struct_set_field(orig, 1, "y", value_int(20));

    Value *copy = value_copy(orig);

    ASSERT(copy != NULL);
    ASSERT(copy != orig);
    ASSERT_STR_EQ("Point", value_struct_type_name(copy));
    ASSERT_EQ(10, value_struct_get_field(copy, "x")->as.integer);
    ASSERT_EQ(20, value_struct_get_field(copy, "y")->as.integer);

    value_free(orig);
    value_free(copy);
}

void test_value_copy_enum(void) {
    Value *orig = value_enum_with_payload("Event", "Click", value_int(5));
    Value *copy = value_copy(orig);

    ASSERT(copy != NULL);
    ASSERT(copy != orig);
    ASSERT_STR_EQ("Event", value_enum_type_name(copy));
    ASSERT_STR_EQ("Click", value_enum_variant_name(copy));
    ASSERT_EQ(5, value_enum_payload(copy)->as.integer);

    value_free(orig);
    value_free(copy);
}

/*============================================================================
 * Type Predicate Tests
 *============================================================================*/

void test_value_is_option(void) {
    Value *opt_some = value_some(value_int(1));
    Value *opt_none = value_none();
    Value *not_opt = value_int(42);

    ASSERT(value_is_option(opt_some));
    ASSERT(value_is_option(opt_none));
    ASSERT(!value_is_option(not_opt));
    ASSERT(!value_is_option(NULL));

    value_free(opt_some);
    value_free(opt_none);
    value_free(not_opt);
}

void test_value_is_result(void) {
    Value *res_ok = value_result_ok(value_int(1));
    Value *res_err = value_result_err(value_string("e"));
    Value *not_res = value_string("test");

    ASSERT(value_is_result(res_ok));
    ASSERT(value_is_result(res_err));
    ASSERT(!value_is_result(not_res));
    ASSERT(!value_is_result(NULL));

    value_free(res_ok);
    value_free(res_err);
    value_free(not_res);
}

void test_value_is_struct(void) {
    Value *s = value_struct_new("Test", 0);
    Value *not_s = value_array();

    ASSERT(value_is_struct(s));
    ASSERT(!value_is_struct(not_s));
    ASSERT(!value_is_struct(NULL));

    value_free(s);
    value_free(not_s);
}

void test_value_is_enum(void) {
    Value *e = value_enum_unit("Test", "A");
    Value *not_e = value_map();

    ASSERT(value_is_enum(e));
    ASSERT(!value_is_enum(not_e));
    ASSERT(!value_is_enum(NULL));

    value_free(e);
    value_free(not_e);
}

/*============================================================================
 * Nested Type Tests
 *============================================================================*/

void test_option_nested(void) {
    /* Some(Some(42)) */
    Value *inner = value_some(value_int(42));
    Value *outer = value_some(inner);

    ASSERT(value_option_is_some(outer));
    Value *unwrapped_outer = value_option_unwrap(outer);
    ASSERT(value_option_is_some(unwrapped_outer));
    Value *unwrapped_inner = value_option_unwrap(unwrapped_outer);
    ASSERT_EQ(42, unwrapped_inner->as.integer);

    value_free(outer);
}

void test_result_nested(void) {
    /* Ok(Ok(42)) */
    Value *inner = value_result_ok(value_int(42));
    Value *outer = value_result_ok(inner);

    ASSERT(value_result_is_ok(outer));
    Value *unwrapped_outer = value_result_unwrap(outer);
    ASSERT(value_result_is_ok(unwrapped_outer));
    Value *unwrapped_inner = value_result_unwrap(unwrapped_outer);
    ASSERT_EQ(42, unwrapped_inner->as.integer);

    value_free(outer);
}

void test_struct_with_option_field(void) {
    Value *s = value_struct_new("User", 2);
    value_struct_set_field(s, 0, "name", value_string("Alice"));
    value_struct_set_field(s, 1, "email", value_some(value_string("alice@example.com")));

    Value *email_opt = value_struct_get_field(s, "email");
    ASSERT(value_option_is_some(email_opt));
    ASSERT_STR_EQ("alice@example.com", value_option_unwrap(email_opt)->as.string->data);

    value_free(s);
}

void test_struct_with_result_field(void) {
    Value *s = value_struct_new("Response", 2);
    value_struct_set_field(s, 0, "status", value_int(200));
    value_struct_set_field(s, 1, "body", value_result_ok(value_string("OK")));

    Value *body_res = value_struct_get_field(s, "body");
    ASSERT(value_result_is_ok(body_res));
    ASSERT_STR_EQ("OK", value_result_unwrap(body_res)->as.string->data);

    value_free(s);
}

void test_enum_with_struct_payload(void) {
    Value *point = value_struct_new("Point", 2);
    value_struct_set_field(point, 0, "x", value_int(10));
    value_struct_set_field(point, 1, "y", value_int(20));

    Value *e = value_enum_with_payload("Shape", "Point", point);

    ASSERT(value_enum_is_variant(e, "Point"));
    Value *payload = value_enum_payload(e);
    ASSERT(value_is_struct(payload));
    ASSERT_EQ(10, value_struct_get_field(payload, "x")->as.integer);

    value_free(e);
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    printf("Running type system tests...\n\n");

    /* Option Type Tests */
    printf("Option Type Tests:\n");
    RUN_TEST(test_option_some_basic);
    RUN_TEST(test_option_none_basic);
    RUN_TEST(test_option_is_some);
    RUN_TEST(test_option_is_none);
    RUN_TEST(test_option_unwrap_some);
    RUN_TEST(test_option_unwrap_none);
    RUN_TEST(test_option_unwrap_or_some);
    RUN_TEST(test_option_unwrap_or_none);
    RUN_TEST(test_option_null_input);
    RUN_TEST(test_option_non_option_type);
    RUN_TEST(test_option_some_wrapping_nil);

    /* Result Type Tests */
    printf("\nResult Type Tests:\n");
    RUN_TEST(test_result_ok_basic);
    RUN_TEST(test_result_err_basic);
    RUN_TEST(test_result_is_ok);
    RUN_TEST(test_result_is_err);
    RUN_TEST(test_result_unwrap_ok);
    RUN_TEST(test_result_unwrap_err);
    RUN_TEST(test_result_unwrap_or_ok);
    RUN_TEST(test_result_unwrap_or_err);
    RUN_TEST(test_result_unwrap_err_function);
    RUN_TEST(test_result_null_input);
    RUN_TEST(test_result_non_result_type);
    RUN_TEST(test_result_ok_wrapping_nil);

    /* Struct Type Tests */
    printf("\nStruct Type Tests:\n");
    RUN_TEST(test_struct_new_basic);
    RUN_TEST(test_struct_set_field);
    RUN_TEST(test_struct_get_field_by_name);
    RUN_TEST(test_struct_get_field_by_index);
    RUN_TEST(test_struct_type_name);
    RUN_TEST(test_struct_multiple_fields);
    RUN_TEST(test_struct_field_overwrite);
    RUN_TEST(test_struct_field_not_found);
    RUN_TEST(test_struct_null_inputs);
    RUN_TEST(test_struct_empty);
    RUN_TEST(test_struct_index_out_of_bounds);

    /* Enum Type Tests */
    printf("\nEnum Type Tests:\n");
    RUN_TEST(test_enum_unit_basic);
    RUN_TEST(test_enum_with_payload_basic);
    RUN_TEST(test_enum_type_name);
    RUN_TEST(test_enum_variant_name);
    RUN_TEST(test_enum_payload);
    RUN_TEST(test_enum_is_variant);
    RUN_TEST(test_enum_is_variant_wrong);
    RUN_TEST(test_enum_unit_no_payload);
    RUN_TEST(test_enum_null_inputs);
    RUN_TEST(test_enum_non_enum_type);
    RUN_TEST(test_enum_payload_with_nil);

    /* Memory Management Tests */
    printf("\nMemory Management Tests:\n");
    RUN_TEST(test_value_free_option);
    RUN_TEST(test_value_free_result);
    RUN_TEST(test_value_free_struct);
    RUN_TEST(test_value_free_enum);
    RUN_TEST(test_value_copy_option);
    RUN_TEST(test_value_copy_result);
    RUN_TEST(test_value_copy_struct);
    RUN_TEST(test_value_copy_enum);

    /* Type Predicate Tests */
    printf("\nType Predicate Tests:\n");
    RUN_TEST(test_value_is_option);
    RUN_TEST(test_value_is_result);
    RUN_TEST(test_value_is_struct);
    RUN_TEST(test_value_is_enum);

    /* Nested Type Tests */
    printf("\nNested Type Tests:\n");
    RUN_TEST(test_option_nested);
    RUN_TEST(test_result_nested);
    RUN_TEST(test_struct_with_option_field);
    RUN_TEST(test_struct_with_result_field);
    RUN_TEST(test_enum_with_struct_payload);

    return TEST_RESULT();
}
