/*
 * Agim - Server-Sent Events Parser
 *
 * Parser for SSE (Server-Sent Events) protocol.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_NET_SSE_H
#define AGIM_NET_SSE_H

#include <stdbool.h>
#include <stddef.h>

/* Types */

typedef struct SSEParser SSEParser;

/**
 * Parsed SSE event.
 * All strings are owned by the parser and valid until
 * the next call to sse_parser_feed() or sse_parser_free().
 */
typedef struct {
    const char *event;  /* Event type (default: "message") */
    const char *data;   /* Event data (may contain newlines) */
    const char *id;     /* Last event ID (may be NULL) */
    int retry;          /* Reconnection time in ms (-1 if not set) */
} SSEEvent;

/* Parser Lifecycle */

/**
 * Create a new SSE parser.
 */
SSEParser *sse_parser_new(void);

/**
 * Free an SSE parser.
 */
void sse_parser_free(SSEParser *parser);

/**
 * Reset parser state for reuse.
 */
void sse_parser_reset(SSEParser *parser);

/* Parsing */

/**
 * Feed data to the SSE parser.
 *
 * @param parser Parser instance
 * @param data   Data to parse
 * @param len    Length of data
 * @return       Number of complete events available
 */
int sse_parser_feed(SSEParser *parser, const char *data, size_t len);

/**
 * Get the next parsed event.
 * Returns NULL if no more events are available.
 * The returned event is valid until the next call to sse_parser_feed().
 */
const SSEEvent *sse_parser_next(SSEParser *parser);

/**
 * Check if the parser has encountered an error.
 */
bool sse_parser_error(const SSEParser *parser);

#endif /* AGIM_NET_SSE_H */
