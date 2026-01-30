/*
 * Agim - HTTP Response Parser
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "net/http_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define MAX_HEADERS 64
#define MAX_STATUS_TEXT 128
#define INITIAL_BUFFER_SIZE 4096

/*============================================================================
 * Parser State
 *============================================================================*/

typedef enum {
    STATE_STATUS_LINE,
    STATE_HEADERS,
    STATE_BODY_CONTENT_LENGTH,
    STATE_BODY_CHUNKED_SIZE,
    STATE_BODY_CHUNKED_DATA,
    STATE_BODY_CHUNKED_TRAILER,
    STATE_DONE,
    STATE_ERROR
} ParserState;

struct HttpParser {
    ParserState state;

    /* Status line */
    int status_code;
    char status_text[MAX_STATUS_TEXT];

    /* Headers */
    HttpHeader headers[MAX_HEADERS];
    size_t header_count;

    /* Body parsing */
    bool is_chunked;
    ssize_t content_length;
    size_t body_received;
    size_t current_chunk_size;
    size_t current_chunk_received;

    /* Current chunk data (for body access) */
    char *chunk_data;
    size_t chunk_length;

    /* Internal buffer for partial lines */
    char *buffer;
    size_t buffer_len;
    size_t buffer_capacity;
};

/*============================================================================
 * Helper Functions
 *============================================================================*/

static bool buffer_append(HttpParser *parser, const char *data, size_t len) {
    if (parser->buffer_len + len > parser->buffer_capacity) {
        size_t new_capacity = parser->buffer_capacity * 2;
        if (new_capacity < parser->buffer_len + len) {
            new_capacity = parser->buffer_len + len + 1024;
        }
        char *new_buffer = realloc(parser->buffer, new_capacity);
        if (!new_buffer) return false;
        parser->buffer = new_buffer;
        parser->buffer_capacity = new_capacity;
    }
    memcpy(parser->buffer + parser->buffer_len, data, len);
    parser->buffer_len += len;
    return true;
}

static void buffer_consume(HttpParser *parser, size_t len) {
    if (len >= parser->buffer_len) {
        parser->buffer_len = 0;
    } else {
        memmove(parser->buffer, parser->buffer + len, parser->buffer_len - len);
        parser->buffer_len -= len;
    }
}

static char *find_line_end(const char *data, size_t len) {
    for (size_t i = 0; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return (char *)(data + i);
        }
    }
    return NULL;
}


