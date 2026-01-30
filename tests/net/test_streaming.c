/*
 * HTTP Streaming Tests
 *
 * Tests for the HTTP streaming and SSE functionality.
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "net/http.h"
#include "net/sse.h"

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

#define TEST(name) \
    printf("Testing: %s... ", name); \
    fflush(stdout);

#define PASS() \
    do { \
        printf("PASS\n"); \
        tests_passed++; \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
    } while (0)

#define SKIP(msg) \
    do { \
        printf("SKIP: %s\n", msg); \
        tests_skipped++; \
    } while (0)

/* Maximum retries for flaky network tests */
#define MAX_RETRIES 3

/*============================================================================
 * SSE Parser Tests
 *============================================================================*/

static void test_sse_parser_basic(void) {
    TEST("sse_parser basic event");

    SSEParser *parser = sse_parser_new();
    if (!parser) {
        FAIL("Failed to create parser");
        return;
    }

    const char *data = "data: hello world\n\n";
    int count = sse_parser_feed(parser, data, strlen(data));

    if (count != 1) {
        sse_parser_free(parser);
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 1 event, got %d", count);
        FAIL(msg);
        return;
    }

    const SSEEvent *event = sse_parser_next(parser);
    if (!event) {
        sse_parser_free(parser);
        FAIL("No event returned");
        return;
    }

    if (!event->data || strcmp(event->data, "hello world") != 0) {
        sse_parser_free(parser);
        FAIL("Wrong event data");
        return;
    }

    if (!event->event || strcmp(event->event, "message") != 0) {
        sse_parser_free(parser);
        FAIL("Wrong event type");
        return;
    }

    sse_parser_free(parser);
    PASS();
}

static void test_sse_parser_multiline(void) {
    TEST("sse_parser multiline data");

    SSEParser *parser = sse_parser_new();
    if (!parser) {
        FAIL("Failed to create parser");
        return;
    }

    const char *data = "data: line1\ndata: line2\ndata: line3\n\n";
    int count = sse_parser_feed(parser, data, strlen(data));

    if (count != 1) {
        sse_parser_free(parser);
        FAIL("Expected 1 event");
        return;
    }

    const SSEEvent *event = sse_parser_next(parser);
    if (!event || !event->data) {
        sse_parser_free(parser);
        FAIL("No event data");
        return;
    }

    if (strcmp(event->data, "line1\nline2\nline3") != 0) {
        sse_parser_free(parser);
        char msg[128];
        snprintf(msg, sizeof(msg), "Wrong data: '%s'", event->data);
        FAIL(msg);
        return;
    }

    sse_parser_free(parser);
    PASS();
}

static void test_sse_parser_custom_event(void) {
    TEST("sse_parser custom event type");

    SSEParser *parser = sse_parser_new();
    if (!parser) {
        FAIL("Failed to create parser");
        return;
    }

    const char *data = "event: custom\ndata: test\n\n";
    sse_parser_feed(parser, data, strlen(data));

    const SSEEvent *event = sse_parser_next(parser);
    if (!event || !event->event) {
        sse_parser_free(parser);
        FAIL("No event");
        return;
    }

    if (strcmp(event->event, "custom") != 0) {
        sse_parser_free(parser);
        FAIL("Wrong event type");
        return;
    }

    sse_parser_free(parser);
    PASS();
}

static void test_sse_parser_id_and_retry(void) {
    TEST("sse_parser id and retry");

    SSEParser *parser = sse_parser_new();
    if (!parser) {
        FAIL("Failed to create parser");
        return;
    }

    const char *data = "id: 123\nretry: 5000\ndata: test\n\n";
    sse_parser_feed(parser, data, strlen(data));

    const SSEEvent *event = sse_parser_next(parser);
    if (!event) {
        sse_parser_free(parser);
        FAIL("No event");
        return;
    }

    if (!event->id || strcmp(event->id, "123") != 0) {
        sse_parser_free(parser);
        FAIL("Wrong id");
        return;
    }

    if (event->retry != 5000) {
        sse_parser_free(parser);
        char msg[64];
        snprintf(msg, sizeof(msg), "Wrong retry: %d", event->retry);
        FAIL(msg);
        return;
    }

    sse_parser_free(parser);
    PASS();
}

