/*
 * Agim - Metrics Infrastructure
 *
 * Thread-safe metrics collection for production monitoring.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "debug/metrics.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Histogram bucket boundaries (in microseconds) */
const double HISTOGRAM_BUCKETS[HISTOGRAM_BUCKET_COUNT] = {
    10,      /* 10us */
    50,      /* 50us */
    100,     /* 100us */
    500,     /* 500us */
    1000,    /* 1ms */
    5000,    /* 5ms */
    10000,   /* 10ms */
    50000,   /* 50ms */
    100000,  /* 100ms */
    500000,  /* 500ms */
    1000000, /* 1s */
    5000000, /* 5s */
};

/* Global State */

static pthread_mutex_t metrics_mutex = PTHREAD_MUTEX_INITIALIZER;
static MetricsRegistry registry = {0};
static MetricsConfig config = {0};
static bool initialized = false;

/* Default Configuration */

MetricsConfig metrics_config_default(void) {
    return (MetricsConfig){
        .enabled = true,
        .expose_prometheus = false,
        .prometheus_port = 9090,
        .export_interval_ms = 10000,
    };
}

/* Initialize/Shutdown */

void metrics_init(const MetricsConfig *cfg) {
    pthread_mutex_lock(&metrics_mutex);
    if (initialized) {
        pthread_mutex_unlock(&metrics_mutex);
        return;
    }

    if (cfg) {
        config = *cfg;
    } else {
        config = metrics_config_default();
    }

    registry.head = NULL;
    registry.count = 0;
    initialized = true;

    pthread_mutex_unlock(&metrics_mutex);
}

void metrics_shutdown(void) {
    pthread_mutex_lock(&metrics_mutex);
    if (!initialized) {
        pthread_mutex_unlock(&metrics_mutex);
        return;
    }

    /* Free all metrics */
    Metric *current = registry.head;
    while (current) {
        Metric *next = current->next;
        free(current->name);
        free(current->description);
        free(current);
        current = next;
    }

    registry.head = NULL;
    registry.count = 0;
    initialized = false;

    pthread_mutex_unlock(&metrics_mutex);
}

/* Internal: Find or create metric */

static Metric *find_or_create_metric(const char *name, const char *desc, MetricType type) {
    /* Search existing */
    for (Metric *m = registry.head; m != NULL; m = m->next) {
        if (strcmp(m->name, name) == 0) {
            return m;
        }
    }

    /* Create new */
    Metric *m = calloc(1, sizeof(Metric));
    if (!m) return NULL;

    m->name = strdup(name);
    m->description = desc ? strdup(desc) : strdup("");
    m->type = type;

    /* Initialize histogram min to infinity */
    if (type == METRIC_HISTOGRAM) {
        m->value.histogram.min = INFINITY;
        m->value.histogram.max = -INFINITY;
    }

    m->next = registry.head;
    registry.head = m;
    registry.count++;

    return m;
}

/* Counter Operations */

void metric_counter_inc(const char *name, uint64_t value) {
    metric_counter_add(name, NULL, value);
}

void metric_counter_add(const char *name, const char *desc, uint64_t value) {
    if (!config.enabled) return;

    pthread_mutex_lock(&metrics_mutex);
    Metric *m = find_or_create_metric(name, desc, METRIC_COUNTER);
    if (m) {
        m->value.counter += value;
    }
    pthread_mutex_unlock(&metrics_mutex);
}

uint64_t metric_counter_get(const char *name) {
    pthread_mutex_lock(&metrics_mutex);
    Metric *m = metrics_find(name);
    uint64_t result = (m && m->type == METRIC_COUNTER) ? m->value.counter : 0;
    pthread_mutex_unlock(&metrics_mutex);
    return result;
}

/* Gauge Operations */

void metric_gauge_set(const char *name, double value) {
    metric_gauge_add(name, NULL, value);
}

void metric_gauge_add(const char *name, const char *desc, double value) {
    if (!config.enabled) return;

    pthread_mutex_lock(&metrics_mutex);
    Metric *m = find_or_create_metric(name, desc, METRIC_GAUGE);
    if (m) {
        m->value.gauge = value;
    }
    pthread_mutex_unlock(&metrics_mutex);
}

void metric_gauge_inc(const char *name) {
    if (!config.enabled) return;

    pthread_mutex_lock(&metrics_mutex);
    Metric *m = find_or_create_metric(name, NULL, METRIC_GAUGE);
    if (m) {
        m->value.gauge += 1.0;
    }
    pthread_mutex_unlock(&metrics_mutex);
}

void metric_gauge_dec(const char *name) {
    if (!config.enabled) return;

    pthread_mutex_lock(&metrics_mutex);
    Metric *m = find_or_create_metric(name, NULL, METRIC_GAUGE);
    if (m) {
        m->value.gauge -= 1.0;
    }
    pthread_mutex_unlock(&metrics_mutex);
}

double metric_gauge_get(const char *name) {
    pthread_mutex_lock(&metrics_mutex);
    Metric *m = metrics_find(name);
    double result = (m && m->type == METRIC_GAUGE) ? m->value.gauge : 0.0;
    pthread_mutex_unlock(&metrics_mutex);
    return result;
}

/* Histogram Operations */

void metric_histogram_observe(const char *name, double value) {
    metric_histogram_add(name, NULL, value);
}

void metric_histogram_add(const char *name, const char *desc, double value) {
    if (!config.enabled) return;

    pthread_mutex_lock(&metrics_mutex);
    Metric *m = find_or_create_metric(name, desc, METRIC_HISTOGRAM);
    if (m) {
        HistogramData *h = &m->value.histogram;

        /* Update buckets */
        for (int i = 0; i < HISTOGRAM_BUCKET_COUNT; i++) {
            if (value <= HISTOGRAM_BUCKETS[i]) {
                h->buckets[i]++;
            }
        }

        /* Update stats */
        h->count++;
        h->sum += value;
        if (value < h->min) h->min = value;
        if (value > h->max) h->max = value;
    }
    pthread_mutex_unlock(&metrics_mutex);
}

