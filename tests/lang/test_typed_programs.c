/*
 * Agim - Typed Programs Integration Tests
 *
 * Tests the full pipeline: source → parse → compile → execute → verify
 * for typed language features including Option, Result, Struct, and Enum.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "lang/agim.h"
#include "lang/lexer.h"
#include "lang/parser.h"
#include "vm/vm.h"
#include "vm/value.h"

/* Test Helpers */

static int64_t run_and_get_int(const char *source) {
    const char *error = NULL;
    Bytecode *code = agim_compile(source, &error);
    if (!code) {
        printf("    Compile error: %s\n", error);
        agim_error_free(error);
        return -999999;
    }

    VM *vm = vm_new();
    vm->reduction_limit = 1000000;
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    if (result != VM_OK && result != VM_HALT) {
        printf("    Runtime error: %s\n", vm_error(vm));
        vm_free(vm);
        bytecode_free(code);
        return -999999;
    }

    Value *top = vm_peek(vm, 0);
    int64_t val = top && top->type == VAL_INT ? top->as.integer : -999999;

    vm_free(vm);
    bytecode_free(code);
    return val;
}

static bool run_and_get_bool(const char *source) {
    const char *error = NULL;
    Bytecode *code = agim_compile(source, &error);
    if (!code) {
        printf("    Compile error: %s\n", error);
        agim_error_free(error);
        return false;
    }

    VM *vm = vm_new();
    vm->reduction_limit = 1000000;
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    if (result != VM_OK && result != VM_HALT) {
        printf("    Runtime error: %s\n", vm_error(vm));
        vm_free(vm);
        bytecode_free(code);
        return false;
    }

    Value *top = vm_peek(vm, 0);
    bool val = top && top->type == VAL_BOOL ? top->as.boolean : false;

    vm_free(vm);
    bytecode_free(code);
    return val;
}

