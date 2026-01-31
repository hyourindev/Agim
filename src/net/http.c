/*
 * Agim - HTTP Client
 *
 * HTTP/HTTPS client implementation.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "net/http.h"
#include "net/tcp.h"
#include "net/tls.h"
#include "net/url.h"
#include "net/http_parser.h"

#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Constants */

#define HTTP_TIMEOUT_MS 30000
#define HTTP_BUFFER_SIZE 8192
#define HTTP_MAX_RESPONSE_SIZE (10 * 1024 * 1024) /* 10 MB */

/* Response Buffer */

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} Buffer;

static bool buffer_init(Buffer *buf, size_t initial_capacity) {
    buf->capacity = initial_capacity > 0 ? initial_capacity : 4096;
    buf->data = malloc(buf->capacity);
    if (!buf->data) return false;
    buf->data[0] = '\0';
    buf->len = 0;
    return true;
}

static bool buffer_append(Buffer *buf, const char *data, size_t len) {
    if (buf->len + len + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        while (new_capacity < buf->len + len + 1) {
            new_capacity *= 2;
        }
        char *new_data = realloc(buf->data, new_capacity);
        if (!new_data) return false;
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

static void buffer_free(Buffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
}

/* Stream Structure */

typedef struct StreamChunk {
    char *data;
    size_t len;
    struct StreamChunk *next;
} StreamChunk;

struct HttpStream {
    TCPSocket *tcp_socket;      /* Plain TCP socket (for HTTP) */
    TLSSocket *tls_socket;      /* TLS socket (for HTTPS) */
    bool is_https;              /* Whether using TLS */
    HttpParser *parser;
    ParsedURL *url;

    pthread_t thread;
    bool thread_started;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    StreamChunk *head;
    StreamChunk *tail;

    _Atomic(bool) done;
    _Atomic(bool) error;
    _Atomic(long) status_code;
    char *error_msg;
};

/* Global State */

static bool g_http_initialized = false;

bool http_init(void) {
    if (g_http_initialized) return true;
    if (!tcp_init()) return false;
    if (!tls_init()) {
        tcp_cleanup();
        return false;
    }
    g_http_initialized = true;
    return true;
}

void http_cleanup(void) {
    if (!g_http_initialized) return;
    tls_cleanup();
    tcp_cleanup();
    g_http_initialized = false;
}

/* URL Validation & SSRF Protection */

static bool is_private_ipv4(uint32_t ip);
static bool parse_ipv4(const char *host, uint32_t *ip);

static int parse_ip_octet(const char *str, const char **endptr) {
    if (!str || !*str) return -1;
    char *end;
    long val;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        val = strtol(str, &end, 16);
    } else if (str[0] == '0' && str[1] >= '0' && str[1] <= '7') {
        val = strtol(str, &end, 8);
    } else {
        val = strtol(str, &end, 10);
    }
    if (endptr) *endptr = end;
    if (end == str || val < 0 || val > 255) return -1;
    return (int)val;
}

static bool parse_ipv4(const char *host, uint32_t *ip) {
    if (!host || !ip) return false;
    char *end;
    unsigned long single = strtoul(host, &end, 10);
    if (*end == '\0' && single <= 0xFFFFFFFF) {
        *ip = (uint32_t)single;
        return true;
    }
    const char *p = host;
    uint32_t result = 0;
    int octets = 0;
    while (octets < 4) {
        int val = parse_ip_octet(p, &p);
        if (val < 0) return false;
        result = (result << 8) | (uint32_t)val;
        octets++;
        if (*p == '.') p++;
        else if (*p == '\0') break;
        else return false;
    }
    if (octets != 4 || *p != '\0') return false;
    *ip = result;
    return true;
}

static bool is_private_ipv4(uint32_t ip) {
    uint8_t a = (ip >> 24) & 0xFF;
    uint8_t b = (ip >> 16) & 0xFF;
    if (a == 127) return true;
    if (a == 10) return true;
    if (a == 172 && b >= 16 && b <= 31) return true;
    if (a == 192 && b == 168) return true;
    if (a == 169 && b == 254) return true;
    if (ip == 0) return true;
    if (ip == 0xFFFFFFFF) return true;
    return false;
}

static bool is_private_ip(const char *host) {
    if (!host || !*host) return true;
    size_t len = strlen(host);
    if (len > 255) return true;

    char lower[256];
    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)host[i]);
    }
    lower[len] = '\0';

    if (strcmp(lower, "localhost") == 0) return true;
    if (strcmp(lower, "localhost.localdomain") == 0) return true;
    if (strcmp(lower, "::1") == 0) return true;
    if (strcmp(lower, "0:0:0:0:0:0:0:1") == 0) return true;

    const char *ipv4_part = NULL;
    if (strncmp(lower, "::ffff:", 7) == 0) ipv4_part = host + 7;
    else if (strncmp(lower, "0:0:0:0:0:ffff:", 15) == 0) ipv4_part = host + 15;

    if (ipv4_part) {
        uint32_t ip;
        if (parse_ipv4(ipv4_part, &ip) && is_private_ipv4(ip)) return true;
    }

    if (lower[0] == '[') {
        char inner[256];
        const char *end = strchr(lower, ']');
        if (end && (size_t)(end - lower - 1) < sizeof(inner)) {
            size_t inner_len = (size_t)(end - lower - 1);
            memcpy(inner, lower + 1, inner_len);
            inner[inner_len] = '\0';
            if (is_private_ip(inner)) return true;
        }
    }

    uint32_t ip;
    if (parse_ipv4(host, &ip)) return is_private_ipv4(ip);

    return false;
}