static void test_sse_parser_comments(void) {
    TEST("sse_parser ignores comments");

    SSEParser *parser = sse_parser_new();
    if (!parser) {
        FAIL("Failed to create parser");
        return;
    }

    const char *data = ": this is a comment\ndata: test\n: another comment\n\n";
    int count = sse_parser_feed(parser, data, strlen(data));

    if (count != 1) {
        sse_parser_free(parser);
        FAIL("Expected 1 event");
        return;
    }

    const SSEEvent *event = sse_parser_next(parser);
    if (!event || strcmp(event->data, "test") != 0) {
        sse_parser_free(parser);
        FAIL("Wrong data");
        return;
    }

    sse_parser_free(parser);
    PASS();
}

/*============================================================================
 * HTTP Streaming Tests
 *============================================================================*/

static void test_stream_get(void) {
    TEST("http_stream_get (HTTPS)");

    HttpStream *stream = NULL;
    size_t total_bytes = 0;

    /* Retry loop for flaky network */
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        if (retry > 0) {
            usleep(500000); /* 500ms delay between retries */
        }

        stream = http_stream_get("https://httpbin.org/stream/3");
        if (!stream) {
            continue;
        }

        if (http_stream_error(stream)) {
            http_stream_close(stream);
            stream = NULL;
            continue;
        }

        /* Wait for status code */
        int attempts = 0;
        while (http_stream_status(stream) == 0 && attempts < 50) {
            usleep(100000); /* 100ms */
            attempts++;
        }

        long status = http_stream_status(stream);
        if (status != 200) {
            http_stream_close(stream);
            stream = NULL;
            continue;
        }

        /* Read some data */
        int chunks_read = 0;
        total_bytes = 0;
        while (!http_stream_done(stream) && chunks_read < 10) {
            size_t len;
            char *chunk = http_stream_read(stream, &len);
            if (chunk) {
                total_bytes += len;
                chunks_read++;
                free(chunk);
            }
        }

        http_stream_close(stream);

        if (total_bytes > 0) {
            break; /* Success */
        }
        stream = NULL;
    }

    if (!stream && total_bytes == 0) {
        SKIP("Network unavailable");
        return;
    }

    PASS();
}

static void test_stream_post(void) {
    TEST("http_stream_post (HTTPS)");

    const char *post_body = "{\"test\": \"data\"}";
    char *response = NULL;
    size_t response_len = 0;

    /* Retry loop for flaky network */
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        if (retry > 0) {
            usleep(500000); /* 500ms delay between retries */
        }

        HttpStream *stream = http_stream_post("https://httpbin.org/post", post_body, "application/json");
        if (!stream) {
            continue;
        }

        if (http_stream_error(stream)) {
            http_stream_close(stream);
            continue;
        }

        /* Read all data */
        response = malloc(1);
        response[0] = '\0';
        response_len = 0;

        while (!http_stream_done(stream)) {
            size_t len;
            char *chunk = http_stream_read(stream, &len);
            if (chunk && len > 0) {
                response = realloc(response, response_len + len + 1);
                memcpy(response + response_len, chunk, len);
                response_len += len;
                response[response_len] = '\0';
                free(chunk);
            }
        }

        http_stream_close(stream);

        if (response_len > 0 && strstr(response, "\"test\"")) {
            break; /* Success */
        }

        free(response);
        response = NULL;
        response_len = 0;
    }

    if (!response || response_len == 0) {
        free(response);
        SKIP("Network unavailable");
        return;
    }

    /* Check response contains our data */
    if (!strstr(response, "\"test\"")) {
        free(response);
        SKIP("Response doesn't contain posted data (network issue)");
        return;
    }

    free(response);
    PASS();
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    printf("=== HTTP Streaming Tests ===\n\n");

    /* SSE Parser tests */
    test_sse_parser_basic();
    test_sse_parser_multiline();
    test_sse_parser_custom_event();
    test_sse_parser_id_and_retry();
    test_sse_parser_comments();

    /* HTTP Streaming tests */
    test_stream_get();
    test_stream_post();

    /* Cleanup */
    http_cleanup();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Skipped: %d\n", tests_skipped);
    printf("Failed: %d\n", tests_failed);

    /* Only fail if there are actual failures, not skips */
    return tests_failed > 0 ? 1 : 0;
}
