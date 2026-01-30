/*
 * Agim - URL Parser
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_NET_URL_H
#define AGIM_NET_URL_H

#include <stdbool.h>
#include <stdint.h>

/*============================================================================
 * Parsed URL Structure
 *============================================================================*/

typedef struct {
    char *scheme;       /* "http" or "https" */
    char *host;         /* Hostname or IP address */
    uint16_t port;      /* Port number (default: 80 for http, 443 for https) */
    char *path;         /* Path including leading slash (default: "/") */
    char *query;        /* Query string without '?' (NULL if none) */
    bool is_https;      /* Convenience flag */
} ParsedURL;

/*============================================================================
 * API
 *============================================================================*/

/**
 * Parse a URL string into components.
 * Returns NULL on parse error.
 * Caller must free with url_free().
 */
ParsedURL *url_parse(const char *url);

/**
 * Free a parsed URL structure.
 */
void url_free(ParsedURL *url);

/**
 * Build a Host header value from parsed URL.
 * Returns allocated string, caller must free.
 * Format: "host" or "host:port" (if non-default port)
 */
char *url_host_header(const ParsedURL *url);

/**
 * Build the request path (path + query).
 * Returns allocated string, caller must free.
 * Format: "/path" or "/path?query"
 */
char *url_request_path(const ParsedURL *url);

#endif /* AGIM_NET_URL_H */
