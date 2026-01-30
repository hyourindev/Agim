/*
 * Agim - Compiler Tests
 *
 * End-to-end tests that compile and run Agim programs.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "lang/agim.h"
#include "vm/vm.h"

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

/*============================================================================
 * Basic Expression Tests
 *============================================================================*/

void test_literals(void) {
    printf("  Testing literals...\n");

    ASSERT_EQ(42, run_and_get_int("42"));
    ASSERT_EQ(0, run_and_get_int("0"));
    ASSERT_EQ(-17, run_and_get_int("-17"));
}

void test_arithmetic(void) {
    printf("  Testing arithmetic...\n");

    ASSERT_EQ(7, run_and_get_int("3 + 4"));
    ASSERT_EQ(10, run_and_get_int("15 - 5"));
    ASSERT_EQ(24, run_and_get_int("6 * 4"));
    ASSERT_EQ(5, run_and_get_int("20 / 4"));
    ASSERT_EQ(1, run_and_get_int("10 % 3"));
}

void test_precedence(void) {
    printf("  Testing operator precedence...\n");

    ASSERT_EQ(14, run_and_get_int("2 + 3 * 4"));
    ASSERT_EQ(20, run_and_get_int("(2 + 3) * 4"));
    ASSERT_EQ(7, run_and_get_int("1 + 2 * 3"));
    ASSERT_EQ(13, run_and_get_int("((10 + 5) * 2 - 4) / 2"));
}

void test_comparison(void) {
    printf("  Testing comparison...\n");

    ASSERT_EQ(1, run_and_get_int("5 < 10 ? 1 : 0"));
    ASSERT_EQ(0, run_and_get_int("5 > 10 ? 1 : 0"));
    ASSERT_EQ(1, run_and_get_int("5 == 5 ? 1 : 0"));
    ASSERT_EQ(1, run_and_get_int("5 != 10 ? 1 : 0"));
    ASSERT_EQ(1, run_and_get_int("5 <= 5 ? 1 : 0"));
    ASSERT_EQ(1, run_and_get_int("10 >= 5 ? 1 : 0"));
}

void test_logical(void) {
    printf("  Testing logical operators...\n");

    ASSERT_EQ(1, run_and_get_int("true and true ? 1 : 0"));
    ASSERT_EQ(0, run_and_get_int("true and false ? 1 : 0"));
    ASSERT_EQ(1, run_and_get_int("true or false ? 1 : 0"));
    ASSERT_EQ(0, run_and_get_int("false or false ? 1 : 0"));
    ASSERT_EQ(0, run_and_get_int("not true ? 1 : 0"));
    ASSERT_EQ(1, run_and_get_int("not false ? 1 : 0"));
}

/*============================================================================
 * Variable Tests
 *============================================================================*/

void test_let(void) {
    printf("  Testing let...\n");

    ASSERT_EQ(42, run_and_get_int("let x = 42\nx"));
    ASSERT_EQ(7, run_and_get_int("let x = 3\nlet y = 4\nx + y"));
    ASSERT_EQ(100, run_and_get_int("let x = 10\nx = x * 10\nx"));
}

void test_const(void) {
    printf("  Testing const...\n");

    ASSERT_EQ(42, run_and_get_int("const x = 42\nx"));
}

/*============================================================================
 * Control Flow Tests
 *============================================================================*/

void test_if(void) {
    printf("  Testing if...\n");

    ASSERT_EQ(1, run_and_get_int("if true { 1 } else { 0 }"));
    ASSERT_EQ(0, run_and_get_int("if false { 1 } else { 0 }"));
    ASSERT_EQ(42, run_and_get_int("let x = 10\nif x > 5 { 42 } else { 0 }"));
}

void test_if_else_chain(void) {
    printf("  Testing if-else chain...\n");

    const char *source =
        "let x = 2\n"
        "if x == 1 { 10 }\n"
        "else if x == 2 { 20 }\n"
        "else if x == 3 { 30 }\n"
        "else { 0 }";
    ASSERT_EQ(20, run_and_get_int(source));
}

void test_while(void) {
    printf("  Testing while...\n");

    const char *source =
        "let sum = 0\n"
        "let i = 1\n"
        "while i <= 10 {\n"
        "    sum = sum + i\n"
        "    i = i + 1\n"
        "}\n"
        "sum";
    ASSERT_EQ(55, run_and_get_int(source));
}

void test_while_break(void) {
    printf("  Testing while with break...\n");

    const char *source =
        "let sum = 0\n"
        "let i = 1\n"
        "while true {\n"
        "    sum = sum + i\n"
        "    i = i + 1\n"
        "    if i > 10 { break }\n"
        "}\n"
        "sum";
    ASSERT_EQ(55, run_and_get_int(source));
}

