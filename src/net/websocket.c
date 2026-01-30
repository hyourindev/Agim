/*
 * Agim - WebSocket Protocol
 *
 * WebSocket client implementation (RFC 6455).
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "net/websocket.h"
#include "net/tcp.h"
#include "net/tls.h"
#include "net/url.h"
#include "net/http_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <bcrypt.h>
    #include <winsock2.h>
#else
    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define WS_TIMEOUT_MS 30000
#define WS_BUFFER_SIZE 8192
#define WS_MAX_HEADER_SIZE 14
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/*============================================================================
 * WebSocket Structure
 *============================================================================*/

struct WebSocket {
    TCPSocket *tcp;             /* Plain TCP socket (for ws://) */
    TLSSocket *tls;             /* TLS socket (for wss://) */
    bool is_secure;             /* Whether using TLS */
    bool connected;             /* Connection state */
    WSError last_error;         /* Last error code */
    uint16_t close_code;        /* Received close status code */
    char *close_reason;         /* Received close reason */
    int timeout_ms;             /* Operation timeout */

    /* Fragment reassembly */
    char *fragment_data;
    size_t fragment_len;
    size_t fragment_capacity;
    int fragment_opcode;
};

/*============================================================================
 * Secure Random
 *============================================================================*/

/**
 * Generate cryptographically secure random bytes.
 */
static bool secure_random(void *buf, size_t len) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;

    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (char *)buf + total, len - total);
        if (n <= 0) {
            close(fd);
            return false;
        }
        total += (size_t)n;
    }

    close(fd);
    return true;
#endif
}

/*============================================================================
 * Base64 Encoding
 *============================================================================*/

static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const void *data, size_t len) {
    const unsigned char *input = (const unsigned char *)data;
    size_t output_len = ((len + 2) / 3) * 4;
    char *output = malloc(output_len + 1);
    if (!output) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3, j += 4) {
        uint32_t v = input[i] << 16;
        if (i + 1 < len) v |= input[i + 1] << 8;
        if (i + 2 < len) v |= input[i + 2];

        output[j] = base64_table[(v >> 18) & 0x3F];
        output[j + 1] = base64_table[(v >> 12) & 0x3F];
        output[j + 2] = (i + 1 < len) ? base64_table[(v >> 6) & 0x3F] : '=';
        output[j + 3] = (i + 2 < len) ? base64_table[v & 0x3F] : '=';
    }

    output[output_len] = '\0';
    return output;
}

/*============================================================================
 * SHA-1 (for WebSocket handshake)
 *============================================================================*/

/* Simple SHA-1 implementation for WebSocket accept key verification */
typedef struct {
    uint32_t state[5];
    uint64_t count;
    unsigned char buffer[64];
} SHA1_CTX;

static void sha1_init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_transform(SHA1_CTX *ctx, const unsigned char *data) {
    uint32_t a, b, c, d, e;
    uint32_t w[80];

    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)data[i * 4] << 24 |
               (uint32_t)data[i * 4 + 1] << 16 |
               (uint32_t)data[i * 4 + 2] << 8 |
               (uint32_t)data[i * 4 + 3];
    }

    for (int i = 16; i < 80; i++) {
        w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        uint32_t temp = ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL(b, 30);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_update(SHA1_CTX *ctx, const void *data, size_t len) {
    const unsigned char *input = (const unsigned char *)data;
    size_t fill = (size_t)(ctx->count & 63);

    ctx->count += len;

    if (fill && fill + len >= 64) {
        memcpy(ctx->buffer + fill, input, 64 - fill);
        sha1_transform(ctx, ctx->buffer);
        input += 64 - fill;
        len -= 64 - fill;
        fill = 0;
    }

    while (len >= 64) {
        sha1_transform(ctx, input);
        input += 64;
        len -= 64;
    }

    if (len > 0) {
        memcpy(ctx->buffer + fill, input, len);
    }
}

static void sha1_final(SHA1_CTX *ctx, unsigned char digest[20]) {
    unsigned char pad[64] = {0x80};
    uint64_t bits = ctx->count * 8;
    size_t fill = (size_t)(ctx->count & 63);

    size_t pad_len = (fill < 56) ? (56 - fill) : (120 - fill);
    sha1_update(ctx, pad, pad_len);

    unsigned char len_buf[8];
    for (int i = 0; i < 8; i++) {
        len_buf[i] = (unsigned char)(bits >> (56 - i * 8));
    }
    sha1_update(ctx, len_buf, 8);

    for (int i = 0; i < 5; i++) {
        digest[i * 4] = (unsigned char)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)(ctx->state[i]);
    }
}

