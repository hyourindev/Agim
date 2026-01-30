/*
 * Agim - HTTP Response Parser
 *
 * Incremental HTTP/1.1 response parser supporting chunked transfer encoding.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_NET_HTTP_PARSER_H
#define AGIM_NET_HTTP_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/*============================================================================
 * Types
 *============================================================================*/

typedef enum {
    HTTP_PARSE_NEED_MORE,       /* Need more data */
    HTTP_PARSE_HEADERS_DONE,    /* Headers complete, body may follow */
    HTTP_PARSE_CHUNK_READY,     /* A body chunk is ready */
    HTTP_PARSE_DONE,            /* Response complete */
    HTTP_PARSE_ERROR            /* Parse error */
} HttpParseResult;

typedef struct HttpHeader {
    char *name;
    char *value;
} HttpHeader;

typedef struct HttpParser HttpParser;

/*============================================================================
 * Parser Lifecycle
 *============================================================================*/

/**
 * Create a new HTTP parser.
 */
HttpParser *http_parser_new(void);

/**
 * Free an HTTP parser.
 */
void http_parser_free(HttpParser *parser);

/**
 * Reset parser for reuse.
 */
void http_parser_reset(HttpParser *parser);

/*============================================================================
 * Parsing
 *============================================================================*/

/**
 * Feed data to the parser.
 *
 * @param parser    Parser instance
 * @param data      Data to parse
 * @param len       Length of data
 * @param consumed  Output: bytes consumed (may be less than len)
 * @return          Parse result
 */
HttpParseResult http_parser_feed(HttpParser *parser, const char *data, size_t len, size_t *consumed);

/*============================================================================
 * Accessors (valid after HTTP_PARSE_HEADERS_DONE)
 *============================================================================*/

/**
 * Get HTTP status code (e.g., 200).
 */
int http_parser_status_code(const HttpParser *parser);

/**
 * Get HTTP status text (e.g., "OK").
 */
const char *http_parser_status_text(const HttpParser *parser);

/**
 * Get number of headers.
 */
size_t http_parser_header_count(const HttpParser *parser);

/**
 * Get a header by index.
 */
const HttpHeader *http_parser_header(const HttpParser *parser, size_t index);

/**
 * Get a header value by name (case-insensitive).
 * Returns NULL if not found.
 */
const char *http_parser_get_header(const HttpParser *parser, const char *name);

/**
 * Check if response uses chunked transfer encoding.
 */
bool http_parser_is_chunked(const HttpParser *parser);

/**
 * Get Content-Length (-1 if not specified or chunked).
 */
ssize_t http_parser_content_length(const HttpParser *parser);

/*============================================================================
 * Body Access (valid after HTTP_PARSE_CHUNK_READY)
 *============================================================================*/

/**
 * Get current body chunk data.
 * Valid until next call to http_parser_feed() or http_parser_free().
 */
const char *http_parser_chunk_data(const HttpParser *parser);

/**
 * Get current body chunk length.
 */
size_t http_parser_chunk_length(const HttpParser *parser);

#endif /* AGIM_NET_HTTP_PARSER_H */
