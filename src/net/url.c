/*
 * Agim - URL Parser
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "net/url.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Helper Functions
 *============================================================================*/

static char *strndup_safe(const char *s, size_t n) {
    if (!s) return NULL;
    char *result = malloc(n + 1);
    if (!result) return NULL;
    memcpy(result, s, n);
    result[n] = '\0';
    return result;
}

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    return strndup_safe(s, strlen(s));
}

/*============================================================================
 * URL Parsing
 *============================================================================*/

ParsedURL *url_parse(const char *url) {
    if (!url || !*url) return NULL;

    ParsedURL *parsed = calloc(1, sizeof(ParsedURL));
    if (!parsed) return NULL;

    const char *p = url;

    /* Parse scheme */
    const char *scheme_end = strstr(p, "://");
    if (!scheme_end) {
        url_free(parsed);
        return NULL;
    }

    size_t scheme_len = (size_t)(scheme_end - p);
    parsed->scheme = strndup_safe(p, scheme_len);
    if (!parsed->scheme) {
        url_free(parsed);
        return NULL;
    }

    /* Convert scheme to lowercase */
    for (char *s = parsed->scheme; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }

    /* Validate scheme */
    if (strcmp(parsed->scheme, "http") == 0) {
        parsed->is_https = false;
        parsed->port = 80;
    } else if (strcmp(parsed->scheme, "https") == 0) {
        parsed->is_https = true;
        parsed->port = 443;
    } else {
        url_free(parsed);
        return NULL;
    }

    p = scheme_end + 3; /* Skip "://" */

    /* Parse host (and optional port) */
    const char *host_start = p;
    const char *host_end;
    const char *port_start = NULL;

    /* Handle IPv6 addresses in brackets: [::1] */
    if (*p == '[') {
        const char *bracket_end = strchr(p, ']');
        if (!bracket_end) {
            url_free(parsed);
            return NULL;
        }
        host_end = bracket_end + 1;
        host_start = p + 1; /* Skip '[' */

        /* Extract host without brackets */
        size_t host_len = (size_t)(bracket_end - host_start);
        parsed->host = strndup_safe(host_start, host_len);

        p = bracket_end + 1;
        if (*p == ':') {
            port_start = p + 1;
        }
    } else {
        /* Regular hostname or IPv4 */
        host_end = p;
        while (*host_end && *host_end != ':' && *host_end != '/' && *host_end != '?') {
            host_end++;
        }

        if (host_end == p) {
            url_free(parsed);
            return NULL; /* Empty host */
        }

        parsed->host = strndup_safe(p, (size_t)(host_end - p));

        p = host_end;
        if (*p == ':') {
            port_start = p + 1;
        }
    }

    if (!parsed->host) {
        url_free(parsed);
        return NULL;
    }

    /* Parse port if present */
    if (port_start) {
        char *port_end;
        long port = strtol(port_start, &port_end, 10);
        if (port_end == port_start || port < 1 || port > 65535) {
            url_free(parsed);
            return NULL;
        }
        parsed->port = (uint16_t)port;
        p = port_end;
    }

    /* Parse path */
    if (*p == '/') {
        const char *path_end = p;
        while (*path_end && *path_end != '?') {
            path_end++;
        }
        parsed->path = strndup_safe(p, (size_t)(path_end - p));
        p = path_end;
    } else {
        parsed->path = strdup_safe("/");
    }

    if (!parsed->path) {
        url_free(parsed);
        return NULL;
    }

    /* Parse query string */
    if (*p == '?') {
        p++; /* Skip '?' */
        if (*p) {
            parsed->query = strdup_safe(p);
        }
    }

    return parsed;
}

void url_free(ParsedURL *url) {
    if (!url) return;
    free(url->scheme);
    free(url->host);
    free(url->path);
    free(url->query);
    free(url);
}

char *url_host_header(const ParsedURL *url) {
    if (!url || !url->host) return NULL;

    /* Check if we need to include port */
    bool need_port = false;
    if (url->is_https && url->port != 443) need_port = true;
    if (!url->is_https && url->port != 80) need_port = true;

    size_t len = strlen(url->host) + (need_port ? 7 : 1); /* :65535 + null */
    char *result = malloc(len);
    if (!result) return NULL;

    if (need_port) {
        snprintf(result, len, "%s:%u", url->host, url->port);
    } else {
        snprintf(result, len, "%s", url->host);
    }

    return result;
}

char *url_request_path(const ParsedURL *url) {
    if (!url || !url->path) return NULL;

    size_t path_len = strlen(url->path);
    size_t query_len = url->query ? strlen(url->query) : 0;
    size_t total_len = path_len + (query_len > 0 ? 1 + query_len : 0) + 1;

    char *result = malloc(total_len);
    if (!result) return NULL;

    if (query_len > 0) {
        snprintf(result, total_len, "%s?%s", url->path, url->query);
    } else {
        snprintf(result, total_len, "%s", url->path);
    }

    return result;
}