/*============================================================================
 * Function Tests
 *============================================================================*/

void test_fn_simple(void) {
    printf("  Testing simple function...\n");

    const char *source =
        "fn add(a, b) {\n"
        "    return a + b\n"
        "}\n"
        "add(10, 32)";
    ASSERT_EQ(42, run_and_get_int(source));
}

void test_fn_recursive(void) {
    printf("  Testing recursive function...\n");

    const char *source =
        "fn factorial(n) {\n"
        "    if n <= 1 { return 1 }\n"
        "    return n * factorial(n - 1)\n"
        "}\n"
        "factorial(5)";
    ASSERT_EQ(120, run_and_get_int(source));
}

void test_fn_fibonacci(void) {
    printf("  Testing fibonacci...\n");

    const char *source =
        "fn fib(n) {\n"
        "    if n <= 1 { return n }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "fib(10)";
    ASSERT_EQ(55, run_and_get_int(source));
}

void test_fn_multiple(void) {
    printf("  Testing multiple functions...\n");

    const char *source =
        "fn double(x) {\n"
        "    return x * 2\n"
        "}\n"
        "fn add_ten(x) {\n"
        "    return x + 10\n"
        "}\n"
        "add_ten(double(5))";
    ASSERT_EQ(20, run_and_get_int(source));
}

/*============================================================================
 * String Tests
 *============================================================================*/

void test_string_concat(void) {
    printf("  Testing string concatenation...\n");

    ASSERT_STR_EQ("hello world", run_and_get_string("\"hello\" + \" \" + \"world\""));
}

void test_string_in_fn(void) {
    printf("  Testing string in function...\n");

    const char *source =
        "fn greet(name) {\n"
        "    return \"Hello, \" + name + \"!\"\n"
        "}\n"
        "greet(\"World\")";
    ASSERT_STR_EQ("Hello, World!", run_and_get_string(source));
}

/*============================================================================
 * Array Tests
 *============================================================================*/

void test_array_literal(void) {
    printf("  Testing array literal...\n");

    ASSERT_EQ(1, run_and_get_int("[1, 2, 3][0]"));
    ASSERT_EQ(2, run_and_get_int("[1, 2, 3][1]"));
    ASSERT_EQ(3, run_and_get_int("[1, 2, 3][2]"));
}

void test_array_assign(void) {
    printf("  Testing array assignment...\n");

    const char *source =
        "let arr = [1, 2, 3]\n"
        "arr[1] = 42\n"
        "arr[1]";
    ASSERT_EQ(42, run_and_get_int(source));
}

/*============================================================================
 * Map Tests
 *============================================================================*/

void test_map_literal(void) {
    printf("  Testing map literal...\n");

    ASSERT_EQ(10, run_and_get_int("{a: 10, b: 20}.a"));
    ASSERT_EQ(20, run_and_get_int("{a: 10, b: 20}.b"));
}

void test_map_assign(void) {
    printf("  Testing map assignment...\n");

    const char *source =
        "let m = {x: 1}\n"
        "m.x = 42\n"
        "m.x";
    ASSERT_EQ(42, run_and_get_int(source));
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Agim Compiler Tests\n");
    printf("=================================================\n\n");

    printf("Expression tests:\n");
    RUN_TEST(test_literals);
    RUN_TEST(test_arithmetic);
    RUN_TEST(test_precedence);
    RUN_TEST(test_comparison);
    RUN_TEST(test_logical);

    printf("\nVariable tests:\n");
    RUN_TEST(test_let);
    RUN_TEST(test_const);

    printf("\nControl flow tests:\n");
    RUN_TEST(test_if);
    RUN_TEST(test_if_else_chain);
    RUN_TEST(test_while);
    RUN_TEST(test_while_break);

    printf("\nFunction tests:\n");
    RUN_TEST(test_fn_simple);
    RUN_TEST(test_fn_recursive);
    RUN_TEST(test_fn_fibonacci);
    RUN_TEST(test_fn_multiple);

    printf("\nString tests:\n");
    RUN_TEST(test_string_concat);
    RUN_TEST(test_string_in_fn);

    printf("\nArray tests:\n");
    RUN_TEST(test_array_literal);
    RUN_TEST(test_array_assign);

    printf("\nMap tests:\n");
    RUN_TEST(test_map_literal);
    RUN_TEST(test_map_assign);

    printf("\n=================================================\n");
    return TEST_RESULT();
}