static int strcasecmp_safe(const char *a, const char *b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

/*============================================================================
 * Parser Lifecycle
 *============================================================================*/

HttpParser *http_parser_new(void) {
    HttpParser *parser = calloc(1, sizeof(HttpParser));
    if (!parser) return NULL;

    parser->state = STATE_STATUS_LINE;
    parser->content_length = -1;

    parser->buffer_capacity = INITIAL_BUFFER_SIZE;
    parser->buffer = malloc(parser->buffer_capacity);
    if (!parser->buffer) {
        free(parser);
        return NULL;
    }
    parser->buffer_len = 0;

    return parser;
}

void http_parser_free(HttpParser *parser) {
    if (!parser) return;

    /* Free headers */
    for (size_t i = 0; i < parser->header_count; i++) {
        free(parser->headers[i].name);
        free(parser->headers[i].value);
    }

    free(parser->chunk_data);
    free(parser->buffer);
    free(parser);
}

void http_parser_reset(HttpParser *parser) {
    if (!parser) return;

    /* Free headers */
    for (size_t i = 0; i < parser->header_count; i++) {
        free(parser->headers[i].name);
        free(parser->headers[i].value);
    }
    parser->header_count = 0;

    /* Reset state */
    parser->state = STATE_STATUS_LINE;
    parser->status_code = 0;
    parser->status_text[0] = '\0';
    parser->is_chunked = false;
    parser->content_length = -1;
    parser->body_received = 0;
    parser->current_chunk_size = 0;
    parser->current_chunk_received = 0;

    free(parser->chunk_data);
    parser->chunk_data = NULL;
    parser->chunk_length = 0;

    parser->buffer_len = 0;
}

/*============================================================================
 * Status Line Parsing
 *============================================================================*/

static HttpParseResult parse_status_line(HttpParser *parser) {
    char *line_end = find_line_end(parser->buffer, parser->buffer_len);
    if (!line_end) return HTTP_PARSE_NEED_MORE;

    /* Null-terminate the line temporarily */
    *line_end = '\0';
    const char *line = parser->buffer;

    /* Parse: HTTP/1.x STATUS_CODE STATUS_TEXT */
    if (strncmp(line, "HTTP/1.", 7) != 0) {
        parser->state = STATE_ERROR;
        return HTTP_PARSE_ERROR;
    }

    const char *p = line + 8; /* Skip "HTTP/1.x" */
    while (*p == ' ') p++;

    /* Parse status code */
    char *end;
    long code = strtol(p, &end, 10);
    if (end == p || code < 100 || code > 599) {
        parser->state = STATE_ERROR;
        return HTTP_PARSE_ERROR;
    }
    parser->status_code = (int)code;

    /* Skip space and copy status text */
    while (*end == ' ') end++;
    strncpy(parser->status_text, end, MAX_STATUS_TEXT - 1);
    parser->status_text[MAX_STATUS_TEXT - 1] = '\0';

    /* Consume the line (including \r\n) */
    buffer_consume(parser, (size_t)(line_end - parser->buffer + 2));

    parser->state = STATE_HEADERS;
    return HTTP_PARSE_NEED_MORE;
}

/*============================================================================
 * Header Parsing
 *============================================================================*/

static HttpParseResult parse_headers(HttpParser *parser) {
    while (true) {
        char *line_end = find_line_end(parser->buffer, parser->buffer_len);
        if (!line_end) return HTTP_PARSE_NEED_MORE;

        size_t line_len = (size_t)(line_end - parser->buffer);

        /* Empty line marks end of headers */
        if (line_len == 0) {
            buffer_consume(parser, 2); /* Consume \r\n */

            /* Determine body parsing mode */
            const char *transfer_encoding = http_parser_get_header(parser, "Transfer-Encoding");
            if (transfer_encoding && strcasecmp_safe(transfer_encoding, "chunked") == 0) {
                parser->is_chunked = true;
                parser->state = STATE_BODY_CHUNKED_SIZE;
            } else {
                const char *content_length = http_parser_get_header(parser, "Content-Length");
                if (content_length) {
                    parser->content_length = strtol(content_length, NULL, 10);
                    if (parser->content_length > 0) {
                        parser->state = STATE_BODY_CONTENT_LENGTH;
                    } else {
                        parser->state = STATE_DONE;
                    }
                } else {
                    /* No Content-Length and not chunked - read until close */
                    parser->content_length = -1;
                    parser->state = STATE_BODY_CONTENT_LENGTH;
                }
            }

            return HTTP_PARSE_HEADERS_DONE;
        }

        /* Parse header line */
        if (parser->header_count >= MAX_HEADERS) {
            parser->state = STATE_ERROR;
            return HTTP_PARSE_ERROR;
        }

        /* Find colon */
        char *colon = memchr(parser->buffer, ':', line_len);
        if (!colon) {
            parser->state = STATE_ERROR;
            return HTTP_PARSE_ERROR;
        }

        /* Extract name (trim trailing spaces) */
        size_t name_len = (size_t)(colon - parser->buffer);
        while (name_len > 0 && parser->buffer[name_len - 1] == ' ') {
            name_len--;
        }

        char *name = malloc(name_len + 1);
        if (!name) {
            parser->state = STATE_ERROR;
            return HTTP_PARSE_ERROR;
        }
        memcpy(name, parser->buffer, name_len);
        name[name_len] = '\0';

        /* Extract value (skip leading spaces) */
        const char *value_start = colon + 1;
        while (*value_start == ' ' && value_start < line_end) {
            value_start++;
        }
        size_t value_len = (size_t)(line_end - value_start);

        char *value = malloc(value_len + 1);
        if (!value) {
            free(name);
            parser->state = STATE_ERROR;
            return HTTP_PARSE_ERROR;
        }
        memcpy(value, value_start, value_len);
        value[value_len] = '\0';

        /* Store header */
        parser->headers[parser->header_count].name = name;
        parser->headers[parser->header_count].value = value;
        parser->header_count++;

        /* Consume the line */
        buffer_consume(parser, line_len + 2);
    }
}

/*============================================================================
 * Body Parsing
 *============================================================================*/

static HttpParseResult parse_body_content_length(HttpParser *parser) {
    if (parser->buffer_len == 0) return HTTP_PARSE_NEED_MORE;

    /* Calculate how much to read */
    size_t remaining;
    if (parser->content_length >= 0) {
        remaining = (size_t)parser->content_length - parser->body_received;
    } else {
        /* Read until close - return whatever we have */
        remaining = parser->buffer_len;
    }

    size_t chunk_len = parser->buffer_len < remaining ? parser->buffer_len : remaining;
    if (chunk_len == 0) return HTTP_PARSE_NEED_MORE;

    /* Copy chunk data */
    free(parser->chunk_data);
    parser->chunk_data = malloc(chunk_len + 1);
    if (!parser->chunk_data) {
        parser->state = STATE_ERROR;
        return HTTP_PARSE_ERROR;
    }
    memcpy(parser->chunk_data, parser->buffer, chunk_len);
    parser->chunk_data[chunk_len] = '\0';
    parser->chunk_length = chunk_len;

    buffer_consume(parser, chunk_len);
    parser->body_received += chunk_len;

    /* Check if done */
    if (parser->content_length >= 0 && parser->body_received >= (size_t)parser->content_length) {
        parser->state = STATE_DONE;
    }

    return HTTP_PARSE_CHUNK_READY;
}

static HttpParseResult parse_chunked_size(HttpParser *parser) {
    char *line_end = find_line_end(parser->buffer, parser->buffer_len);
    if (!line_end) return HTTP_PARSE_NEED_MORE;

    /* Parse hex size */
    char *end;
    size_t size = strtoul(parser->buffer, &end, 16);

    buffer_consume(parser, (size_t)(line_end - parser->buffer + 2));

    parser->current_chunk_size = size;
    parser->current_chunk_received = 0;

    if (size == 0) {
        /* Final chunk */
        parser->state = STATE_BODY_CHUNKED_TRAILER;
    } else {
        parser->state = STATE_BODY_CHUNKED_DATA;
    }

    return HTTP_PARSE_NEED_MORE;
}

static HttpParseResult parse_chunked_data(HttpParser *parser) {
    size_t remaining = parser->current_chunk_size - parser->current_chunk_received;
    if (parser->buffer_len == 0 || remaining == 0) return HTTP_PARSE_NEED_MORE;

    size_t chunk_len = parser->buffer_len < remaining ? parser->buffer_len : remaining;

    /* Copy chunk data */
    free(parser->chunk_data);
    parser->chunk_data = malloc(chunk_len + 1);
    if (!parser->chunk_data) {
        parser->state = STATE_ERROR;
        return HTTP_PARSE_ERROR;
    }
    memcpy(parser->chunk_data, parser->buffer, chunk_len);
    parser->chunk_data[chunk_len] = '\0';
    parser->chunk_length = chunk_len;

    buffer_consume(parser, chunk_len);
    parser->current_chunk_received += chunk_len;
    parser->body_received += chunk_len;

    /* Check if chunk complete */
    if (parser->current_chunk_received >= parser->current_chunk_size) {
        /* Need to consume trailing \r\n */
        if (parser->buffer_len >= 2 && parser->buffer[0] == '\r' && parser->buffer[1] == '\n') {
            buffer_consume(parser, 2);
        }
        parser->state = STATE_BODY_CHUNKED_SIZE;
    }

    return HTTP_PARSE_CHUNK_READY;
}

static HttpParseResult parse_chunked_trailer(HttpParser *parser) {
    /* Look for empty line marking end of trailers */
    char *line_end = find_line_end(parser->buffer, parser->buffer_len);
    if (!line_end) return HTTP_PARSE_NEED_MORE;

    size_t line_len = (size_t)(line_end - parser->buffer);
    buffer_consume(parser, line_len + 2);

    if (line_len == 0) {
        /* Empty line - done */
        parser->state = STATE_DONE;
        return HTTP_PARSE_DONE;
    }

    /* Skip trailer headers */
    return HTTP_PARSE_NEED_MORE;
}

/*============================================================================
 * Main Parse Function
 *============================================================================*/

HttpParseResult http_parser_feed(HttpParser *parser, const char *data, size_t len, size_t *consumed) {
    if (!parser || !data) {
        if (consumed) *consumed = 0;
        return HTTP_PARSE_ERROR;
    }

    if (parser->state == STATE_DONE) {
        if (consumed) *consumed = 0;
        return HTTP_PARSE_DONE;
    }

    if (parser->state == STATE_ERROR) {
        if (consumed) *consumed = 0;
        return HTTP_PARSE_ERROR;
    }

    /* Buffer the incoming data */
    if (!buffer_append(parser, data, len)) {
        if (consumed) *consumed = 0;
        return HTTP_PARSE_ERROR;
    }

    HttpParseResult result = HTTP_PARSE_NEED_MORE;

    while (result == HTTP_PARSE_NEED_MORE && parser->buffer_len > 0) {
        switch (parser->state) {
        case STATE_STATUS_LINE:
            result = parse_status_line(parser);
            break;

        case STATE_HEADERS:
            result = parse_headers(parser);
            break;

        case STATE_BODY_CONTENT_LENGTH:
            result = parse_body_content_length(parser);
            break;

        case STATE_BODY_CHUNKED_SIZE:
            result = parse_chunked_size(parser);
            break;

        case STATE_BODY_CHUNKED_DATA:
            result = parse_chunked_data(parser);
            break;

        case STATE_BODY_CHUNKED_TRAILER:
            result = parse_chunked_trailer(parser);
            break;

        case STATE_DONE:
            result = HTTP_PARSE_DONE;
            break;

        case STATE_ERROR:
            result = HTTP_PARSE_ERROR;
            break;
        }
    }

    if (consumed) {
        *consumed = len; /* We buffered all input */
    }

    return result;
}

/*============================================================================
 * Accessors
 *============================================================================*/

int http_parser_status_code(const HttpParser *parser) {
    return parser ? parser->status_code : 0;
}

const char *http_parser_status_text(const HttpParser *parser) {
    return parser ? parser->status_text : "";
}

size_t http_parser_header_count(const HttpParser *parser) {
    return parser ? parser->header_count : 0;
}

const HttpHeader *http_parser_header(const HttpParser *parser, size_t index) {
    if (!parser || index >= parser->header_count) return NULL;
    return &parser->headers[index];
}

const char *http_parser_get_header(const HttpParser *parser, const char *name) {
    if (!parser || !name) return NULL;

    for (size_t i = 0; i < parser->header_count; i++) {
        if (strcasecmp_safe(parser->headers[i].name, name) == 0) {
            return parser->headers[i].value;
        }
    }
    return NULL;
}

bool http_parser_is_chunked(const HttpParser *parser) {
    return parser ? parser->is_chunked : false;
}

ssize_t http_parser_content_length(const HttpParser *parser) {
    return parser ? parser->content_length : -1;
}

const char *http_parser_chunk_data(const HttpParser *parser) {
    return parser ? parser->chunk_data : NULL;
}

size_t http_parser_chunk_length(const HttpParser *parser) {
    return parser ? parser->chunk_length : 0;
}