static const char *run_and_get_string(const char *source) {
    const char *error = NULL;
    Bytecode *code = agim_compile(source, &error);
    if (!code) {
        printf("    Compile error: %s\n", error);
        agim_error_free(error);
        return NULL;
    }

    VM *vm = vm_new();
    vm->reduction_limit = 1000000;
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    if (result != VM_OK && result != VM_HALT) {
        printf("    Runtime error: %s\n", vm_error(vm));
        vm_free(vm);
        bytecode_free(code);
        return NULL;
    }

    Value *top = vm_peek(vm, 0);
    static char buffer[256];
    if (top && top->type == VAL_STRING) {
        strncpy(buffer, top->as.string->data, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
    } else {
        buffer[0] = '\0';
    }

    vm_free(vm);
    bytecode_free(code);
    return buffer;
}

static bool compiles(const char *source) {
    const char *error = NULL;
    Bytecode *code = agim_compile(source, &error);
    if (!code) {
        printf("    Compile error: %s\n", error);
        agim_error_free(error);
        return false;
    }
    bytecode_free(code);
    return true;
}

static bool runs_successfully(const char *source) {
    const char *error = NULL;
    Bytecode *code = agim_compile(source, &error);
    if (!code) {
        printf("    Compile error: %s\n", error);
        agim_error_free(error);
        return false;
    }

    VM *vm = vm_new();
    vm->reduction_limit = 1000000;
    vm_load(vm, code);
    VMResult result = vm_run(vm);

    bool success = (result == VM_OK || result == VM_HALT);
    if (!success) {
        printf("    Runtime error: %s\n", vm_error(vm));
    }

    vm_free(vm);
    bytecode_free(code);
    return success;
}

/* Option Integration Tests */

void test_option_some_creation(void) {
    printf("  Testing some() creation...\n");
    ASSERT_EQ(42, run_and_get_int("let x = some(42)\nunwrap(x)"));
}

void test_option_none_creation(void) {
    printf("  Testing none creation...\n");
    ASSERT(run_and_get_bool("let x = none\nis_none(x)"));
}

void test_option_is_some_check(void) {
    printf("  Testing is_some check...\n");
    ASSERT(run_and_get_bool("is_some(some(5))"));
    ASSERT(!run_and_get_bool("is_some(none)"));
}

void test_option_is_none_check(void) {
    printf("  Testing is_none check...\n");
    ASSERT(run_and_get_bool("is_none(none)"));
    ASSERT(!run_and_get_bool("is_none(some(1))"));
}

void test_option_unwrap_or_some(void) {
    printf("  Testing unwrap_or on Some...\n");
    ASSERT_EQ(5, run_and_get_int("unwrap_or(some(5), 0)"));
}

void test_option_unwrap_or_none(void) {
    printf("  Testing unwrap_or on None...\n");
    ASSERT_EQ(0, run_and_get_int("unwrap_or(none, 0)"));
}

void test_option_in_function(void) {
    printf("  Testing Option in function...\n");

    const char *source =
        "fn find_positive(n) {\n"
        "    if n > 0 {\n"
        "        return some(n)\n"
        "    }\n"
        "    return none\n"
        "}\n"
        "let r1 = find_positive(10)\n"
        "let r2 = find_positive(-5)\n"
        "is_some(r1) and is_none(r2)";

    ASSERT(run_and_get_bool(source));
}

void test_option_match_some(void) {
    printf("  Testing match on Some...\n");

    const char *source =
        "let x = some(42)\n"
        "match x {\n"
        "    some(v) => v\n"
        "    none => 0\n"
        "}";

    ASSERT_EQ(42, run_and_get_int(source));
}

void test_option_match_none(void) {
    printf("  Testing match on None...\n");

    const char *source =
        "let x = none\n"
        "match x {\n"
        "    some(v) => v\n"
        "    none => 99\n"
        "}";

    ASSERT_EQ(99, run_and_get_int(source));
}

/* Result Integration Tests */

void test_result_ok_creation(void) {
    printf("  Testing ok() creation...\n");
    ASSERT_EQ(42, run_and_get_int("let r = ok(42)\nunwrap(r)"));
}

void test_result_err_creation(void) {
    printf("  Testing err() creation...\n");
    ASSERT(run_and_get_bool("let r = err(\"failed\")\nis_err(r)"));
}

void test_result_is_ok_check(void) {
    printf("  Testing is_ok check...\n");
    ASSERT(run_and_get_bool("is_ok(ok(1))"));
    ASSERT(!run_and_get_bool("is_ok(err(\"x\"))"));
}

void test_result_is_err_check(void) {
    printf("  Testing is_err check...\n");
    ASSERT(run_and_get_bool("is_err(err(\"x\"))"));
    ASSERT(!run_and_get_bool("is_err(ok(1))"));
}

void test_result_match_ok(void) {
    printf("  Testing match on Ok...\n");

    const char *source =
        "match ok(5) {\n"
        "    ok(x) => x\n"
        "    err(e) => 0\n"
        "}";

    ASSERT_EQ(5, run_and_get_int(source));
}

void test_result_match_err(void) {
    printf("  Testing match on Err...\n");

    const char *source =
        "match err(\"error\") {\n"
        "    ok(x) => 1\n"
        "    err(e) => 0\n"
        "}";

    ASSERT_EQ(0, run_and_get_int(source));
}

void test_result_unwrap_or_ok(void) {
    printf("  Testing unwrap_or on Ok...\n");
    ASSERT_EQ(42, run_and_get_int("unwrap_or(ok(42), 0)"));
}

void test_result_unwrap_or_err(void) {
    printf("  Testing unwrap_or on Err...\n");
    ASSERT_EQ(99, run_and_get_int("unwrap_or(err(\"fail\"), 99)"));
}

void test_result_in_function(void) {
    printf("  Testing Result in function...\n");

    const char *source =
        "fn divide(a, b) {\n"
        "    if b == 0 {\n"
        "        return err(\"division by zero\")\n"
        "    }\n"
        "    return ok(a / b)\n"
        "}\n"
        "let r1 = divide(10, 2)\n"
        "let r2 = divide(10, 0)\n"
        "is_ok(r1) and is_err(r2)";

    ASSERT(run_and_get_bool(source));
}

void test_result_chaining(void) {
    printf("  Testing Result chaining...\n");

    const char *source =
        "fn safe_div(a, b) {\n"
        "    if b == 0 { return err(\"div0\") }\n"
        "    return ok(a / b)\n"
        "}\n"
        "unwrap_or(safe_div(10, 2), -1)";

    ASSERT_EQ(5, run_and_get_int(source));
}

/* Typed Function Tests */

void test_typed_fn_int_params(void) {
    printf("  Testing typed function with int params...\n");

    const char *source =
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b\n"
        "}\n"
        "add(2, 3)";

    ASSERT_EQ(5, run_and_get_int(source));
}

void test_typed_fn_string_param(void) {
    printf("  Testing typed function with string param...\n");

    const char *source =
        "fn greet(name: string) -> string {\n"
        "    return \"Hello, \" + name\n"
        "}\n"
        "greet(\"World\")";

    ASSERT_STR_EQ("Hello, World", run_and_get_string(source));
}

void test_typed_fn_bool_return(void) {
    printf("  Testing typed function with bool return...\n");

    const char *source =
        "fn is_positive(n: int) -> bool {\n"
        "    return n > 0\n"
        "}\n"
        "is_positive(5)";

    ASSERT(run_and_get_bool(source));
}

void test_typed_fn_option_return(void) {
    printf("  Testing typed function with Option return...\n");

    const char *source =
        "fn safe_sqrt(n: int) -> Option<int> {\n"
        "    if n < 0 {\n"
        "        return none\n"
        "    }\n"
        "    return some(n)\n"
        "}\n"
        "is_some(safe_sqrt(4)) and is_none(safe_sqrt(-1))";

    ASSERT(run_and_get_bool(source));
}

void test_typed_fn_result_return(void) {
    printf("  Testing typed function with Result return...\n");

    const char *source =
        "fn check_positive(n: int) -> Result<int, string> {\n"
        "    if n < 0 {\n"
        "        return err(\"must be positive\")\n"
        "    }\n"
        "    return ok(n)\n"
        "}\n"
        "is_ok(check_positive(42)) and is_err(check_positive(-1))";

    ASSERT(run_and_get_bool(source));
}

void test_typed_variable_declaration(void) {
    printf("  Testing typed variable declaration...\n");

    const char *source =
        "let x: int = 10\n"
        "let y: int = 20\n"
        "x + y";

    ASSERT_EQ(30, run_and_get_int(source));
}

void test_typed_mutable_variable(void) {
    printf("  Testing typed mutable variable...\n");

    const char *source =
        "let mut x: int = 10\n"
        "x = x + 5\n"
        "x";

    ASSERT_EQ(15, run_and_get_int(source));
}

void test_typed_array(void) {
    printf("  Testing typed array...\n");

    const char *source =
        "let numbers: [int] = [1, 2, 3, 4, 5]\n"
        "len(numbers)";

    ASSERT_EQ(5, run_and_get_int(source));
}

void test_typed_map(void) {
    printf("  Testing typed map...\n");

    const char *source =
        "let scores: map<string, int> = {\n"
        "    \"alice\": 100,\n"
        "    \"bob\": 85\n"
        "}\n"
        "scores[\"alice\"]";

    ASSERT_EQ(100, run_and_get_int(source));
}

/* Struct Integration Tests */

void test_struct_definition(void) {
    printf("  Testing struct definition...\n");

    const char *source =
        "struct Point {\n"
        "    x: int,\n"
        "    y: int\n"
        "}\n"
        "1";  /* Just verify it compiles */

    ASSERT(compiles(source));
}

void test_struct_instantiation(void) {
    printf("  Testing struct instantiation...\n");

    const char *source =
        "struct Point {\n"
        "    x: int,\n"
        "    y: int\n"
        "}\n"
        "let p = Point { x: 10, y: 20 }\n"
        "p.x";

    ASSERT_EQ(10, run_and_get_int(source));
}

void test_struct_field_access(void) {
    printf("  Testing struct field access...\n");

    const char *source =
        "struct Point {\n"
        "    x: int,\n"
        "    y: int\n"
        "}\n"
        "let p = Point { x: 5, y: 15 }\n"
        "p.x + p.y";

    ASSERT_EQ(20, run_and_get_int(source));
}

void test_struct_in_function(void) {
    printf("  Testing struct in function...\n");

    const char *source =
        "struct Point {\n"
        "    x: int,\n"
        "    y: int\n"
        "}\n"
        "fn make_point(x: int, y: int) -> Point {\n"
        "    return Point { x: x, y: y }\n"
        "}\n"
        "let p = make_point(3, 4)\n"
        "p.x * p.y";

    ASSERT_EQ(12, run_and_get_int(source));
}

void test_struct_multiple_fields(void) {
    printf("  Testing struct with multiple fields...\n");

    const char *source =
        "struct User {\n"
        "    name: string,\n"
        "    age: int,\n"
        "    active: bool\n"
        "}\n"
        "let u = User { name: \"Alice\", age: 30, active: true }\n"
        "u.age";

    ASSERT_EQ(30, run_and_get_int(source));
}

void test_struct_with_option_field(void) {
    printf("  Testing struct with Option field...\n");

    const char *source =
        "struct User {\n"
        "    name: string,\n"
        "    email: Option<string>\n"
        "}\n"
        "let u1 = User { name: \"Alice\", email: some(\"a@b.com\") }\n"
        "let u2 = User { name: \"Bob\", email: none }\n"
        "is_some(u1.email) and is_none(u2.email)";

    ASSERT(run_and_get_bool(source));
}

/* Enum Integration Tests */

void test_enum_definition(void) {
    printf("  Testing enum definition...\n");

    const char *source =
        "enum Color {\n"
        "    Red,\n"
        "    Green,\n"
        "    Blue\n"
        "}\n"
        "1";  /* Just verify it compiles */

    ASSERT(compiles(source));
}

void test_enum_unit_variant(void) {
    printf("  Testing enum unit variant...\n");

    const char *source =
        "enum Status {\n"
        "    Ok,\n"
        "    Pending,\n"
        "    Error\n"
        "}\n"
        "let s = Status::Ok\n"
        "1";

    ASSERT(runs_successfully(source));
}

void test_enum_variant_with_payload(void) {
    printf("  Testing enum variant with payload...\n");

    const char *source =
        "enum MyResult {\n"
        "    Success(int),\n"
        "    Failure(string)\n"
        "}\n"
        "let r = MyResult::Success(42)\n"
        "1";

    ASSERT(runs_successfully(source));
}

void test_enum_match_unit(void) {
    printf("  Testing match on unit enum...\n");

    const char *source =
        "enum Color {\n"
        "    Red,\n"
        "    Green,\n"
        "    Blue\n"
        "}\n"
        "let c = Color::Red\n"
        "match c {\n"
        "    Red => 1\n"
        "    Green => 2\n"
        "    Blue => 3\n"
        "}";

    ASSERT_EQ(1, run_and_get_int(source));
}

void test_enum_match_payload(void) {
    printf("  Testing match on enum with payload...\n");

    const char *source =
        "enum Message {\n"
        "    Text(string),\n"
        "    Number(int)\n"
        "}\n"
        "let m = Message::Number(42)\n"
        "match m {\n"
        "    Text(s) => 0\n"
        "    Number(n) => n\n"
        "}";

    ASSERT_EQ(42, run_and_get_int(source));
}

void test_enum_in_function(void) {
    printf("  Testing enum in function...\n");

    const char *source =
        "enum Decision {\n"
        "    Continue,\n"
        "    Stop,\n"
        "    Retry(int)\n"
        "}\n"
        "fn decide(count: int) -> Decision {\n"
        "    if count < 3 {\n"
        "        return Decision::Retry(count + 1)\n"
        "    }\n"
        "    return Decision::Stop\n"
        "}\n"
        "let d = decide(1)\n"
        "match d {\n"
        "    Continue => 0\n"
        "    Stop => -1\n"
        "    Retry(n) => n\n"
        "}";

    ASSERT_EQ(2, run_and_get_int(source));
}

/* Complex Integration Tests */

void test_struct_and_result_combined(void) {
    printf("  Testing struct and Result combined...\n");

    const char *source =
        "struct User {\n"
        "    id: int,\n"
        "    name: string\n"
        "}\n"
        "fn create_user(id: int, name: string) -> Result<User, string> {\n"
        "    if id <= 0 {\n"
        "        return err(\"invalid id\")\n"
        "    }\n"
        "    return ok(User { id: id, name: name })\n"
        "}\n"
        "let r = create_user(1, \"Alice\")\n"
        "match r {\n"
        "    ok(u) => u.id\n"
        "    err(e) => -1\n"
        "}";

    ASSERT_EQ(1, run_and_get_int(source));
}

void test_enum_and_struct_combined(void) {
    printf("  Testing enum and struct combined...\n");

    const char *source =
        "struct Point {\n"
        "    x: int,\n"
        "    y: int\n"
        "}\n"
        "enum Shape {\n"
        "    Circle(int),\n"
        "    Rectangle(Point)\n"
        "}\n"
        "let s = Shape::Rectangle(Point { x: 10, y: 20 })\n"
        "match s {\n"
        "    Circle(r) => r\n"
        "    Rectangle(p) => p.x + p.y\n"
        "}";

    ASSERT_EQ(30, run_and_get_int(source));
}

void test_option_result_combination(void) {
    printf("  Testing Option and Result combination...\n");

    const char *source =
        "fn find_and_divide(nums: [int], divisor: int) -> Result<Option<int>, string> {\n"
        "    if divisor == 0 {\n"
        "        return err(\"division by zero\")\n"
        "    }\n"
        "    if len(nums) == 0 {\n"
        "        return ok(none)\n"
        "    }\n"
        "    return ok(some(nums[0] / divisor))\n"
        "}\n"
        "let r = find_and_divide([10, 20, 30], 2)\n"
        "match r {\n"
        "    ok(opt) => match opt {\n"
        "        some(v) => v\n"
        "        none => -1\n"
        "    }\n"
        "    err(e) => -2\n"
        "}";

    ASSERT_EQ(5, run_and_get_int(source));
}

void test_typed_recursive_function(void) {
    printf("  Testing typed recursive function...\n");

    const char *source =
        "fn factorial(n: int) -> int {\n"
        "    if n <= 1 {\n"
        "        return 1\n"
        "    }\n"
        "    return n * factorial(n - 1)\n"
        "}\n"
        "factorial(5)";

    ASSERT_EQ(120, run_and_get_int(source));
}

void test_typed_higher_order_function(void) {
    printf("  Testing typed higher order function...\n");

    const char *source =
        "fn apply_twice(f, x: int) -> int {\n"
        "    return f(f(x))\n"
        "}\n"
        "fn double(n: int) -> int {\n"
        "    return n * 2\n"
        "}\n"
        "apply_twice(double, 3)";

    ASSERT_EQ(12, run_and_get_int(source));
}

/* Main */

int main(void) {
    printf("Running typed programs integration tests...\n\n");

    /* Option Integration Tests */
    printf("Option Integration Tests:\n");
    RUN_TEST(test_option_some_creation);
    RUN_TEST(test_option_none_creation);
    RUN_TEST(test_option_is_some_check);
    RUN_TEST(test_option_is_none_check);
    RUN_TEST(test_option_unwrap_or_some);
    RUN_TEST(test_option_unwrap_or_none);
    RUN_TEST(test_option_in_function);
    RUN_TEST(test_option_match_some);
    RUN_TEST(test_option_match_none);

    /* Result Integration Tests */
    printf("\nResult Integration Tests:\n");
    RUN_TEST(test_result_ok_creation);
    RUN_TEST(test_result_err_creation);
    RUN_TEST(test_result_is_ok_check);
    RUN_TEST(test_result_is_err_check);
    RUN_TEST(test_result_match_ok);
    RUN_TEST(test_result_match_err);
    RUN_TEST(test_result_unwrap_or_ok);
    RUN_TEST(test_result_unwrap_or_err);
    RUN_TEST(test_result_in_function);
    RUN_TEST(test_result_chaining);

    /* Typed Function Tests */
    printf("\nTyped Function Tests:\n");
    RUN_TEST(test_typed_fn_int_params);
    RUN_TEST(test_typed_fn_string_param);
    RUN_TEST(test_typed_fn_bool_return);
    RUN_TEST(test_typed_fn_option_return);
    RUN_TEST(test_typed_fn_result_return);
    RUN_TEST(test_typed_variable_declaration);
    RUN_TEST(test_typed_mutable_variable);
    RUN_TEST(test_typed_array);
    RUN_TEST(test_typed_map);

    /* Struct Integration Tests */
    printf("\nStruct Integration Tests:\n");
    RUN_TEST(test_struct_definition);
    RUN_TEST(test_struct_instantiation);
    RUN_TEST(test_struct_field_access);
    RUN_TEST(test_struct_in_function);
    RUN_TEST(test_struct_multiple_fields);
    RUN_TEST(test_struct_with_option_field);

    /* Enum Integration Tests */
    printf("\nEnum Integration Tests:\n");
    RUN_TEST(test_enum_definition);
    RUN_TEST(test_enum_unit_variant);
    RUN_TEST(test_enum_variant_with_payload);
    RUN_TEST(test_enum_match_unit);
    RUN_TEST(test_enum_match_payload);
    RUN_TEST(test_enum_in_function);

    /* Complex Integration Tests */
    printf("\nComplex Integration Tests:\n");
    RUN_TEST(test_struct_and_result_combined);
    RUN_TEST(test_enum_and_struct_combined);
    RUN_TEST(test_option_result_combination);
    RUN_TEST(test_typed_recursive_function);
    RUN_TEST(test_typed_higher_order_function);

    return TEST_RESULT();
}