bool http_url_valid(const char *url, bool allow_private) {
    if (!url) return false;
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) return false;
    if (strncmp(url, "file://", 7) == 0) return false;

    ParsedURL *parsed = url_parse(url);
    if (!parsed) return false;

    bool valid = true;
    if (!allow_private && is_private_ip(parsed->host)) {
        valid = false;
    }

    url_free(parsed);
    return valid;
}

char *http_url_encode(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *encoded = malloc(len * 3 + 1);
    if (!encoded) return NULL;

    char *out = encoded;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            *out++ = (char)c;
        } else {
            snprintf(out, 4, "%%%02X", c);
            out += 3;
        }
    }
    *out = '\0';
    return encoded;
}

/* Response Helpers */

void http_response_free(HttpResponse *resp) {
    if (!resp) return;
    free(resp->body);
    free(resp->content_type);
    free(resp->error);
    free(resp);
}

static HttpResponse *response_new(void) {
    HttpResponse *resp = calloc(1, sizeof(HttpResponse));
    return resp;
}

static HttpResponse *response_error(const char *msg) {
    HttpResponse *resp = response_new();
    if (!resp) return NULL;
    resp->error = msg ? strdup(msg) : NULL;
    return resp;
}

/* Request Building */

static char *build_request(const char *method, ParsedURL *url, const char *body,
                           const char **headers) {
    Buffer buf;
    if (!buffer_init(&buf, 1024)) return NULL;

    char *path = url_request_path(url);
    char *host_header = url_host_header(url);

    if (!path || !host_header) {
        free(path);
        free(host_header);
        buffer_free(&buf);
        return NULL;
    }

    /* Request line */
    buffer_append(&buf, method, strlen(method));
    buffer_append(&buf, " ", 1);
    buffer_append(&buf, path, strlen(path));
    buffer_append(&buf, " HTTP/1.1\r\n", 11);

    /* Host header */
    buffer_append(&buf, "Host: ", 6);
    buffer_append(&buf, host_header, strlen(host_header));
    buffer_append(&buf, "\r\n", 2);

    /* User-Agent */
    buffer_append(&buf, "User-Agent: Agim/1.0\r\n", 22);

    /* Connection: close */
    buffer_append(&buf, "Connection: close\r\n", 19);

    /* Custom headers */
    if (headers) {
        for (const char **h = headers; *h; h++) {
            buffer_append(&buf, *h, strlen(*h));
            buffer_append(&buf, "\r\n", 2);
        }
    }

    /* Content-Length for body */
    if (body) {
        char cl[64];
        snprintf(cl, sizeof(cl), "Content-Length: %zu\r\n", strlen(body));
        buffer_append(&buf, cl, strlen(cl));
    }

    /* End of headers */
    buffer_append(&buf, "\r\n", 2);

    /* Body */
    if (body) {
        buffer_append(&buf, body, strlen(body));
    }

    free(path);
    free(host_header);

    return buf.data;
}

