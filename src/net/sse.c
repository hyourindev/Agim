/*
 * Agim - Server-Sent Events Parser
 *
 * Parser for SSE (Server-Sent Events) protocol.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "net/sse.h"
#include "util/alloc.h"

#include <stdlib.h>
#include <string.h>

/* Constants */

#define SSE_INITIAL_BUFFER_SIZE 4096
#define SSE_MAX_EVENTS 16

/* Parser State */

typedef struct {
    char *event;       /* Event type (allocated) */
    char *data;        /* Event data (allocated) */
    char *id;          /* Last event ID (allocated) */
    int retry;         /* Reconnection time */
} ParsedEvent;

struct SSEParser {
    /* Input buffer */
    char *buffer;
    size_t buffer_len;
    size_t buffer_capacity;

    /* Current event being built */
    char *cur_event;
    char *cur_data;
    char *cur_id;
    int cur_retry;

    /* Parsed events queue */
    ParsedEvent events[SSE_MAX_EVENTS];
    int event_count;
    int event_read_idx;

    /* Last event ID (persists across events) */
    char *last_id;

    /* Error flag */
    bool has_error;
};

/* Helper Functions */

static char *str_append_with_newline(char *existing, const char *append) {
    if (!append) return existing;

    if (!existing) {
        return agim_strdup(append);
    }

    size_t existing_len = strlen(existing);
    size_t append_len = strlen(append);
    char *new_str = realloc(existing, existing_len + 1 + append_len + 1);
    if (!new_str) {
        return existing;
    }
    new_str[existing_len] = '\n';
    memcpy(new_str + existing_len + 1, append, append_len + 1);
    return new_str;
}

