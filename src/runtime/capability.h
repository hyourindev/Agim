/*
 * Agim - Capability-Based Security
 *
 * Defines the capability model for controlling block permissions.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_RUNTIME_CAPABILITY_H
#define AGIM_RUNTIME_CAPABILITY_H

#include <stdint.h>

/* Capability Definitions */

typedef enum Capability {
    CAP_NONE        = 0,
    CAP_SPAWN       = 1 << 0,
    CAP_SEND        = 1 << 1,
    CAP_RECEIVE     = 1 << 2,
    CAP_INFER       = 1 << 3,
    CAP_HTTP        = 1 << 4,
    CAP_FILE_READ   = 1 << 5,
    CAP_FILE_WRITE  = 1 << 6,
    CAP_DB          = 1 << 7,
    CAP_MEMORY      = 1 << 8,
    CAP_LINK        = 1 << 9,
    CAP_SHELL       = 1 << 10,
    CAP_EXEC        = 1 << 11,
    CAP_TRAP_EXIT   = 1 << 12,
    CAP_MONITOR     = 1 << 13,
    CAP_SUPERVISE   = 1 << 14,
    CAP_ALL         = 0x7FFF,
} Capability;

typedef uint32_t CapabilitySet;

const char *capability_name(Capability cap);

#endif /* AGIM_RUNTIME_CAPABILITY_H */
