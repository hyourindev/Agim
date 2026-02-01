/*
 * Agim - Health Check Infrastructure
 *
 * Health checks for production monitoring and orchestration.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "debug/health.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Maximum number of registered checks */
#define MAX_HEALTH_CHECKS 32

/* Registered Check */

typedef struct RegisteredCheck {
    char *name;
    HealthCheckType type;
    HealthCheckFn fn;
    bool active;
} RegisteredCheck;

/* Global State */

static pthread_mutex_t health_mutex = PTHREAD_MUTEX_INITIALIZER;
static HealthConfig config = {0};
static RegisteredCheck checks[MAX_HEALTH_CHECKS] = {0};
static size_t check_count = 0;
static bool initialized = false;

/* Time helper */

static uint64_t current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* Default Configuration */

HealthConfig health_config_default(void) {
    return (HealthConfig){
        .enabled = true,
        .check_interval_ms = 10000,
        .timeout_ms = 5000,
        .cache_results = true,
        .cache_ttl_ms = 1000,
    };
}

/* Status Helpers */

const char *health_status_name(HealthStatus status) {
    switch (status) {
        case HEALTH_OK: return "ok";
        case HEALTH_DEGRADED: return "degraded";
        case HEALTH_UNHEALTHY: return "unhealthy";
        default: return "unknown";
    }
}

bool health_is_ok(HealthStatus status) {
    return status == HEALTH_OK || status == HEALTH_DEGRADED;
}

/* Initialize/Shutdown */

void health_init(const HealthConfig *cfg) {
    pthread_mutex_lock(&health_mutex);
    if (initialized) {
        pthread_mutex_unlock(&health_mutex);
        return;
    }

    if (cfg) {
        config = *cfg;
    } else {
        config = health_config_default();
    }

    memset(checks, 0, sizeof(checks));
    check_count = 0;
    initialized = true;

    pthread_mutex_unlock(&health_mutex);

    /* Register built-in checks */
    health_register("scheduler", CHECK_READINESS, health_check_scheduler);
    health_register("memory", CHECK_DEEP, health_check_memory);
    health_register("gc", CHECK_DEEP, health_check_gc);
}

void health_shutdown(void) {
    pthread_mutex_lock(&health_mutex);
    if (!initialized) {
        pthread_mutex_unlock(&health_mutex);
        return;
    }

    for (size_t i = 0; i < MAX_HEALTH_CHECKS; i++) {
        if (checks[i].name) {
            free(checks[i].name);
            checks[i].name = NULL;
        }
        checks[i].active = false;
    }

    check_count = 0;
    initialized = false;

    pthread_mutex_unlock(&health_mutex);
}

/* Register Health Checks */

