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

/*============================================================================
 * Capability Definitions
 *============================================================================*/

/**
 * Individual capabilities that can be granted to blocks.
 * Each capability is a single bit flag.
 */
typedef enum Capability {
    CAP_NONE        = 0,
    CAP_SPAWN       = 1 << 0,   /* Can create new blocks */
    CAP_SEND        = 1 << 1,   /* Can send messages */
    CAP_RECEIVE     = 1 << 2,   /* Can receive messages */
    CAP_INFER       = 1 << 3,   /* Can call LLM inference */
    CAP_HTTP        = 1 << 4,   /* Can make HTTP requests */
    CAP_FILE_READ   = 1 << 5,   /* Can read files */
    CAP_FILE_WRITE  = 1 << 6,   /* Can write files */
    CAP_DB          = 1 << 7,   /* Can access databases */
    CAP_MEMORY      = 1 << 8,   /* Can use persistent memory */
    CAP_LINK        = 1 << 9,   /* Can link to other blocks */
    CAP_SHELL       = 1 << 10,  /* Can execute shell commands */
    CAP_EXEC        = 1 << 11,  /* Can execute processes */
    CAP_ALL         = 0xFFF,    /* All defined capabilities */
} Capability;

/**
 * A set of capabilities (bitmask).
 */
typedef uint32_t CapabilitySet;

/*============================================================================
 * Capability Names (for debugging)
 *============================================================================*/

/**
 * Get the name of a capability.
 */
const char *capability_name(Capability cap);

#endif /* AGIM_RUNTIME_CAPABILITY_H */
