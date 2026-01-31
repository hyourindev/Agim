/*
 * Agim - End-to-End Program Tests
 *
 * Tests complete Agim programs from source to execution.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "lang/agim.h"
#include "vm/vm.h"

static int64_t run_program(const char *source) {
    const char *error = NULL;
    Bytecode *code = agim_compile(source, &error);
    if (!code) {
        printf("    Compile error: %s\n", error);
        agim_error_free(error);
        return -999999;
    }

    VM *vm = vm_new();
    vm->reduction_limit = 10000000;
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

/* Classic Algorithm Tests */

void test_factorial_iterative(void) {
    printf("  Testing iterative factorial...\n");

    const char *source =
        "fn factorial(n) {\n"
        "    let result = 1\n"
        "    let i = 2\n"
        "    while i <= n {\n"
        "        result = result * i\n"
        "        i = i + 1\n"
        "    }\n"
        "    return result\n"
        "}\n"
        "factorial(10)";

    ASSERT_EQ(3628800, run_program(source));
}

void test_factorial_recursive(void) {
    printf("  Testing recursive factorial...\n");

    const char *source =
        "fn factorial(n) {\n"
        "    if n <= 1 { return 1 }\n"
        "    return n * factorial(n - 1)\n"
        "}\n"
        "factorial(10)";

    ASSERT_EQ(3628800, run_program(source));
}

void test_fibonacci_iterative(void) {
    printf("  Testing iterative fibonacci...\n");

    const char *source =
        "fn fib(n) {\n"
        "    if n <= 1 { return n }\n"
        "    let a = 0\n"
        "    let b = 1\n"
        "    let i = 2\n"
        "    while i <= n {\n"
        "        let temp = a + b\n"
        "        a = b\n"
        "        b = temp\n"
        "        i = i + 1\n"
        "    }\n"
        "    return b\n"
        "}\n"
        "fib(20)";

    ASSERT_EQ(6765, run_program(source));
}

void test_fibonacci_recursive(void) {
    printf("  Testing recursive fibonacci...\n");

    const char *source =
        "fn fib(n) {\n"
        "    if n <= 1 { return n }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "fib(15)";

    ASSERT_EQ(610, run_program(source));
}

void test_gcd(void) {
    printf("  Testing GCD (Euclidean algorithm)...\n");

    const char *source =
        "fn gcd(a, b) {\n"
        "    while b != 0 {\n"
        "        let temp = b\n"
        "        b = a % b\n"
        "        a = temp\n"
        "    }\n"
        "    return a\n"
        "}\n"
        "gcd(48, 18)";

    ASSERT_EQ(6, run_program(source));
}

void test_is_prime(void) {
    printf("  Testing primality check...\n");

    const char *source =
        "fn is_prime(n) {\n"
        "    if n < 2 { return 0 }\n"
        "    let i = 2\n"
        "    while i * i <= n {\n"
        "        if n % i == 0 { return 0 }\n"
        "        i = i + 1\n"
        "    }\n"
        "    return 1\n"
        "}\n"
        "is_prime(97)";

    ASSERT_EQ(1, run_program(source));

    const char *source2 =
        "fn is_prime(n) {\n"
        "    if n < 2 { return 0 }\n"
        "    let i = 2\n"
        "    while i * i <= n {\n"
        "        if n % i == 0 { return 0 }\n"
        "        i = i + 1\n"
        "    }\n"
        "    return 1\n"
        "}\n"
        "is_prime(100)";

    ASSERT_EQ(0, run_program(source2));
}

/* Array Tests */

void test_array_sum(void) {
    printf("  Testing array sum with for loop...\n");

    const char *source =
        "let arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]\n"
        "let sum = 0\n"
        "for x in arr {\n"
        "    sum = sum + x\n"
        "}\n"
        "sum";

    ASSERT_EQ(55, run_program(source));
}

void test_array_max(void) {
    printf("  Testing array max...\n");

    const char *source =
        "let arr = [3, 1, 4, 1, 5, 9, 2, 6, 5, 3]\n"
        "let max = arr[0]\n"
        "for x in arr {\n"
        "    if x > max { max = x }\n"
        "}\n"
        "max";

    ASSERT_EQ(9, run_program(source));
}

void test_array_count(void) {
    printf("  Testing array element count...\n");

    const char *source =
        "let arr = [1, 2, 1, 3, 1, 4, 1, 5]\n"
        "let count = 0\n"
        "for x in arr {\n"
        "    if x == 1 { count = count + 1 }\n"
        "}\n"
        "count";

    ASSERT_EQ(4, run_program(source));
}

/* Control Flow Tests */

void test_nested_loops(void) {
    printf("  Testing nested loops...\n");

    const char *source =
        "let sum = 0\n"
        "let i = 1\n"
        "while i <= 5 {\n"
        "    let j = 1\n"
        "    while j <= 5 {\n"
        "        sum = sum + i * j\n"
        "        j = j + 1\n"
        "    }\n"
        "    i = i + 1\n"
        "}\n"
        "sum";

    ASSERT_EQ(225, run_program(source));
}