/* Synchronous HTTP Requests */

static HttpResponse *http_request(const char *method, const char *url_str,
                                   const char *body, const char **headers) {
    if (!url_str) return response_error("URL is required");
    if (!http_url_valid(url_str, false)) return response_error("Invalid or blocked URL");

    /* Ensure initialized */
    if (!g_http_initialized) http_init();

    /* Parse URL */
    ParsedURL *url = url_parse(url_str);
    if (!url) return response_error("Failed to parse URL");

    /* Connect - use TLS for HTTPS, plain TCP for HTTP */
    TCPSocket *tcp_sock = NULL;
    TLSSocket *tls_sock = NULL;

    if (url->is_https) {
        TLSError tls_err;
        tls_sock = tls_connect(url->host, url->port, HTTP_TIMEOUT_MS, &tls_err);
        if (!tls_sock) {
            url_free(url);
            return response_error(tls_error_string(tls_err));
        }
    } else {
        TCPError tcp_err;
        tcp_sock = tcp_connect(url->host, url->port, HTTP_TIMEOUT_MS, &tcp_err);
        if (!tcp_sock) {
            url_free(url);
            return response_error(tcp_error_string(tcp_err));
        }
    }

    /* Build request */
    char *request = build_request(method, url, body, headers);
    if (!request) {
        if (tls_sock) tls_close(tls_sock);
        if (tcp_sock) tcp_close(tcp_sock);
        url_free(url);
        return response_error("Failed to build request");
    }

    /* Send request */
    bool send_ok;
    if (tls_sock) {
        send_ok = tls_write_all(tls_sock, request, strlen(request));
    } else {
        send_ok = tcp_write_all(tcp_sock, request, strlen(request));
    }

    if (!send_ok) {
        free(request);
        if (tls_sock) tls_close(tls_sock);
        if (tcp_sock) tcp_close(tcp_sock);
        url_free(url);
        return response_error("Failed to send request");
    }
    free(request);

    /* Create parser */
    HttpParser *parser = http_parser_new();
    if (!parser) {
        if (tls_sock) tls_close(tls_sock);
        if (tcp_sock) tcp_close(tcp_sock);
        url_free(url);
        return response_error("Out of memory");
    }

    /* Read and parse response */
    Buffer body_buf;
    if (!buffer_init(&body_buf, 4096)) {
        http_parser_free(parser);
        if (tls_sock) tls_close(tls_sock);
        if (tcp_sock) tcp_close(tcp_sock);
        url_free(url);
        return response_error("Out of memory");
    }

    char read_buf[HTTP_BUFFER_SIZE];
    HttpResponse *resp = response_new();
    bool headers_done = false;

    while (true) {
        ssize_t n;
        if (tls_sock) {
            n = tls_read(tls_sock, read_buf, sizeof(read_buf));
        } else {
            n = tcp_read(tcp_sock, read_buf, sizeof(read_buf));
        }

        if (n < 0) {
            /* Error or timeout */
            break;
        }
        if (n == 0) {
            /* EOF */
            break;
        }

        size_t consumed;
        HttpParseResult result = http_parser_feed(parser, read_buf, (size_t)n, &consumed);

        if (result == HTTP_PARSE_ERROR) {
            resp->error = strdup("HTTP parse error");
            break;
        }

        if (result == HTTP_PARSE_HEADERS_DONE && !headers_done) {
            headers_done = true;
            resp->status_code = http_parser_status_code(parser);
            const char *ct = http_parser_get_header(parser, "Content-Type");
            if (ct) resp->content_type = strdup(ct);
        }

        if (result == HTTP_PARSE_CHUNK_READY) {
            const char *chunk = http_parser_chunk_data(parser);
            size_t chunk_len = http_parser_chunk_length(parser);
            if (chunk && chunk_len > 0) {
                if (body_buf.len + chunk_len > HTTP_MAX_RESPONSE_SIZE) {
                    resp->error = strdup("Response too large");
                    break;
                }
                buffer_append(&body_buf, chunk, chunk_len);
            }
        }

        if (result == HTTP_PARSE_DONE) {
            break;
        }
    }

    /* Transfer body to response */
    if (body_buf.len > 0 && !resp->error) {
        resp->body = body_buf.data;
        resp->body_len = body_buf.len;
        body_buf.data = NULL; /* Transfer ownership */
    } else {
        buffer_free(&body_buf);
    }

    http_parser_free(parser);
    if (tls_sock) tls_close(tls_sock);
    if (tcp_sock) tcp_close(tcp_sock);
    url_free(url);

    return resp;
}

