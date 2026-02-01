/*
 * Agim - Health Check Tests
 *
 * Tests for the health check infrastructure.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "debug/health.h"

#include <string.h>

/* Custom health check functions for testing */

static HealthStatus check_always_ok(const char **message) {
    *message = "always ok";
    return HEALTH_OK;
}

static HealthStatus check_always_degraded(const char **message) {
    *message = "degraded state";
    return HEALTH_DEGRADED;
}

static HealthStatus check_always_unhealthy(const char **message) {
    *message = "unhealthy";
    return HEALTH_UNHEALTHY;
}

/* Status Helper Tests */

void test_status_name(void) {
    ASSERT_STR_EQ("ok", health_status_name(HEALTH_OK));
    ASSERT_STR_EQ("degraded", health_status_name(HEALTH_DEGRADED));
    ASSERT_STR_EQ("unhealthy", health_status_name(HEALTH_UNHEALTHY));
}

void test_is_ok(void) {
    ASSERT(health_is_ok(HEALTH_OK));
    ASSERT(health_is_ok(HEALTH_DEGRADED));
    ASSERT(!health_is_ok(HEALTH_UNHEALTHY));
}

/* Registration Tests */

void test_register_check(void) {
    HealthConfig cfg = health_config_default();
    health_init(&cfg);

    /* Built-in checks are registered by health_init */
    ASSERT(health_register("test_check", CHECK_LIVENESS, check_always_ok));

    /* Duplicate should fail */
    ASSERT(!health_register("test_check", CHECK_LIVENESS, check_always_ok));

    health_shutdown();
}

void test_unregister_check(void) {
    HealthConfig cfg = health_config_default();
    health_init(&cfg);

    ASSERT(health_register("removable", CHECK_LIVENESS, check_always_ok));
    ASSERT(health_unregister("removable"));
    ASSERT(!health_unregister("removable")); /* Already removed */

    health_shutdown();
}

/* Check Execution Tests */

void test_liveness_check(void) {
    HealthConfig cfg = health_config_default();
    health_init(&cfg);

    ASSERT(health_register("liveness_test", CHECK_LIVENESS, check_always_ok));

    HealthResult *result = health_check_liveness();
    ASSERT(result != NULL);
    ASSERT_EQ(HEALTH_OK, result->status);
    ASSERT(result->component_count >= 1);

    health_result_free(result);
    health_shutdown();
}

void test_readiness_check(void) {
    HealthConfig cfg = health_config_default();
    health_init(&cfg);

    /* Built-in scheduler check is registered */
    HealthResult *result = health_check_readiness();
    ASSERT(result != NULL);
    ASSERT_EQ(HEALTH_OK, result->status);

    health_result_free(result);
    health_shutdown();
}

void test_aggregate_status(void) {
    HealthConfig cfg = health_config_default();
    health_init(&cfg);

    /* Add checks with different statuses */
    ASSERT(health_register("ok_check", CHECK_LIVENESS, check_always_ok));
    ASSERT(health_register("degraded_check", CHECK_LIVENESS, check_always_degraded));

    HealthResult *result = health_check_liveness();
    ASSERT(result != NULL);
    /* Aggregate should be worst status */
    ASSERT_EQ(HEALTH_DEGRADED, result->status);

    health_result_free(result);
    health_shutdown();
}

void test_unhealthy_aggregate(void) {
    HealthConfig cfg = health_config_default();
    health_init(&cfg);

    ASSERT(health_register("ok_check", CHECK_LIVENESS, check_always_ok));
    ASSERT(health_register("unhealthy_check", CHECK_LIVENESS, check_always_unhealthy));

    HealthResult *result = health_check_liveness();
    ASSERT(result != NULL);
    ASSERT_EQ(HEALTH_UNHEALTHY, result->status);

    health_result_free(result);
    health_shutdown();
}

/* Export Tests */

void test_export_json(void) {
    HealthConfig cfg = health_config_default();
    health_init(&cfg);

    ASSERT(health_register("json_test", CHECK_LIVENESS, check_always_ok));

    HealthResult *result = health_check_liveness();
    ASSERT(result != NULL);

    char *json = health_export_json(result);
    ASSERT(json != NULL);
    ASSERT(strstr(json, "\"status\"") != NULL);
    ASSERT(strstr(json, "\"components\"") != NULL);
    ASSERT(strstr(json, "json_test") != NULL);
    ASSERT(strstr(json, "\"ok\"") != NULL);

    free(json);
    health_result_free(result);
    health_shutdown();
}

void test_check_all(void) {
    HealthConfig cfg = health_config_default();
    health_init(&cfg);

    ASSERT(health_register("all_check_1", CHECK_LIVENESS, check_always_ok));
    ASSERT(health_register("all_check_2", CHECK_READINESS, check_always_ok));
    ASSERT(health_register("all_check_3", CHECK_DEEP, check_always_ok));

    HealthResult *result = health_check_all();
    ASSERT(result != NULL);
    /* Should include all checks plus built-in ones */
    ASSERT(result->component_count >= 3);

    health_result_free(result);
    health_shutdown();
}

/* Main */

int main(void) {
    printf("Running health check tests...\n\n");

    printf("Status Helper Tests:\n");
    RUN_TEST(test_status_name);
    RUN_TEST(test_is_ok);

    printf("\nRegistration Tests:\n");
    RUN_TEST(test_register_check);
    RUN_TEST(test_unregister_check);

    printf("\nCheck Execution Tests:\n");
    RUN_TEST(test_liveness_check);
    RUN_TEST(test_readiness_check);
    RUN_TEST(test_aggregate_status);
    RUN_TEST(test_unhealthy_aggregate);

    printf("\nExport Tests:\n");
    RUN_TEST(test_export_json);
    RUN_TEST(test_check_all);

    return TEST_RESULT();
}
