/*
 * Agim - HTTP Client
 *
 * HTTP/HTTPS client with SSRF protection.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_NET_HTTP_H
#define AGIM_NET_HTTP_H

#include <stdbool.h>
#include <stddef.h>

/* HTTP Response */

typedef struct HttpResponse {
    long status_code;       /* HTTP status code (200, 404, etc.) */
    char *body;             /* Response body (null-terminated) */
    size_t body_len;        /* Length of body */
    char *content_type;     /* Content-Type header value */
    char *error;            /* Error message if request failed */
} HttpResponse;

/* HTTP Streaming */

typedef struct HttpStream HttpStream;

/**
 * Callback for streaming responses.
 * Called for each chunk of data received.
 * Return false to abort the stream.
 */
typedef bool (*HttpStreamCallback)(const char *chunk, size_t len, void *ctx);

/* HTTP Client Lifecycle */

/**
 * Initialize the HTTP client (call once at startup).
 * Returns true on success.
 */
bool http_init(void);

/**
 * Cleanup the HTTP client (call once at shutdown).
 */
void http_cleanup(void);

/* Synchronous Requests */

/**
 * Perform an HTTP GET request.
 * Returns a response that must be freed with http_response_free().
 * Returns NULL on fatal error (memory allocation).
 */
HttpResponse *http_get(const char *url);

/**
 * Perform an HTTP POST request.
 * Returns a response that must be freed with http_response_free().
 * Returns NULL on fatal error (memory allocation).
 */
HttpResponse *http_post(const char *url, const char *body, const char *content_type);

/**
 * Perform an HTTP POST request with custom headers.
 * headers is an array of "Name: Value" strings, terminated by NULL.
 */
HttpResponse *http_post_with_headers(const char *url, const char *body,
                                      const char **headers);

/**
 * Perform an HTTP PUT request.
 */
HttpResponse *http_put(const char *url, const char *body, const char *content_type);

/**
 * Perform an HTTP DELETE request.
 */
HttpResponse *http_delete(const char *url);

/**
 * Perform an HTTP PATCH request.
 */
HttpResponse *http_patch(const char *url, const char *body, const char *content_type);

/**
 * Perform a generic HTTP request with custom method and headers.
 * headers is an array of "Name: Value" strings, terminated by NULL.
 */
HttpResponse *http_request_generic(const char *method, const char *url,
                                    const char *body, const char **headers);

/**
 * Free an HTTP response.
 */
void http_response_free(HttpResponse *resp);

/* Streaming Requests */

/**
 * Start a streaming HTTP GET request.
 * Returns a stream handle, or NULL on error.
 */
HttpStream *http_stream_get(const char *url);

/**
 * Start a streaming HTTP POST request.
 * Returns a stream handle, or NULL on error.
 */
HttpStream *http_stream_post(const char *url, const char *body, const char *content_type);

/**
 * Start a streaming HTTP POST with custom headers.
 */
HttpStream *http_stream_post_with_headers(const char *url, const char *body,
                                           const char **headers);

/**
 * Read the next chunk from a stream.
 * Returns the chunk data (must be freed by caller), or NULL if no data available.
 * Sets *len to the chunk length.
 * Check http_stream_done() to see if stream is complete.
 */
char *http_stream_read(HttpStream *stream, size_t *len);

/**
 * Check if the stream is complete (no more data will arrive).
 */
bool http_stream_done(HttpStream *stream);

/**
 * Check if the stream encountered an error.
 */
bool http_stream_error(HttpStream *stream);

/**
 * Get the error message from a stream (if any).
 */
const char *http_stream_error_msg(HttpStream *stream);

/**
 * Get the HTTP status code from a stream (available after headers received).
 * Returns 0 if not yet available.
 */
long http_stream_status(HttpStream *stream);

/**
 * Close and free a stream.
 */
void http_stream_close(HttpStream *stream);

/* URL Validation */

/**
 * Check if a URL is valid and safe to request.
 * Rejects file://, localhost, private IPs, etc. for security.
 * Set allow_private to true to allow localhost/private IPs.
 */
bool http_url_valid(const char *url, bool allow_private);

/**
 * Escape a string for use in a URL query parameter.
 * Returns a newly allocated string that must be freed.
 */
char *http_url_encode(const char *str);

#endif /* AGIM_NET_HTTP_H */
