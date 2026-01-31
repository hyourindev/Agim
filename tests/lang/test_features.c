/*
 * Agim - Feature Tests
 *
 * Tests for modules, Result types, and enhanced tool system.
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

/* Module/Import Tests */

void test_import_lexer(void) {
    printf("  Testing import/export tokens...\n");

    Lexer *lexer = lexer_new("import from export match ok err try");

    ASSERT_EQ(TOK_IMPORT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_FROM, lexer_next(lexer).type);
    ASSERT_EQ(TOK_EXPORT, lexer_next(lexer).type);
    ASSERT_EQ(TOK_MATCH, lexer_next(lexer).type);
    ASSERT_EQ(TOK_OK, lexer_next(lexer).type);
    ASSERT_EQ(TOK_ERR, lexer_next(lexer).type);
    ASSERT_EQ(TOK_TRY, lexer_next(lexer).type);
    ASSERT_EQ(TOK_EOF, lexer_next(lexer).type);

    lexer_free(lexer);
}

void test_fat_arrow(void) {
    printf("  Testing fat arrow token...\n");

    Lexer *lexer = lexer_new("=> -> =");

    ASSERT_EQ(TOK_FAT_ARROW, lexer_next(lexer).type);
    ASSERT_EQ(TOK_ARROW, lexer_next(lexer).type);
    ASSERT_EQ(TOK_ASSIGN, lexer_next(lexer).type);
    ASSERT_EQ(TOK_EOF, lexer_next(lexer).type);

    lexer_free(lexer);
}

void test_export_parsing(void) {
    printf("  Testing export parsing...\n");

    /* Export let statement */
    ASSERT(compiles("export let x = 42"));

    /* Export function */
    ASSERT(compiles("export fn add(a, b) { return a + b }"));
}

/* Result Type Tests */

void test_result_ok(void) {
    printf("  Testing ok() creation...\n");

    /* Create ok result */
    ASSERT_EQ(42, run_and_get_int("let r = ok(42)\nunwrap(r)"));
}

void test_result_err(void) {
    printf("  Testing err() creation...\n");

    /* Create err result and check is_err */
    ASSERT(run_and_get_bool("let r = err(\"failed\")\nis_err(r)"));
}

void test_is_ok_is_err(void) {
    printf("  Testing is_ok/is_err...\n");

    /* is_ok on ok result */
    ASSERT(run_and_get_bool("is_ok(ok(1))"));

    /* is_ok on err result */
    ASSERT(!run_and_get_bool("is_ok(err(\"x\"))"));

    /* is_err on err result */
    ASSERT(run_and_get_bool("is_err(err(\"x\"))"));

    /* is_err on ok result */
    ASSERT(!run_and_get_bool("is_err(ok(1))"));
}

void test_unwrap(void) {
    printf("  Testing unwrap...\n");

    /* Unwrap ok value */
    ASSERT_EQ(100, run_and_get_int("unwrap(ok(100))"));
}

void test_unwrap_or(void) {
    printf("  Testing unwrap_or...\n");

    /* Unwrap ok - returns inner value */
    ASSERT_EQ(42, run_and_get_int("unwrap_or(ok(42), 0)"));

    /* Unwrap err - returns default */
    ASSERT_EQ(99, run_and_get_int("unwrap_or(err(\"fail\"), 99)"));
}

void test_result_in_function(void) {
    printf("  Testing Result in functions...\n");

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
    printf("  Testing Result value extraction...\n");

    const char *source =
        "fn safe_div(a, b) {\n"
        "    if b == 0 { return err(\"div0\") }\n"
        "    return ok(a / b)\n"
        "}\n"
        "unwrap_or(safe_div(10, 2), -1)";

    ASSERT_EQ(5, run_and_get_int(source));
}

/* Match Expression Tests */

void test_match_parsing(void) {
    printf("  Testing match expression parsing...\n");

    /* Match on result */
    const char *source =
        "let r = ok(42)\n"
        "match r {\n"
        "    ok(x) => x\n"
        "    err(e) => 0\n"
        "}";

    ASSERT_EQ(42, run_and_get_int(source));
}