static bool buffer_append(SSEParser *parser, const char *data, size_t len) {
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

static void buffer_consume(SSEParser *parser, size_t len) {
    if (len >= parser->buffer_len) {
        parser->buffer_len = 0;
    } else {
        memmove(parser->buffer, parser->buffer + len, parser->buffer_len - len);
        parser->buffer_len -= len;
    }
}

static void free_event_contents(ParsedEvent *event) {
    free(event->event);
    free(event->data);
    free(event->id);
    event->event = NULL;
    event->data = NULL;
    event->id = NULL;
    event->retry = -1;
}

/* Parser Lifecycle */

SSEParser *sse_parser_new(void) {
    SSEParser *parser = calloc(1, sizeof(SSEParser));
    if (!parser) return NULL;

    parser->buffer_capacity = SSE_INITIAL_BUFFER_SIZE;
    parser->buffer = malloc(parser->buffer_capacity);
    if (!parser->buffer) {
        free(parser);
        return NULL;
    }
    parser->buffer_len = 0;
    parser->cur_retry = -1;

    for (int i = 0; i < SSE_MAX_EVENTS; i++) {
        parser->events[i].retry = -1;
    }

    return parser;
}

void sse_parser_free(SSEParser *parser) {
    if (!parser) return;

    free(parser->buffer);
    free(parser->cur_event);
    free(parser->cur_data);
    free(parser->cur_id);
    free(parser->last_id);

    for (int i = 0; i < SSE_MAX_EVENTS; i++) {
        free_event_contents(&parser->events[i]);
    }

    free(parser);
}

void sse_parser_reset(SSEParser *parser) {
    if (!parser) return;

    parser->buffer_len = 0;
    free(parser->cur_event);
    free(parser->cur_data);
    free(parser->cur_id);
    parser->cur_event = NULL;
    parser->cur_data = NULL;
    parser->cur_id = NULL;
    parser->cur_retry = -1;

    for (int i = 0; i < SSE_MAX_EVENTS; i++) {
        free_event_contents(&parser->events[i]);
    }
    parser->event_count = 0;
    parser->event_read_idx = 0;
    parser->has_error = false;
}

/* Parsing */

/**
 * Process a single line from the SSE stream.
 * Returns true if an event was completed (blank line).
 */
static bool process_line(SSEParser *parser, const char *line, size_t len) {
    /* Empty line = dispatch event */
    if (len == 0) {
        if (parser->cur_data) {
            /* We have data, dispatch an event */
            if (parser->event_count < SSE_MAX_EVENTS) {
                ParsedEvent *event = &parser->events[parser->event_count];
                free_event_contents(event);

                event->event = parser->cur_event ? parser->cur_event : agim_strdup("message");
                event->data = parser->cur_data;
                event->id = parser->cur_id ? parser->cur_id : agim_strdup(parser->last_id);
                event->retry = parser->cur_retry;

                /* Update last_id if we had an id */
                if (parser->cur_id) {
                    free(parser->last_id);
                    parser->last_id = agim_strdup(parser->cur_id);
                }

                parser->cur_event = NULL;
                parser->cur_data = NULL;
                parser->cur_id = NULL;
                parser->cur_retry = -1;
                parser->event_count++;
                return true;
            }
        }
        /* Reset for next event */
        free(parser->cur_event);
        free(parser->cur_data);
        free(parser->cur_id);
        parser->cur_event = NULL;
        parser->cur_data = NULL;
        parser->cur_id = NULL;
        parser->cur_retry = -1;
        return false;
    }

    /* Comment line (starts with :) - ignore */
    if (line[0] == ':') {
        return false;
    }

    /* Find the colon separator */
    const char *colon = memchr(line, ':', len);
    const char *field_start = line;
    size_t field_len;
    const char *value_start;
    size_t value_len;

    if (colon) {
        field_len = (size_t)(colon - line);
        value_start = colon + 1;
        /* Skip optional space after colon */
        if ((size_t)(value_start - line) < len && *value_start == ' ') {
            value_start++;
        }
        value_len = len - (size_t)(value_start - line);
    } else {
        /* No colon - treat entire line as field name with empty value */
        field_len = len;
        value_start = "";
        value_len = 0;
    }

    /* Create null-terminated value string */
    char *value = malloc(value_len + 1);
    if (!value) return false;
    memcpy(value, value_start, value_len);
    value[value_len] = '\0';

    /* Process field */
    if (field_len == 5 && strncmp(field_start, "event", 5) == 0) {
        free(parser->cur_event);
        parser->cur_event = value;
    } else if (field_len == 4 && strncmp(field_start, "data", 4) == 0) {
        if (parser->cur_data) {
            parser->cur_data = str_append_with_newline(parser->cur_data, value);
            free(value);
        } else {
            parser->cur_data = value;
        }
    } else if (field_len == 2 && strncmp(field_start, "id", 2) == 0) {
        /* ID must not contain null bytes */
        if (memchr(value, '\0', value_len) == NULL) {
            free(parser->cur_id);
            parser->cur_id = value;
        } else {
            free(value);
        }
    } else if (field_len == 5 && strncmp(field_start, "retry", 5) == 0) {
        /* Retry must be a number */
        char *endptr;
        long retry = strtol(value, &endptr, 10);
        if (*endptr == '\0' && retry >= 0) {
            parser->cur_retry = (int)retry;
        }
        free(value);
    } else {
        /* Unknown field - ignore */
        free(value);
    }

    return false;
}

int sse_parser_feed(SSEParser *parser, const char *data, size_t len) {
    if (!parser || !data || len == 0) return 0;

    /* Buffer the incoming data */
    if (!buffer_append(parser, data, len)) {
        parser->has_error = true;
        return 0;
    }

    /* Reset event queue */
    for (int i = 0; i < parser->event_count; i++) {
        free_event_contents(&parser->events[i]);
    }
    parser->event_count = 0;
    parser->event_read_idx = 0;

    /* Process complete lines */
    size_t pos = 0;
    while (pos < parser->buffer_len) {
        /* Find end of line (CR, LF, or CRLF) */
        size_t line_start = pos;
        size_t line_end = pos;

        while (line_end < parser->buffer_len &&
               parser->buffer[line_end] != '\r' &&
               parser->buffer[line_end] != '\n') {
            line_end++;
        }

        /* No complete line yet */
        if (line_end >= parser->buffer_len) {
            break;
        }

        /* Process the line */
        process_line(parser, parser->buffer + line_start, line_end - line_start);

        /* Skip line terminator */
        pos = line_end + 1;
        if (pos < parser->buffer_len && parser->buffer[line_end] == '\r' &&
            parser->buffer[pos] == '\n') {
            pos++;
        }
    }

    /* Remove processed data from buffer */
    if (pos > 0) {
        buffer_consume(parser, pos);
    }

    return parser->event_count;
}

const SSEEvent *sse_parser_next(SSEParser *parser) {
    if (!parser || parser->event_read_idx >= parser->event_count) {
        return NULL;
    }

    /* Convert ParsedEvent to SSEEvent (same layout, but we cast) */
    static SSEEvent result;
    ParsedEvent *event = &parser->events[parser->event_read_idx++];
    result.event = event->event;
    result.data = event->data;
    result.id = event->id;
    result.retry = event->retry;

    return &result;
}

bool sse_parser_error(const SSEParser *parser) {
    return parser ? parser->has_error : true;
}