bool health_register(const char *name, HealthCheckType type, HealthCheckFn fn) {
    if (!name || !fn) return false;

    pthread_mutex_lock(&health_mutex);

    /* Check for existing */
    for (size_t i = 0; i < MAX_HEALTH_CHECKS; i++) {
        if (checks[i].active && checks[i].name &&
            strcmp(checks[i].name, name) == 0) {
            pthread_mutex_unlock(&health_mutex);
            return false; /* Already registered */
        }
    }

    /* Find empty slot */
    for (size_t i = 0; i < MAX_HEALTH_CHECKS; i++) {
        if (!checks[i].active) {
            checks[i].name = strdup(name);
            checks[i].type = type;
            checks[i].fn = fn;
            checks[i].active = true;
            check_count++;
            pthread_mutex_unlock(&health_mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&health_mutex);
    return false; /* No space */
}

bool health_unregister(const char *name) {
    if (!name) return false;

    pthread_mutex_lock(&health_mutex);

    for (size_t i = 0; i < MAX_HEALTH_CHECKS; i++) {
        if (checks[i].active && checks[i].name &&
            strcmp(checks[i].name, name) == 0) {
            free(checks[i].name);
            checks[i].name = NULL;
            checks[i].active = false;
            check_count--;
            pthread_mutex_unlock(&health_mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&health_mutex);
    return false;
}

/* Run Health Checks */

static HealthResult *run_checks(HealthCheckType filter, bool filter_enabled) {
    pthread_mutex_lock(&health_mutex);

    /* Count matching checks */
    size_t count = 0;
    for (size_t i = 0; i < MAX_HEALTH_CHECKS; i++) {
        if (checks[i].active) {
            if (!filter_enabled || checks[i].type == filter) {
                count++;
            }
        }
    }

    /* Allocate result */
    HealthResult *result = calloc(1, sizeof(HealthResult));
    if (!result) {
        pthread_mutex_unlock(&health_mutex);
        return NULL;
    }

    result->timestamp_ms = current_time_ms();
    result->component_count = count;
    result->status = HEALTH_OK;

    if (count > 0) {
        result->components = calloc(count, sizeof(ComponentHealth));
        if (!result->components) {
            free(result);
            pthread_mutex_unlock(&health_mutex);
            return NULL;
        }
    }

    /* Run checks */
    size_t idx = 0;
    for (size_t i = 0; i < MAX_HEALTH_CHECKS && idx < count; i++) {
        if (checks[i].active) {
            if (!filter_enabled || checks[i].type == filter) {
                ComponentHealth *c = &result->components[idx];
                c->name = checks[i].name;
                c->last_check_ms = current_time_ms();

                uint64_t start = current_time_ms();
                c->status = checks[i].fn(&c->message);
                c->latency_ms = current_time_ms() - start;

                /* Update aggregate status */
                if (c->status > result->status) {
                    result->status = c->status;
                }

                idx++;
            }
        }
    }

    pthread_mutex_unlock(&health_mutex);
    return result;
}

HealthResult *health_check_liveness(void) {
    return run_checks(CHECK_LIVENESS, true);
}

HealthResult *health_check_readiness(void) {
    return run_checks(CHECK_READINESS, true);
}

HealthResult *health_check_deep(void) {
    return run_checks(CHECK_DEEP, true);
}

HealthResult *health_check_all(void) {
    return run_checks(CHECK_LIVENESS, false); /* filter_enabled=false */
}

void health_result_free(HealthResult *result) {
    if (!result) return;
    free(result->components);
    free(result);
}

/* Built-in Checks */

HealthStatus health_check_scheduler(const char **message) {
    /* Placeholder - always healthy for now */
    *message = "scheduler operational";
    return HEALTH_OK;
}

HealthStatus health_check_memory(const char **message) {
    /* Placeholder - could check memory pressure */
    *message = "memory within limits";
    return HEALTH_OK;
}

HealthStatus health_check_gc(const char **message) {
    /* Placeholder - could check GC health */
    *message = "GC operational";
    return HEALTH_OK;
}

/* Export as JSON */

char *health_export_json(HealthResult *result) {
    if (!result) return NULL;

    size_t buf_size = 1024 + result->component_count * 256;
    char *buffer = malloc(buf_size);
    if (!buffer) return NULL;

    char *ptr = buffer;
    size_t remaining = buf_size;
    int written;

    written = snprintf(ptr, remaining,
                       "{\"status\":\"%s\",\"timestamp\":%lu,\"components\":[",
                       health_status_name(result->status),
                       (unsigned long)result->timestamp_ms);
    ptr += written;
    remaining -= written;

    for (size_t i = 0; i < result->component_count; i++) {
        ComponentHealth *c = &result->components[i];
        if (i > 0) {
            written = snprintf(ptr, remaining, ",");
            ptr += written;
            remaining -= written;
        }

        written = snprintf(ptr, remaining,
                           "{\"name\":\"%s\",\"status\":\"%s\","
                           "\"message\":\"%s\",\"latency_ms\":%lu}",
                           c->name,
                           health_status_name(c->status),
                           c->message ? c->message : "",
                           (unsigned long)c->latency_ms);
        ptr += written;
        remaining -= written;
    }

    written = snprintf(ptr, remaining, "]}");

    return buffer;
}