/**
 * Compute SHA-1 hash and return base64-encoded result.
 */
static char *sha1_base64(const char *input) {
    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, input, strlen(input));

    unsigned char digest[20];
    sha1_final(&ctx, digest);

    return base64_encode(digest, 20);
}

/*============================================================================
 * Socket I/O Wrappers
 *============================================================================*/

/**
 * Get the underlying file descriptor for poll/select.
 */
static int ws_get_fd(WebSocket *ws) {
    if (ws->is_secure) {
        return tls_get_fd(ws->tls);
    } else {
        return tcp_get_fd(ws->tcp);
    }
}

/**
 * Wait for data to be available with timeout.
 * Returns: 1 if data available, 0 if timeout, -1 if error.
 */
static int ws_wait_readable(WebSocket *ws, int timeout_ms) {
    if (timeout_ms < 0) {
        return 1; /* No timeout, proceed to blocking read */
    }

    int fd = ws_get_fd(ws);
    if (fd < 0) return -1;

#ifdef _WIN32
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int result = select(fd + 1, &readfds, NULL, NULL, timeout_ms == 0 ? &tv : &tv);
    return result;
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int result = poll(&pfd, 1, timeout_ms);
    if (result > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        return -1;
    }
    return result;
#endif
}

static ssize_t ws_socket_read(WebSocket *ws, void *buf, size_t len) {
    if (ws->is_secure) {
        return tls_read(ws->tls, buf, len);
    } else {
        return tcp_read(ws->tcp, buf, len);
    }
}

static bool ws_socket_write_all(WebSocket *ws, const void *data, size_t len) {
    if (ws->is_secure) {
        return tls_write_all(ws->tls, data, len);
    } else {
        return tcp_write_all(ws->tcp, data, len);
    }
}

/*============================================================================
 * WebSocket Frame Encoding
 *============================================================================*/

/**
 * Send a WebSocket frame.
 * Client frames must always be masked per RFC 6455.
 */
static bool ws_send_frame(WebSocket *ws, uint8_t opcode, const void *data, size_t len, bool fin) {
    if (!ws->connected) return false;

    uint8_t header[WS_MAX_HEADER_SIZE];
    size_t header_len = 2;

    /* First byte: FIN + opcode */
    header[0] = (fin ? 0x80 : 0x00) | (opcode & 0x0F);

    /* Second byte: MASK bit + length */
    header[1] = 0x80; /* Client always masks */

    if (len < 126) {
        header[1] |= (uint8_t)len;
    } else if (len < 65536) {
        header[1] |= 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)(len & 0xFF);
        header_len = 4;
    } else {
        header[1] |= 127;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (uint8_t)(len >> (56 - i * 8));
        }
        header_len = 10;
    }

    /* Generate mask key */
    uint8_t mask[4];
    if (!secure_random(mask, 4)) {
        ws->last_error = WS_ERROR_IO;
        return false;
    }
    memcpy(header + header_len, mask, 4);
    header_len += 4;

    /* Send header */
    if (!ws_socket_write_all(ws, header, header_len)) {
        ws->last_error = WS_ERROR_IO;
        ws->connected = false;
        return false;
    }

    /* Send masked payload */
    if (len > 0) {
        const uint8_t *payload = (const uint8_t *)data;
        uint8_t *masked = malloc(len);
        if (!masked) {
            ws->last_error = WS_ERROR_MEMORY;
            return false;
        }

        for (size_t i = 0; i < len; i++) {
            masked[i] = payload[i] ^ mask[i % 4];
        }

        bool ok = ws_socket_write_all(ws, masked, len);
        free(masked);

        if (!ok) {
            ws->last_error = WS_ERROR_IO;
            ws->connected = false;
            return false;
        }
    }

    return true;
}

