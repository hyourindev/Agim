/*
 * Agim - Capability-Based Security
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "runtime/capability.h"

const char *capability_name(Capability cap) {
    switch (cap) {
    case CAP_NONE:       return "NONE";
    case CAP_SPAWN:      return "SPAWN";
    case CAP_SEND:       return "SEND";
    case CAP_RECEIVE:    return "RECEIVE";
    case CAP_INFER:      return "INFER";
    case CAP_HTTP:       return "HTTP";
    case CAP_FILE_READ:  return "FILE_READ";
    case CAP_FILE_WRITE: return "FILE_WRITE";
    case CAP_DB:         return "DB";
    case CAP_MEMORY:     return "MEMORY";
    case CAP_LINK:       return "LINK";
    case CAP_SHELL:      return "SHELL";
    case CAP_EXEC:       return "EXEC";
    case CAP_ALL:        return "ALL";
    default:             return "UNKNOWN";
    }
}