HttpResponse *http_get(const char *url) {
    return http_request("GET", url, NULL, NULL);
}

HttpResponse *http_post(const char *url, const char *body, const char *content_type) {
    const char *headers[2] = {NULL, NULL};
    char ct_header[256];

    if (content_type) {
        snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);
        headers[0] = ct_header;
    }

    return http_request("POST", url, body, headers);
}

HttpResponse *http_post_with_headers(const char *url, const char *body,
                                      const char **headers) {
    return http_request("POST", url, body, headers);
}

HttpResponse *http_put(const char *url, const char *body, const char *content_type) {
    const char *headers[2] = {NULL, NULL};
    char ct_header[256];

    if (content_type) {
        snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);
        headers[0] = ct_header;
    }

    return http_request("PUT", url, body, headers);
}

HttpResponse *http_delete(const char *url) {
    return http_request("DELETE", url, NULL, NULL);
}

HttpResponse *http_patch(const char *url, const char *body, const char *content_type) {
    const char *headers[2] = {NULL, NULL};
    char ct_header[256];

    if (content_type) {
        snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);
        headers[0] = ct_header;
    }

    return http_request("PATCH", url, body, headers);
}

HttpResponse *http_request_generic(const char *method, const char *url,
                                    const char *body, const char **headers) {
    return http_request(method, url, body, headers);
}

/* Streaming - Helper Functions */

/**
 * Enqueue a chunk to the stream's queue.
 */
static void stream_enqueue_chunk(HttpStream *stream, const char *data, size_t len) {
    StreamChunk *chunk = malloc(sizeof(StreamChunk));
    if (!chunk) return;

    chunk->data = malloc(len + 1);
    if (!chunk->data) {
        free(chunk);
        return;
    }
    memcpy(chunk->data, data, len);
    chunk->data[len] = '\0';
    chunk->len = len;
    chunk->next = NULL;

    pthread_mutex_lock(&stream->mutex);
    if (stream->tail) {
        stream->tail->next = chunk;
        stream->tail = chunk;
    } else {
        stream->head = stream->tail = chunk;
    }
    pthread_cond_signal(&stream->cond);
    pthread_mutex_unlock(&stream->mutex);
}

/**
 * Dequeue a chunk from the stream's queue.
 */
static StreamChunk *stream_dequeue_chunk(HttpStream *stream) {
    StreamChunk *chunk = stream->head;
    if (chunk) {
        stream->head = chunk->next;
        if (!stream->head) {
            stream->tail = NULL;
        }
    }
    return chunk;
}

/**
 * Read from the stream's socket (HTTP or HTTPS).
 */
static ssize_t stream_socket_read(HttpStream *stream, void *buf, size_t len) {
    if (stream->is_https) {
        return tls_read(stream->tls_socket, buf, len);
    } else {
        return tcp_read(stream->tcp_socket, buf, len);
    }
}

/**
 * Background thread that reads from socket and processes HTTP response.
 */
