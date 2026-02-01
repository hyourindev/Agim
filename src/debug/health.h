/*
 * Agim - Health Check Infrastructure
 *
 * Health checks for production monitoring and orchestration.
 * Supports liveness, readiness, and deep health checks.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_HEALTH_H
#define AGIM_HEALTH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Health Status */

typedef enum {
    HEALTH_OK = 0,       /* Healthy */
    HEALTH_DEGRADED = 1, /* Degraded but functional */
    HEALTH_UNHEALTHY = 2,/* Unhealthy */
} HealthStatus;

/* Check Types */

typedef enum {
    CHECK_LIVENESS,  /* Is the process alive? */
    CHECK_READINESS, /* Is the process ready to serve? */
    CHECK_DEEP,      /* Deep health check (more expensive) */
} HealthCheckType;

/* Component Health */

typedef struct ComponentHealth {
    const char *name;
    HealthStatus status;
    const char *message;
    uint64_t last_check_ms;
    uint64_t latency_ms;
} ComponentHealth;

/* Aggregate Health Result */

typedef struct HealthResult {
    HealthStatus status;
    uint64_t timestamp_ms;
    size_t component_count;
    ComponentHealth *components;
} HealthResult;

/* Health Check Callback */

typedef HealthStatus (*HealthCheckFn)(const char **message);

/* Health Check Configuration */

typedef struct HealthConfig {
    bool enabled;
    uint32_t check_interval_ms;  /* How often to run checks */
    uint32_t timeout_ms;         /* Timeout for checks */
    bool cache_results;          /* Cache results */
    uint32_t cache_ttl_ms;       /* Cache TTL */
} HealthConfig;

/* Initialize/Shutdown */

void health_init(const HealthConfig *config);
void health_shutdown(void);
HealthConfig health_config_default(void);

/* Register Health Checks */

bool health_register(const char *name, HealthCheckType type, HealthCheckFn fn);
bool health_unregister(const char *name);

/* Run Health Checks */

HealthResult *health_check_liveness(void);
HealthResult *health_check_readiness(void);
HealthResult *health_check_deep(void);
HealthResult *health_check_all(void);

/* Free result */
void health_result_free(HealthResult *result);

/* Status helpers */
const char *health_status_name(HealthStatus status);
bool health_is_ok(HealthStatus status);

/* Built-in Checks */

/* Scheduler health - are blocks running? */
HealthStatus health_check_scheduler(const char **message);

/* Memory health - memory pressure? */
HealthStatus health_check_memory(const char **message);

/* GC health - is GC keeping up? */
HealthStatus health_check_gc(const char **message);

/* Export as JSON */
char *health_export_json(HealthResult *result);

#endif /* AGIM_HEALTH_H */