/*============================================================================
 * WebSocket Frame Decoding
 *============================================================================*/

/**
 * Read exactly n bytes from socket.
 */
static bool ws_read_exact(WebSocket *ws, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = ws_socket_read(ws, (char *)buf + total, len - total);
        if (n <= 0) {
            return false;
        }
        total += (size_t)n;
    }
    return true;
}

/**
 * Receive a WebSocket frame.
 * Returns the opcode, stores data in *out_data and length in *out_len.
 * Caller must free *out_data.
 * Returns -1 on error.
 */
static int ws_recv_frame(WebSocket *ws, char **out_data, size_t *out_len, bool *out_fin) {
    if (!ws->connected) return -1;

    /* Read first two bytes of header */
    uint8_t header[2];
    if (!ws_read_exact(ws, header, 2)) {
        ws->last_error = WS_ERROR_IO;
        ws->connected = false;
        return -1;
    }

    bool fin = (header[0] & 0x80) != 0;
    int opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    /* Server frames should not be masked, but handle anyway */
    (void)masked;

    /* Read extended length if needed */
    if (payload_len == 126) {
        uint8_t ext[2];
        if (!ws_read_exact(ws, ext, 2)) {
            ws->last_error = WS_ERROR_IO;
            ws->connected = false;
            return -1;
        }
        payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!ws_read_exact(ws, ext, 8)) {
            ws->last_error = WS_ERROR_IO;
            ws->connected = false;
            return -1;
        }
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    /* Limit payload size */
    if (payload_len > 100 * 1024 * 1024) { /* 100 MB */
        ws->last_error = WS_ERROR_PROTOCOL;
        ws->connected = false;
        return -1;
    }

    /* Read mask key if present */
    uint8_t mask[4] = {0};
    if (masked) {
        if (!ws_read_exact(ws, mask, 4)) {
            ws->last_error = WS_ERROR_IO;
            ws->connected = false;
            return -1;
        }
    }

    /* Read payload */
    char *data = NULL;
    if (payload_len > 0) {
        data = malloc((size_t)payload_len + 1);
        if (!data) {
            ws->last_error = WS_ERROR_MEMORY;
            return -1;
        }

        if (!ws_read_exact(ws, data, (size_t)payload_len)) {
            free(data);
            ws->last_error = WS_ERROR_IO;
            ws->connected = false;
            return -1;
        }

        /* Unmask if needed */
        if (masked) {
            for (size_t i = 0; i < payload_len; i++) {
                data[i] ^= mask[i % 4];
            }
        }

        data[payload_len] = '\0';
    }

    *out_data = data;
    *out_len = (size_t)payload_len;
    *out_fin = fin;

    return opcode;
}

/*============================================================================
 * Connection
 *============================================================================*/