static void *stream_reader_thread(void *arg) {
    HttpStream *stream = (HttpStream *)arg;
    char buf[HTTP_BUFFER_SIZE];
    bool headers_done = false;

    while (!atomic_load(&stream->done)) {
        ssize_t n = stream_socket_read(stream, buf, sizeof(buf));

        if (n < 0) {
            /* Error */
            pthread_mutex_lock(&stream->mutex);
            if (!stream->error_msg) {
                stream->error_msg = strdup("Read error");
            }
            pthread_mutex_unlock(&stream->mutex);
            atomic_store(&stream->error, true);
            break;
        }

        if (n == 0) {
            /* EOF */
            break;
        }

        /* Feed to HTTP parser */
        size_t consumed;
        HttpParseResult result = http_parser_feed(stream->parser, buf, (size_t)n, &consumed);

        if (result == HTTP_PARSE_ERROR) {
            pthread_mutex_lock(&stream->mutex);
            if (!stream->error_msg) {
                stream->error_msg = strdup("HTTP parse error");
            }
            pthread_mutex_unlock(&stream->mutex);
            atomic_store(&stream->error, true);
            break;
        }

        if (result == HTTP_PARSE_HEADERS_DONE && !headers_done) {
            headers_done = true;
            atomic_store(&stream->status_code, http_parser_status_code(stream->parser));
        }

        if (result == HTTP_PARSE_CHUNK_READY) {
            const char *chunk = http_parser_chunk_data(stream->parser);
            size_t chunk_len = http_parser_chunk_length(stream->parser);
            if (chunk && chunk_len > 0) {
                stream_enqueue_chunk(stream, chunk, chunk_len);
            }
        }

        if (result == HTTP_PARSE_DONE) {
            break;
        }
    }

    atomic_store(&stream->done, true);
    pthread_mutex_lock(&stream->mutex);
    pthread_cond_signal(&stream->cond);
    pthread_mutex_unlock(&stream->mutex);

    return NULL;
}

/**
 * Internal function to start a streaming request.
 */
static HttpStream *http_stream_request(const char *method, const char *url_str,
                                        const char *body, const char **headers) {
    if (!url_str) return NULL;
    if (!http_url_valid(url_str, false)) return NULL;

    /* Ensure initialized */
    if (!g_http_initialized) http_init();

    /* Parse URL */
    ParsedURL *url = url_parse(url_str);
    if (!url) return NULL;

    /* Allocate stream */
    HttpStream *stream = calloc(1, sizeof(HttpStream));
    if (!stream) {
        url_free(url);
        return NULL;
    }

    stream->url = url;
    stream->is_https = url->is_https;
    atomic_init(&stream->done, false);
    atomic_init(&stream->error, false);
    atomic_init(&stream->status_code, 0);

    /* Initialize mutex and condition variable */
    if (pthread_mutex_init(&stream->mutex, NULL) != 0) {
        url_free(url);
        free(stream);
        return NULL;
    }

    if (pthread_cond_init(&stream->cond, NULL) != 0) {
        pthread_mutex_destroy(&stream->mutex);
        url_free(url);
        free(stream);
        return NULL;
    }

    /* Connect */
    if (stream->is_https) {
        TLSError tls_err;
        stream->tls_socket = tls_connect(url->host, url->port, HTTP_TIMEOUT_MS, &tls_err);
        if (!stream->tls_socket) {
            stream->error_msg = strdup(tls_error_string(tls_err));
            atomic_store(&stream->error, true);
            atomic_store(&stream->done, true);
            return stream;
        }
    } else {
        TCPError tcp_err;
        stream->tcp_socket = tcp_connect(url->host, url->port, HTTP_TIMEOUT_MS, &tcp_err);
        if (!stream->tcp_socket) {
            stream->error_msg = strdup(tcp_error_string(tcp_err));
            atomic_store(&stream->error, true);
            atomic_store(&stream->done, true);
            return stream;
        }
    }

    /* Build and send request */
    char *request = build_request(method, url, body, headers);
    if (!request) {
        stream->error_msg = strdup("Failed to build request");
        atomic_store(&stream->error, true);
        atomic_store(&stream->done, true);
        return stream;
    }

    bool send_ok;
    if (stream->is_https) {
        send_ok = tls_write_all(stream->tls_socket, request, strlen(request));
    } else {
        send_ok = tcp_write_all(stream->tcp_socket, request, strlen(request));
    }
    free(request);

    if (!send_ok) {
        stream->error_msg = strdup("Failed to send request");
        atomic_store(&stream->error, true);
        atomic_store(&stream->done, true);
        return stream;
    }

    /* Create HTTP parser */
    stream->parser = http_parser_new();
    if (!stream->parser) {
        stream->error_msg = strdup("Out of memory");
        atomic_store(&stream->error, true);
        atomic_store(&stream->done, true);
        return stream;
    }

    /* Start background reader thread */
    if (pthread_create(&stream->thread, NULL, stream_reader_thread, stream) != 0) {
        stream->error_msg = strdup("Failed to create reader thread");
        atomic_store(&stream->error, true);
        atomic_store(&stream->done, true);
        return stream;
    }
    stream->thread_started = true;

    return stream;
}

