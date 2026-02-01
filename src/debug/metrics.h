/*
 * Agim - Metrics Infrastructure
 *
 * Thread-safe metrics collection for production monitoring.
 * Supports counters, gauges, and histograms.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_METRICS_H
#define AGIM_METRICS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Metric Types */

typedef enum {
    METRIC_COUNTER,     /* Monotonically increasing value */
    METRIC_GAUGE,       /* Value that can go up or down */
    METRIC_HISTOGRAM,   /* Distribution of values */
} MetricType;

/* Histogram buckets for latency metrics (in microseconds) */
#define HISTOGRAM_BUCKET_COUNT 12
extern const double HISTOGRAM_BUCKETS[HISTOGRAM_BUCKET_COUNT];

/* Histogram data */
typedef struct {
    uint64_t buckets[HISTOGRAM_BUCKET_COUNT];
    uint64_t count;
    double sum;
    double min;
    double max;
} HistogramData;

/* Metric value union */
typedef union {
    uint64_t counter;
    double gauge;
    HistogramData histogram;
} MetricValue;

/* Single metric */
typedef struct Metric {
    char *name;
    char *description;
    MetricType type;
    MetricValue value;
    struct Metric *next;
} Metric;

/* Metrics Registry */

typedef struct MetricsRegistry {
    Metric *head;
    size_t count;
} MetricsRegistry;

/* Metrics Configuration */

typedef struct MetricsConfig {
    bool enabled;               /* Whether metrics are enabled */
    bool expose_prometheus;     /* Expose Prometheus endpoint */
    uint16_t prometheus_port;   /* Port for Prometheus (default 9090) */
    uint32_t export_interval_ms;/* Export interval in milliseconds */
} MetricsConfig;

/* Initialize/Shutdown */

void metrics_init(const MetricsConfig *config);
void metrics_shutdown(void);
MetricsConfig metrics_config_default(void);

/* Counter Operations */

void metric_counter_inc(const char *name, uint64_t value);
void metric_counter_add(const char *name, const char *desc, uint64_t value);
uint64_t metric_counter_get(const char *name);

/* Gauge Operations */

void metric_gauge_set(const char *name, double value);
void metric_gauge_add(const char *name, const char *desc, double value);
void metric_gauge_inc(const char *name);
void metric_gauge_dec(const char *name);
double metric_gauge_get(const char *name);

/* Histogram Operations */

void metric_histogram_observe(const char *name, double value);
void metric_histogram_add(const char *name, const char *desc, double value);
HistogramData metric_histogram_get(const char *name);

/* Registry Access */

MetricsRegistry *metrics_get_registry(void);
Metric *metrics_find(const char *name);

/* Export Functions */

/* Get all metrics as Prometheus text format */
char *metrics_export_prometheus(void);

/* Get all metrics as JSON format */
char *metrics_export_json(void);

/* Built-in Metrics Names */

/* Scheduler metrics */
#define METRIC_BLOCKS_SPAWNED     "agim_blocks_spawned_total"
#define METRIC_BLOCKS_TERMINATED  "agim_blocks_terminated_total"
#define METRIC_BLOCKS_ACTIVE      "agim_blocks_active"
#define METRIC_CONTEXT_SWITCHES   "agim_context_switches_total"
#define METRIC_MESSAGES_SENT      "agim_messages_sent_total"
#define METRIC_MESSAGES_RECEIVED  "agim_messages_received_total"

/* GC metrics */
#define METRIC_GC_COLLECTIONS     "agim_gc_collections_total"
#define METRIC_GC_BYTES_ALLOCATED "agim_gc_bytes_allocated"
#define METRIC_GC_BYTES_FREED     "agim_gc_bytes_freed_total"
#define METRIC_GC_PAUSE_MS        "agim_gc_pause_milliseconds"

/* Worker metrics */
#define METRIC_WORKERS_ACTIVE     "agim_workers_active"
#define METRIC_WORK_STEALS        "agim_work_steals_total"

/* Memory metrics */
#define METRIC_HEAP_SIZE          "agim_heap_bytes"
#define METRIC_HEAP_USED          "agim_heap_used_bytes"

#endif /* AGIM_METRICS_H */
