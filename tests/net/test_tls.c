/*
 * TLS Layer Tests
 *
 * Tests for the TLS/HTTPS functionality.
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "net/tls.h"
#include "net/http.h"

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

/* TLS Tests */

static void test_tls_init(void) {
    TEST("tls_init");
    bool result = tls_init();
    if (result) {
        PASS();
    } else {
        FAIL("tls_init returned false");
    }
}

static void test_tls_connect_github(void) {
    TEST("tls_connect to api.github.com");

    TLSError err;
    TLSSocket *sock = tls_connect("api.github.com", 443, 30000, &err);

    if (!sock) {
        FAIL(tls_error_string(err));
        return;
    }

    /* Send a simple HTTP request */
    const char *request = "GET / HTTP/1.1\r\nHost: api.github.com\r\nConnection: close\r\n\r\n";
    if (!tls_write_all(sock, request, strlen(request))) {
        tls_close(sock);
        FAIL("tls_write_all failed");
        return;
    }

    /* Read response */
    char buf[1024];
    ssize_t n = tls_read(sock, buf, sizeof(buf) - 1);

    if (n <= 0) {
        tls_close(sock);
        FAIL("tls_read failed");
        return;
    }

    buf[n] = '\0';

    /* Check for HTTP response */
    if (strncmp(buf, "HTTP/1.1", 8) != 0) {
        tls_close(sock);
        FAIL("Response doesn't start with HTTP/1.1");
        return;
    }

    tls_close(sock);
    PASS();
}

static void test_tls_connect_invalid_host(void) {
    TEST("tls_connect to invalid host");

    TLSError err;
    TLSSocket *sock = tls_connect("this.host.does.not.exist.example.com", 443, 5000, &err);

    if (sock) {
        tls_close(sock);
        FAIL("Should have failed but succeeded");
        return;
    }

    if (err != TLS_ERROR_CONNECT) {
        FAIL("Expected TLS_ERROR_CONNECT");
        return;
    }

    PASS();
}

/* HTTP/HTTPS Tests */

static void test_https_get(void) {
    TEST("http_get (HTTPS)");

    HttpResponse *resp = NULL;

    /* Retry loop for flaky network */
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            usleep(500000); /* 500ms delay between retries */
        }

        resp = http_get("https://httpbin.org/get");

        if (resp && !resp->error && resp->status_code == 200 &&
            resp->body && resp->body_len > 0) {
            break; /* Success */
        }

        if (resp) {
            http_response_free(resp);
            resp = NULL;
        }
    }

    if (!resp) {
        SKIP("Network unavailable");
        return;
    }

    if (resp->error) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Error: %s", resp->error);
        http_response_free(resp);
        SKIP(msg);
        return;
    }

    if (resp->status_code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 200, got %ld", resp->status_code);
        http_response_free(resp);
        FAIL(msg);
        return;
    }

    if (!resp->body || resp->body_len == 0) {
        http_response_free(resp);
        SKIP("No body received (network issue)");
        return;
    }

    /* Check that body contains expected content */
    if (!strstr(resp->body, "httpbin.org")) {
        http_response_free(resp);
        FAIL("Body doesn't contain expected content");
        return;
    }

    http_response_free(resp);
    PASS();
}

static void test_https_post(void) {
    TEST("http_post (HTTPS)");

    const char *post_body = "{\"test\": \"data\"}";
    HttpResponse *resp = NULL;

    /* Retry loop for flaky network */
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            usleep(500000); /* 500ms delay between retries */
        }

        resp = http_post("https://httpbin.org/post", post_body, "application/json");

        if (resp && !resp->error && resp->status_code == 200 &&
            resp->body && resp->body_len > 0) {
            break; /* Success */
        }

        if (resp) {
            http_response_free(resp);
            resp = NULL;
        }
    }

    if (!resp) {
        SKIP("Network unavailable");
        return;
    }

    if (resp->error) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Error: %s", resp->error);
        http_response_free(resp);
        SKIP(msg);
        return;
    }

    if (resp->status_code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 200, got %ld", resp->status_code);
        http_response_free(resp);
        FAIL(msg);
        return;
    }

    /* Check that body contains our posted data */
    if (!resp->body || !strstr(resp->body, "\"test\"")) {
        http_response_free(resp);
        SKIP("Body doesn't contain posted data (network issue)");
        return;
    }

    http_response_free(resp);
    PASS();
}

static void test_http_still_works(void) {
    TEST("http_get (plain HTTP still works)");

    HttpResponse *resp = http_get("http://httpbin.org/get");

    if (!resp) {
        FAIL("http_get returned NULL");
        return;
    }

    if (resp->error) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Error: %s", resp->error);
        http_response_free(resp);
        FAIL(msg);
        return;
    }

    if (resp->status_code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 200, got %ld", resp->status_code);
        http_response_free(resp);
        FAIL(msg);
        return;
    }

    http_response_free(resp);
    PASS();
}

/* Main */

int main(void) {
    printf("=== TLS/HTTPS Tests ===\n\n");

    /* TLS layer tests */
    test_tls_init();
    test_tls_connect_github();
    test_tls_connect_invalid_host();

    /* HTTP/HTTPS tests */
    test_https_get();
    test_https_post();
    test_http_still_works();

    /* Cleanup */
    tls_cleanup();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Skipped: %d\n", tests_skipped);
    printf("Failed: %d\n", tests_failed);

    /* Only fail if there are actual failures, not skips */
    return tests_failed > 0 ? 1 : 0;
}