HistogramData metric_histogram_get(const char *name) {
    HistogramData result = {0};
    result.min = INFINITY;
    result.max = -INFINITY;

    pthread_mutex_lock(&metrics_mutex);
    Metric *m = metrics_find(name);
    if (m && m->type == METRIC_HISTOGRAM) {
        result = m->value.histogram;
    }
    pthread_mutex_unlock(&metrics_mutex);
    return result;
}

/* Registry Access */

MetricsRegistry *metrics_get_registry(void) {
    return &registry;
}

Metric *metrics_find(const char *name) {
    for (Metric *m = registry.head; m != NULL; m = m->next) {
        if (strcmp(m->name, name) == 0) {
            return m;
        }
    }
    return NULL;
}

/* Export: Prometheus Format */

char *metrics_export_prometheus(void) {
    pthread_mutex_lock(&metrics_mutex);

    /* Estimate buffer size */
    size_t buf_size = 4096 + registry.count * 256;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        pthread_mutex_unlock(&metrics_mutex);
        return NULL;
    }

    char *ptr = buffer;
    size_t remaining = buf_size;
    int written;

    for (Metric *m = registry.head; m != NULL; m = m->next) {
        /* Write HELP line */
        if (m->description && m->description[0]) {
            written = snprintf(ptr, remaining, "# HELP %s %s\n",
                               m->name, m->description);
            ptr += written;
            remaining -= written;
        }

        /* Write TYPE line */
        const char *type_str = "untyped";
        switch (m->type) {
            case METRIC_COUNTER: type_str = "counter"; break;
            case METRIC_GAUGE: type_str = "gauge"; break;
            case METRIC_HISTOGRAM: type_str = "histogram"; break;
        }
        written = snprintf(ptr, remaining, "# TYPE %s %s\n", m->name, type_str);
        ptr += written;
        remaining -= written;

        /* Write value(s) */
        switch (m->type) {
            case METRIC_COUNTER:
                written = snprintf(ptr, remaining, "%s %lu\n",
                                   m->name, (unsigned long)m->value.counter);
                ptr += written;
                remaining -= written;
                break;

            case METRIC_GAUGE:
                written = snprintf(ptr, remaining, "%s %g\n",
                                   m->name, m->value.gauge);
                ptr += written;
                remaining -= written;
                break;

            case METRIC_HISTOGRAM: {
                HistogramData *h = &m->value.histogram;
                uint64_t cumulative = 0;
                for (int i = 0; i < HISTOGRAM_BUCKET_COUNT; i++) {
                    cumulative += h->buckets[i];
                    written = snprintf(ptr, remaining,
                                       "%s_bucket{le=\"%g\"} %lu\n",
                                       m->name, HISTOGRAM_BUCKETS[i],
                                       (unsigned long)cumulative);
                    ptr += written;
                    remaining -= written;
                }
                /* +Inf bucket */
                written = snprintf(ptr, remaining,
                                   "%s_bucket{le=\"+Inf\"} %lu\n",
                                   m->name, (unsigned long)h->count);
                ptr += written;
                remaining -= written;

                /* Sum and count */
                written = snprintf(ptr, remaining, "%s_sum %g\n",
                                   m->name, h->sum);
                ptr += written;
                remaining -= written;

                written = snprintf(ptr, remaining, "%s_count %lu\n",
                                   m->name, (unsigned long)h->count);
                ptr += written;
                remaining -= written;
                break;
            }
        }
    }

    pthread_mutex_unlock(&metrics_mutex);
    return buffer;
}

/* Export: JSON Format */

char *metrics_export_json(void) {
    pthread_mutex_lock(&metrics_mutex);

    /* Estimate buffer size */
    size_t buf_size = 4096 + registry.count * 512;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        pthread_mutex_unlock(&metrics_mutex);
        return NULL;
    }

    char *ptr = buffer;
    size_t remaining = buf_size;
    int written;

    written = snprintf(ptr, remaining, "{\"metrics\":[");
    ptr += written;
    remaining -= written;

    bool first = true;
    for (Metric *m = registry.head; m != NULL; m = m->next) {
        if (!first) {
            written = snprintf(ptr, remaining, ",");
            ptr += written;
            remaining -= written;
        }
        first = false;

        written = snprintf(ptr, remaining,
                           "{\"name\":\"%s\",\"type\":\"%s\",",
                           m->name,
                           m->type == METRIC_COUNTER ? "counter" :
                           m->type == METRIC_GAUGE ? "gauge" : "histogram");
        ptr += written;
        remaining -= written;

        switch (m->type) {
            case METRIC_COUNTER:
                written = snprintf(ptr, remaining, "\"value\":%lu}",
                                   (unsigned long)m->value.counter);
                break;

            case METRIC_GAUGE:
                written = snprintf(ptr, remaining, "\"value\":%g}",
                                   m->value.gauge);
                break;

            case METRIC_HISTOGRAM: {
                HistogramData *h = &m->value.histogram;
                written = snprintf(ptr, remaining,
                                   "\"count\":%lu,\"sum\":%g,\"min\":%g,\"max\":%g}",
                                   (unsigned long)h->count, h->sum,
                                   isinf(h->min) ? 0.0 : h->min,
                                   isinf(h->max) ? 0.0 : h->max);
                break;
            }
        }
        ptr += written;
        remaining -= written;
    }

    written = snprintf(ptr, remaining, "]}");
    ptr += written;

    pthread_mutex_unlock(&metrics_mutex);
    return buffer;
}