WebSocket *ws_connect(const char *url, int timeout_ms, WSError *error) {
    if (!url) {
        if (error) *error = WS_ERROR_URL;
        return NULL;
    }

    /* Parse URL and check scheme */
    bool is_secure = false;
    const char *http_url = url;

    if (strncmp(url, "wss://", 6) == 0) {
        is_secure = true;
        /* Convert wss:// to https:// for URL parsing */
        char *temp = malloc(strlen(url) + 2);
        if (!temp) {
            if (error) *error = WS_ERROR_MEMORY;
            return NULL;
        }
        strcpy(temp, "https://");
        strcat(temp, url + 6);
        http_url = temp;
    } else if (strncmp(url, "ws://", 5) == 0) {
        /* Convert ws:// to http:// for URL parsing */
        char *temp = malloc(strlen(url) + 1);
        if (!temp) {
            if (error) *error = WS_ERROR_MEMORY;
            return NULL;
        }
        strcpy(temp, "http://");
        strcat(temp, url + 5);
        http_url = temp;
    } else {
        if (error) *error = WS_ERROR_URL;
        return NULL;
    }

    ParsedURL *parsed = url_parse(http_url);
    if (http_url != url) free((char *)http_url);

    if (!parsed) {
        if (error) *error = WS_ERROR_URL;
        return NULL;
    }

    /* Allocate WebSocket structure */
    WebSocket *ws = calloc(1, sizeof(WebSocket));
    if (!ws) {
        url_free(parsed);
        if (error) *error = WS_ERROR_MEMORY;
        return NULL;
    }

    ws->is_secure = is_secure;
    ws->timeout_ms = timeout_ms > 0 ? timeout_ms : WS_TIMEOUT_MS;

    /* Connect */
    if (is_secure) {
        TLSError tls_err;
        ws->tls = tls_connect(parsed->host, parsed->port, ws->timeout_ms, &tls_err);
        if (!ws->tls) {
            url_free(parsed);
            free(ws);
            if (error) *error = WS_ERROR_CONNECT;
            return NULL;
        }
    } else {
        TCPError tcp_err;
        ws->tcp = tcp_connect(parsed->host, parsed->port, ws->timeout_ms, &tcp_err);
        if (!ws->tcp) {
            url_free(parsed);
            free(ws);
            if (error) *error = WS_ERROR_CONNECT;
            return NULL;
        }
    }

    /* Generate WebSocket key */
    uint8_t key_bytes[16];
    if (!secure_random(key_bytes, 16)) {
        if (ws->tls) tls_close(ws->tls);
        if (ws->tcp) tcp_close(ws->tcp);
        url_free(parsed);
        free(ws);
        if (error) *error = WS_ERROR_IO;
        return NULL;
    }

    char *key = base64_encode(key_bytes, 16);
    if (!key) {
        if (ws->tls) tls_close(ws->tls);
        if (ws->tcp) tcp_close(ws->tcp);
        url_free(parsed);
        free(ws);
        if (error) *error = WS_ERROR_MEMORY;
        return NULL;
    }

    /* Build upgrade request */
    char *request_path = url_request_path(parsed);
    char *host_header = url_host_header(parsed);

    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             request_path, host_header, key);

    free(request_path);
    free(host_header);

    /* Send upgrade request */
    if (!ws_socket_write_all(ws, request, strlen(request))) {
        free(key);
        if (ws->tls) tls_close(ws->tls);
        if (ws->tcp) tcp_close(ws->tcp);
        url_free(parsed);
        free(ws);
        if (error) *error = WS_ERROR_IO;
        return NULL;
    }

    /* Compute expected accept key */
    char accept_input[128];
    snprintf(accept_input, sizeof(accept_input), "%s%s", key, WS_GUID);
    char *expected_accept = sha1_base64(accept_input);
    free(key);

    if (!expected_accept) {
        if (ws->tls) tls_close(ws->tls);
        if (ws->tcp) tcp_close(ws->tcp);
        url_free(parsed);
        free(ws);
        if (error) *error = WS_ERROR_MEMORY;
        return NULL;
    }

    /* Read and parse HTTP response */
    HttpParser *parser = http_parser_new();
    if (!parser) {
        free(expected_accept);
        if (ws->tls) tls_close(ws->tls);
        if (ws->tcp) tcp_close(ws->tcp);
        url_free(parsed);
        free(ws);
        if (error) *error = WS_ERROR_MEMORY;
        return NULL;
    }

    char buf[1024];
    bool handshake_ok = false;

    while (true) {
        ssize_t n = ws_socket_read(ws, buf, sizeof(buf));
        if (n <= 0) break;

        size_t consumed;
        HttpParseResult result = http_parser_feed(parser, buf, (size_t)n, &consumed);

        if (result == HTTP_PARSE_ERROR) {
            break;
        }

        if (result == HTTP_PARSE_HEADERS_DONE || result == HTTP_PARSE_DONE) {
            /* Check response */
            int status = http_parser_status_code(parser);
            if (status != 101) {
                break;
            }

            /* Check Sec-WebSocket-Accept */
            const char *accept = http_parser_get_header(parser, "Sec-WebSocket-Accept");
            if (!accept || strcmp(accept, expected_accept) != 0) {
                break;
            }

            handshake_ok = true;
            break;
        }
    }

    http_parser_free(parser);
    free(expected_accept);
    url_free(parsed);

    if (!handshake_ok) {
        if (ws->tls) tls_close(ws->tls);
        if (ws->tcp) tcp_close(ws->tcp);
        free(ws);
        if (error) *error = WS_ERROR_HANDSHAKE;
        return NULL;
    }

    ws->connected = true;
    if (error) *error = WS_OK;
    return ws;
}