/* Streaming - Public API */

HttpStream *http_stream_get(const char *url) {
    return http_stream_request("GET", url, NULL, NULL);
}

HttpStream *http_stream_post(const char *url, const char *body, const char *content_type) {
    const char *headers[2] = {NULL, NULL};
    char ct_header[256];

    if (content_type) {
        snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);
        headers[0] = ct_header;
    }

    return http_stream_request("POST", url, body, headers);
}

HttpStream *http_stream_post_with_headers(const char *url, const char *body,
                                           const char **headers) {
    return http_stream_request("POST", url, body, headers);
}

char *http_stream_read(HttpStream *stream, size_t *len) {
    if (!stream) {
        if (len) *len = 0;
        return NULL;
    }

    pthread_mutex_lock(&stream->mutex);

    /* Wait for data or completion */
    while (!stream->head && !atomic_load(&stream->done) && !atomic_load(&stream->error)) {
        pthread_cond_wait(&stream->cond, &stream->mutex);
    }

    StreamChunk *chunk = stream_dequeue_chunk(stream);
    pthread_mutex_unlock(&stream->mutex);

    if (!chunk) {
        if (len) *len = 0;
        return NULL;
    }

    char *data = chunk->data;
    if (len) *len = chunk->len;
    free(chunk);

    return data;
}

bool http_stream_done(HttpStream *stream) {
    if (!stream) return true;

    /* Check if done and queue is empty */
    pthread_mutex_lock(&stream->mutex);
    bool is_done = atomic_load(&stream->done) && stream->head == NULL;
    pthread_mutex_unlock(&stream->mutex);

    return is_done;
}

bool http_stream_error(HttpStream *stream) {
    if (!stream) return true;
    return atomic_load(&stream->error);
}

const char *http_stream_error_msg(HttpStream *stream) {
    if (!stream) return "Stream is NULL";
    if (!stream->error_msg) return NULL;
    return stream->error_msg;
}

long http_stream_status(HttpStream *stream) {
    if (!stream) return 0;
    return atomic_load(&stream->status_code);
}

void http_stream_close(HttpStream *stream) {
    if (!stream) return;

    /* Signal done to stop reader thread */
    atomic_store(&stream->done, true);

    /* Wait for reader thread to finish */
    if (stream->thread_started) {
        pthread_join(stream->thread, NULL);
    }

    /* Clean up chunks in queue */
    pthread_mutex_lock(&stream->mutex);
    while (stream->head) {
        StreamChunk *chunk = stream_dequeue_chunk(stream);
        if (chunk) {
            free(chunk->data);
            free(chunk);
        }
    }
    pthread_mutex_unlock(&stream->mutex);

    /* Clean up resources */
    pthread_mutex_destroy(&stream->mutex);
    pthread_cond_destroy(&stream->cond);

    if (stream->tls_socket) tls_close(stream->tls_socket);
    if (stream->tcp_socket) tcp_close(stream->tcp_socket);
    if (stream->parser) http_parser_free(stream->parser);
    if (stream->url) url_free(stream->url);
    free(stream->error_msg);
    free(stream);
}
