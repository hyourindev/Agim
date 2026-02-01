/*
 * Agim - Metrics Tests
 *
 * Tests for the metrics infrastructure.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "../test_common.h"
#include "debug/metrics.h"

#include <string.h>
#include <math.h>

/* Counter Tests */

void test_counter_inc(void) {
    MetricsConfig cfg = metrics_config_default();
    metrics_init(&cfg);

    metric_counter_inc("test_counter", 1);
    ASSERT_EQ(1, metric_counter_get("test_counter"));

    metric_counter_inc("test_counter", 5);
    ASSERT_EQ(6, metric_counter_get("test_counter"));

    metrics_shutdown();
}

void test_counter_multiple(void) {
    MetricsConfig cfg = metrics_config_default();
    metrics_init(&cfg);

    metric_counter_add("counter_a", "Counter A", 10);
    metric_counter_add("counter_b", "Counter B", 20);

    ASSERT_EQ(10, metric_counter_get("counter_a"));
    ASSERT_EQ(20, metric_counter_get("counter_b"));

    metrics_shutdown();
}

/* Gauge Tests */

void test_gauge_set(void) {
    MetricsConfig cfg = metrics_config_default();
    metrics_init(&cfg);

    metric_gauge_set("test_gauge", 42.5);
    ASSERT(fabs(metric_gauge_get("test_gauge") - 42.5) < 0.001);

    metric_gauge_set("test_gauge", 100.0);
    ASSERT(fabs(metric_gauge_get("test_gauge") - 100.0) < 0.001);

    metrics_shutdown();
}

void test_gauge_inc_dec(void) {
    MetricsConfig cfg = metrics_config_default();
    metrics_init(&cfg);

    metric_gauge_set("active_count", 5.0);
    metric_gauge_inc("active_count");
    ASSERT(fabs(metric_gauge_get("active_count") - 6.0) < 0.001);

    metric_gauge_dec("active_count");
    metric_gauge_dec("active_count");
    ASSERT(fabs(metric_gauge_get("active_count") - 4.0) < 0.001);

    metrics_shutdown();
}

/* Histogram Tests */

void test_histogram_observe(void) {
    MetricsConfig cfg = metrics_config_default();
    metrics_init(&cfg);

    metric_histogram_observe("latency", 5.0);
    metric_histogram_observe("latency", 10.0);
    metric_histogram_observe("latency", 100.0);

    HistogramData h = metric_histogram_get("latency");
    ASSERT_EQ(3, h.count);
    ASSERT(fabs(h.sum - 115.0) < 0.001);
    ASSERT(fabs(h.min - 5.0) < 0.001);
    ASSERT(fabs(h.max - 100.0) < 0.001);

    metrics_shutdown();
}

/* Export Tests */

void test_export_prometheus(void) {
    MetricsConfig cfg = metrics_config_default();
    metrics_init(&cfg);

    metric_counter_add("requests_total", "Total requests", 100);
    metric_gauge_add("temperature", "Current temperature", 23.5);

    char *output = metrics_export_prometheus();
    ASSERT(output != NULL);
    ASSERT(strstr(output, "requests_total") != NULL);
    ASSERT(strstr(output, "temperature") != NULL);
    ASSERT(strstr(output, "counter") != NULL);
    ASSERT(strstr(output, "gauge") != NULL);

    free(output);
    metrics_shutdown();
}

void test_export_json(void) {
    MetricsConfig cfg = metrics_config_default();
    metrics_init(&cfg);

    metric_counter_add("api_calls", "API calls", 50);

    char *output = metrics_export_json();
    ASSERT(output != NULL);
    ASSERT(strstr(output, "\"metrics\"") != NULL);
    ASSERT(strstr(output, "api_calls") != NULL);
    ASSERT(strstr(output, "\"counter\"") != NULL);

    free(output);
    metrics_shutdown();
}

/* Registry Tests */

void test_registry_find(void) {
    MetricsConfig cfg = metrics_config_default();
    metrics_init(&cfg);

    metric_counter_inc("find_me", 1);

    Metric *m = metrics_find("find_me");
    ASSERT(m != NULL);
    ASSERT_STR_EQ("find_me", m->name);
    ASSERT_EQ(METRIC_COUNTER, m->type);

    Metric *not_found = metrics_find("not_exists");
    ASSERT(not_found == NULL);

    metrics_shutdown();
}

void test_metrics_disabled(void) {
    MetricsConfig cfg = metrics_config_default();
    cfg.enabled = false;
    metrics_init(&cfg);

    metric_counter_inc("disabled_counter", 100);
    /* Should not record when disabled */
    ASSERT_EQ(0, metric_counter_get("disabled_counter"));

    metrics_shutdown();
}

/* Main */

int main(void) {
    printf("Running metrics tests...\n\n");

    printf("Counter Tests:\n");
    RUN_TEST(test_counter_inc);
    RUN_TEST(test_counter_multiple);

    printf("\nGauge Tests:\n");
    RUN_TEST(test_gauge_set);
    RUN_TEST(test_gauge_inc_dec);

    printf("\nHistogram Tests:\n");
    RUN_TEST(test_histogram_observe);

    printf("\nExport Tests:\n");
    RUN_TEST(test_export_prometheus);
    RUN_TEST(test_export_json);

    printf("\nRegistry Tests:\n");
    RUN_TEST(test_registry_find);
    RUN_TEST(test_metrics_disabled);

    return TEST_RESULT();
}