void ws_close(WebSocket *ws, uint16_t code, const char *reason) {
    if (!ws) return;

    if (ws->connected) {
        /* Send close frame */
        size_t reason_len = reason ? strlen(reason) : 0;
        size_t payload_len = 2 + reason_len;
        uint8_t *payload = malloc(payload_len);

        if (payload) {
            payload[0] = (uint8_t)(code >> 8);
            payload[1] = (uint8_t)(code & 0xFF);
            if (reason) {
                memcpy(payload + 2, reason, reason_len);
            }

            ws_send_frame(ws, WS_OPCODE_CLOSE, payload, payload_len, true);
            free(payload);
        }

        ws->connected = false;
    }

    if (ws->tls) tls_close(ws->tls);
    if (ws->tcp) tcp_close(ws->tcp);
    free(ws->close_reason);
    free(ws->fragment_data);
    free(ws);
}

/*============================================================================
 * Send
 *============================================================================*/

bool ws_send_text(WebSocket *ws, const char *message) {
    if (!ws || !message) return false;
    return ws_send_frame(ws, WS_OPCODE_TEXT, message, strlen(message), true);
}

bool ws_send_binary(WebSocket *ws, const void *data, size_t len) {
    if (!ws || (!data && len > 0)) return false;
    return ws_send_frame(ws, WS_OPCODE_BINARY, data, len, true);
}

bool ws_send_ping(WebSocket *ws, const void *data, size_t len) {
    if (!ws) return false;
    if (len > 125) len = 125; /* Control frames max 125 bytes */
    return ws_send_frame(ws, WS_OPCODE_PING, data, len, true);
}

/*============================================================================
 * Receive
 *============================================================================*/