void test_break_continue(void) {
    printf("  Testing break in nested loop...\n");

    const char *source =
        "let result = 0\n"
        "let i = 0\n"
        "while i < 100 {\n"
        "    i = i + 1\n"
        "    if i == 50 { break }\n"
        "    result = result + 1\n"
        "}\n"
        "result";

    ASSERT_EQ(49, run_program(source));
}

void test_early_return(void) {
    printf("  Testing early return...\n");

    const char *source =
        "fn find_first_even(arr) {\n"
        "    for x in arr {\n"
        "        if x % 2 == 0 { return x }\n"
        "    }\n"
        "    return -1\n"
        "}\n"
        "find_first_even([1, 3, 5, 8, 9, 10])";

    ASSERT_EQ(8, run_program(source));
}

/* Function Tests */

void test_higher_order(void) {
    printf("  Testing function composition...\n");

    const char *source =
        "fn double(x) { return x * 2 }\n"
        "fn square(x) { return x * x }\n"
        "fn apply_twice(f, x) { return f(f(x)) }\n"
        "apply_twice(double, 3)";

    ASSERT_EQ(12, run_program(source));
}

void test_mutual_recursion(void) {
    printf("  Testing mutual recursion...\n");

    const char *source =
        "fn is_even(n) {\n"
        "    if n == 0 { return 1 }\n"
        "    return is_odd(n - 1)\n"
        "}\n"
        "fn is_odd(n) {\n"
        "    if n == 0 { return 0 }\n"
        "    return is_even(n - 1)\n"
        "}\n"
        "is_even(10)";

    ASSERT_EQ(1, run_program(source));
}

/* Map Tests */

void test_map_operations(void) {
    printf("  Testing map operations...\n");

    const char *source =
        "let person = {name: 0, age: 25, score: 100}\n"
        "person.age + person.score";

    ASSERT_EQ(125, run_program(source));
}

/* Complex Programs */

void test_bubble_sort(void) {
    printf("  Testing bubble sort...\n");

    const char *source =
        "let arr = [5, 2, 8, 1, 9, 3, 7, 4, 6, 0]\n"
        "let n = 10\n"
        "let i = 0\n"
        "while i < n - 1 {\n"
        "    let j = 0\n"
        "    while j < n - i - 1 {\n"
        "        if arr[j] > arr[j + 1] {\n"
        "            let temp = arr[j]\n"
        "            arr[j] = arr[j + 1]\n"
        "            arr[j + 1] = temp\n"
        "        }\n"
        "        j = j + 1\n"
        "    }\n"
        "    i = i + 1\n"
        "}\n"
        "arr[0] * 1000 + arr[4] * 100 + arr[9] * 10";

    /* arr should be [0,1,2,3,4,5,6,7,8,9] */
    /* Result: 0*1000 + 4*100 + 9*10 = 490 */
    ASSERT_EQ(490, run_program(source));
}

void test_sum_of_primes(void) {
    printf("  Testing sum of primes...\n");

    const char *source =
        "fn is_prime(n) {\n"
        "    if n < 2 { return 0 }\n"
        "    let i = 2\n"
        "    while i * i <= n {\n"
        "        if n % i == 0 { return 0 }\n"
        "        i = i + 1\n"
        "    }\n"
        "    return 1\n"
        "}\n"
        "\n"
        "let sum = 0\n"
        "let n = 2\n"
        "while n <= 50 {\n"
        "    if is_prime(n) == 1 {\n"
        "        sum = sum + n\n"
        "    }\n"
        "    n = n + 1\n"
        "}\n"
        "sum";

    /* Primes <= 50: 2,3,5,7,11,13,17,19,23,29,31,37,41,43,47 = 328 */
    ASSERT_EQ(328, run_program(source));
}

/* Main */

int main(void) {
    printf("\n");
    printf("=================================================\n");
    printf("Agim End-to-End Program Tests\n");
    printf("=================================================\n\n");

    printf("Classic algorithms:\n");
    RUN_TEST(test_factorial_iterative);
    RUN_TEST(test_factorial_recursive);
    RUN_TEST(test_fibonacci_iterative);
    RUN_TEST(test_fibonacci_recursive);
    RUN_TEST(test_gcd);
    RUN_TEST(test_is_prime);

    printf("\nArray operations:\n");
    RUN_TEST(test_array_sum);
    RUN_TEST(test_array_max);
    RUN_TEST(test_array_count);

    printf("\nControl flow:\n");
    RUN_TEST(test_nested_loops);
    RUN_TEST(test_break_continue);
    RUN_TEST(test_early_return);

    printf("\nFunctions:\n");
    RUN_TEST(test_higher_order);
    RUN_TEST(test_mutual_recursion);

    printf("\nMaps:\n");
    RUN_TEST(test_map_operations);

    printf("\nComplex programs:\n");
    RUN_TEST(test_bubble_sort);
    RUN_TEST(test_sum_of_primes);

    printf("\n=================================================\n");
    return TEST_RESULT();
}