void test_match_err_branch(void) {
    printf("  Testing match err branch...\n");

    const char *source =
        "let r = err(\"failed\")\n"
        "match r {\n"
        "    ok(x) => 1\n"
        "    err(e) => 99\n"
        "}";

    ASSERT_EQ(99, run_and_get_int(source));
}

void test_match_with_function(void) {
    printf("  Testing match with function result...\n");

    const char *source =
        "fn compute(x) {\n"
        "    if x < 0 { return err(\"negative\") }\n"
        "    return ok(x * 2)\n"
        "}\n"
        "let result = match compute(5) {\n"
        "    ok(v) => v\n"
        "    err(e) => -1\n"
        "}\n"
        "result";

    ASSERT_EQ(10, run_and_get_int(source));
}

/* Value-Level Result Tests */

void test_value_result_ok(void) {
    printf("  Testing value_result_ok...\n");

    Value *inner = value_int(42);
    Value *result = value_result_ok(inner);

    ASSERT(result != NULL);
    ASSERT_EQ(VAL_RESULT, result->type);
    ASSERT(value_result_is_ok(result));
    ASSERT(!value_result_is_err(result));

    Value *unwrapped = value_result_unwrap(result);
    ASSERT(unwrapped != NULL);
    ASSERT_EQ(42, unwrapped->as.integer);

    value_free(result);
}

void test_value_result_err(void) {
    printf("  Testing value_result_err...\n");

    Value *err_val = value_string("error message");
    Value *result = value_result_err(err_val);

    ASSERT(result != NULL);
    ASSERT_EQ(VAL_RESULT, result->type);
    ASSERT(!value_result_is_ok(result));
    ASSERT(value_result_is_err(result));

    value_free(result);
}

void test_value_result_unwrap_or(void) {
    printf("  Testing value_result_unwrap_or...\n");

    /* Ok case */
    Value *ok_result = value_result_ok(value_int(42));
    Value *default_val = value_int(0);
    Value *unwrapped = value_result_unwrap_or(ok_result, default_val);
    ASSERT_EQ(42, unwrapped->as.integer);
    value_free(ok_result);
    value_free(default_val);

    /* Err case */
    Value *err_result = value_result_err(value_string("error"));
    Value *default_val2 = value_int(99);
    Value *unwrapped2 = value_result_unwrap_or(err_result, default_val2);
    ASSERT_EQ(99, unwrapped2->as.integer);
    value_free(err_result);
    value_free(default_val2);
}

/* Tool System Tests */

void test_list_tools(void) {
    printf("  Testing list_tools...\n");

    /* list_tools should return an array */
    const char *source = "type(list_tools())";
    ASSERT_STR_EQ("array", run_and_get_string(source));
}

/* Main */

int main(void) {
    printf("Running feature tests...\n\n");

    /* Module/Import Tests */
    printf("Module/Import Tests:\n");
    RUN_TEST(test_import_lexer);
    RUN_TEST(test_fat_arrow);
    RUN_TEST(test_export_parsing);

    /* Result Type Tests */
    printf("\nResult Type Tests:\n");
    RUN_TEST(test_result_ok);
    RUN_TEST(test_result_err);
    RUN_TEST(test_is_ok_is_err);
    RUN_TEST(test_unwrap);
    RUN_TEST(test_unwrap_or);
    RUN_TEST(test_result_in_function);
    RUN_TEST(test_result_chaining);

    /* Match Expression Tests */
    printf("\nMatch Expression Tests:\n");
    RUN_TEST(test_match_parsing);
    RUN_TEST(test_match_err_branch);
    RUN_TEST(test_match_with_function);

    /* Value-Level Tests */
    printf("\nValue-Level Result Tests:\n");
    RUN_TEST(test_value_result_ok);
    RUN_TEST(test_value_result_err);
    RUN_TEST(test_value_result_unwrap_or);

    /* Tool System Tests */
    printf("\nTool System Tests:\n");
    RUN_TEST(test_list_tools);

    return TEST_RESULT();
}