char *ws_recv(WebSocket *ws, size_t *len, int *opcode, int timeout_ms) {
    if (!ws || !ws->connected) {
        if (len) *len = 0;
        if (opcode) *opcode = 0;
        return NULL;
    }

    while (true) {
        /* Wait for data with timeout */
        int wait_result = ws_wait_readable(ws, timeout_ms);
        if (wait_result == 0) {
            /* Timeout */
            ws->last_error = WS_ERROR_TIMEOUT;
            if (len) *len = 0;
            if (opcode) *opcode = 0;
            return NULL;
        } else if (wait_result < 0) {
            /* Error */
            ws->last_error = WS_ERROR_IO;
            ws->connected = false;
            if (len) *len = 0;
            if (opcode) *opcode = 0;
            return NULL;
        }

        char *data;
        size_t data_len;
        bool fin;

        int frame_opcode = ws_recv_frame(ws, &data, &data_len, &fin);
        if (frame_opcode < 0) {
            if (len) *len = 0;
            if (opcode) *opcode = 0;
            return NULL;
        }

        switch (frame_opcode) {
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
            if (fin && ws->fragment_data == NULL) {
                /* Complete message */
                if (len) *len = data_len;
                if (opcode) *opcode = frame_opcode;
                return data;
            } else {
                /* Start of fragmented message */
                free(ws->fragment_data);
                ws->fragment_data = data;
                ws->fragment_len = data_len;
                ws->fragment_capacity = data_len;
                ws->fragment_opcode = frame_opcode;
            }
            break;

        case WS_OPCODE_CONTINUATION:
            if (ws->fragment_data) {
                /* Continue fragment */
                size_t new_len = ws->fragment_len + data_len;
                if (new_len > ws->fragment_capacity) {
                    size_t new_cap = ws->fragment_capacity * 2;
                    if (new_cap < new_len) new_cap = new_len + 1024;
                    char *new_data = realloc(ws->fragment_data, new_cap + 1);
                    if (!new_data) {
                        free(data);
                        ws->last_error = WS_ERROR_MEMORY;
                        return NULL;
                    }
                    ws->fragment_data = new_data;
                    ws->fragment_capacity = new_cap;
                }
                memcpy(ws->fragment_data + ws->fragment_len, data, data_len);
                ws->fragment_len = new_len;
                ws->fragment_data[new_len] = '\0';
                free(data);

                if (fin) {
                    /* Message complete */
                    char *result = ws->fragment_data;
                    if (len) *len = ws->fragment_len;
                    if (opcode) *opcode = ws->fragment_opcode;
                    ws->fragment_data = NULL;
                    ws->fragment_len = 0;
                    ws->fragment_capacity = 0;
                    return result;
                }
            } else {
                free(data);
            }
            break;

        case WS_OPCODE_PING:
            /* Send pong */
            ws_send_frame(ws, WS_OPCODE_PONG, data, data_len, true);
            free(data);
            break;

        case WS_OPCODE_PONG:
            /* Ignore */
            free(data);
            break;

        case WS_OPCODE_CLOSE:
            /* Parse close code and reason */
            if (data_len >= 2) {
                ws->close_code = ((uint8_t)data[0] << 8) | (uint8_t)data[1];
                if (data_len > 2) {
                    ws->close_reason = malloc(data_len - 1);
                    if (ws->close_reason) {
                        memcpy(ws->close_reason, data + 2, data_len - 2);
                        ws->close_reason[data_len - 2] = '\0';
                    }
                }
            }
            free(data);

            /* Send close response if we initiated */
            if (ws->connected) {
                ws_send_frame(ws, WS_OPCODE_CLOSE, NULL, 0, true);
            }

            ws->connected = false;
            ws->last_error = WS_ERROR_CLOSED;

            if (len) *len = 0;
            if (opcode) *opcode = WS_OPCODE_CLOSE;
            return NULL;

        default:
            /* Unknown opcode */
            free(data);
            break;
        }
    }
}

/*============================================================================
 * Status
 *============================================================================*/

bool ws_is_connected(WebSocket *ws) {
    return ws && ws->connected;
}

WSError ws_last_error(WebSocket *ws) {
    return ws ? ws->last_error : WS_ERROR_IO;
}

const char *ws_error_string(WSError error) {
    switch (error) {
    case WS_OK:             return "Success";
    case WS_ERROR_URL:      return "Invalid URL";
    case WS_ERROR_CONNECT:  return "Connection failed";
    case WS_ERROR_HANDSHAKE: return "WebSocket handshake failed";
    case WS_ERROR_PROTOCOL: return "Protocol error";
    case WS_ERROR_CLOSED:   return "Connection closed";
    case WS_ERROR_IO:       return "I/O error";
    case WS_ERROR_MEMORY:   return "Memory allocation failed";
    case WS_ERROR_TIMEOUT:  return "Operation timed out";
    default:                return "Unknown error";
    }
}

uint16_t ws_close_code(WebSocket *ws) {
    return ws ? ws->close_code : 0;
}

const char *ws_close_reason(WebSocket *ws) {
    return ws ? ws->close_reason : NULL;
}
