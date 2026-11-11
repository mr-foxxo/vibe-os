#ifndef VIBE_PORTED_HTTP_CLIENT_H
#define VIBE_PORTED_HTTP_CLIENT_H

/* Minimal HTTP/1.0 GET client over the vibeOS TCP socket API (item 1.6).
 *
 * Resolution reuses the DNS client built for item 1.4 (literal IPv4 or a
 * real A-record query to the lease nameserver); the transport is the TCP
 * state machine built and loopback-tested in item 1.2. Nothing here fakes
 * a result: every failure mode returns a distinct code and the caller
 * prints an explicit diagnostic.
 *
 * Return codes (vibe_http_get):
 *    0  ok (status_code parsed, body accumulated)
 *   -1  malformed url
 *   -2  host resolution failed
 *   -3  socket() failed
 *   -6  connect() rejected (route/ARP/table full)
 *   -7  never established (handshake timeout)
 *   -4  connected but no complete response within the deadline
 *   -5  response larger than caller buffer (truncated ok flag set)
 */

#include "applications/ported/include/dns_client.h"


#define VIBE_HTTP_DEFAULT_PORT 80u
#define VIBE_HTTP_HOST_MAX 64
#define VIBE_HTTP_PATH_MAX 96
#define VIBE_HTTP_BODY_MAX 4096u

static int vibe_http_parse_url(const char *url,
                               char *host, int host_max,
                               uint32_t *port,
                               char *path, int path_max) {
    const char *cursor;
    int hn = 0;
    int pn = 0;
    uint32_t p = 0u;
    int saw_port = 0;
    int saw_path = 0;

    if (url == 0 || host == 0 || port == 0 || path == 0) {
        return -1;
    }
    if (strncmp(url, "http://", 7u) != 0) {
        return -1;
    }
    cursor = url + 7;
    while (*cursor != '\0' && *cursor != ':' && *cursor != '/') {
        if (hn + 1 >= host_max) {
            return -1;
        }
        host[hn++] = *cursor++;
    }
    host[hn] = '\0';
    if (hn == 0) {
        return -1;
    }
    if (*cursor == ':') {
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9') {
            p = p * 10u + (uint32_t)(*cursor - '0');
            if (p > 65535u) {
                return -1;
            }
            ++cursor;
            saw_port = 1;
        }
        if (!saw_port || p == 0u) {
            return -1;
        }
    } else {
        p = VIBE_HTTP_DEFAULT_PORT;
    }
    *port = p;
    if (*cursor == '/') {
        while (*cursor != '\0') {
            if (pn + 1 >= path_max) {
                return -1;
            }
            path[pn++] = *cursor++;
            saw_path = 1;
        }
    }
    if (!saw_path) {
        path[pn++] = '/';
    }
    path[pn] = '\0';
    return 0;
}

static int vibe_http_established_send(int handle, const uint8_t *req, uint32_t len,
                                      uint32_t timeout_ms) {
    unsigned long long deadline = vibe_app_millis() + timeout_ms;

    while (vibe_app_millis() < deadline) {
        int n = vibe_app_network_send(handle, req, len);
        if (n == (int)len) {
            return 0;
        }
        vibe_app_yield();
        (void)vibe_app_sleep_ms(10u);
    }
    return -3;
}

static int vibe_http_get(const char *url,
                         const char *dns_server_text,
                         uint8_t *body, uint32_t body_max,
                         uint32_t *body_len_out,
                         uint32_t *status_out,
                         char *line0, int line0_max) {
    char host[VIBE_HTTP_HOST_MAX];
    char path[VIBE_HTTP_PATH_MAX];
    uint32_t port = 0u;
    uint32_t ip[4];
    uint8_t resolved[4];
    uint8_t req[256];
        int handle;
    uint32_t total = 0u;
    uint32_t quiet_ms;
    unsigned long long deadline;
    struct sockaddr_in addr;

    if (vibe_http_parse_url(url, host, (int)sizeof(host), &port,
                            path, (int)sizeof(path)) != 0) {
        return -1;
    }
    if (vibe_dns_parse_ipv4(host, resolved) == 0) {
        ip[0] = resolved[0];
        ip[1] = resolved[1];
        ip[2] = resolved[2];
        ip[3] = resolved[3];
    } else {
        uint8_t addr_bytes[4];

        if (dns_server_text == 0 || dns_server_text[0] == '\0' ||
            vibe_dns_resolve(host, dns_server_text, addr_bytes, 0, 0) != 0) {
            return -2;
        }
        ip[0] = addr_bytes[0];
        ip[1] = addr_bytes[1];
        ip[2] = addr_bytes[2];
        ip[3] = addr_bytes[3];
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = (uint8_t)sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = (uint32_t)ip[0] |
                           ((uint32_t)ip[1] << 8) |
                           ((uint32_t)ip[2] << 16) |
                           ((uint32_t)ip[3] << 24);

    handle = vibe_app_network_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle < 0) {
        return -3;
    }
    if (vibe_app_network_socket_connect(handle, (const struct sockaddr *)&addr,
                                        sizeof(addr)) != 0) {
        (void)vibe_app_network_close(handle);
        return -6;
    }

    /* The kernel send refuses until ESTABLISHED; this loop doubles as the
     * handshake wait (SYN retransmission runs on its own RFC 6298 timer). */
    {
        int hlen = 0;
        int i;
        const char *verb = "GET ";

        while (*verb != '\0' && hlen < (int)sizeof(req) - 3) {
            req[hlen++] = (uint8_t)*verb++;
        }
        for (i = 0; path[i] != '\0' && hlen < (int)sizeof(req) - 32; ++i) {
            req[hlen++] = (uint8_t)path[i];
        }
        {
            const char *tail = " HTTP/1.0\r\nHost: ";
            const char *t = tail;
            while (*t != '\0' && hlen < (int)sizeof(req) - 16) {
                req[hlen++] = (uint8_t)*t++;
            }
        }
        for (i = 0; host[i] != '\0' && hlen < (int)sizeof(req) - 16; ++i) {
            req[hlen++] = (uint8_t)host[i];
        }
        {
            const char *end = "\r\n\r\n";
            const char *t = end;
            while (*t != '\0') {
                req[hlen++] = (uint8_t)*t++;
            }
        }
        req[hlen] = '\0';
        if (vibe_http_established_send(handle, req, (uint32_t)hlen, 6000u) != 0) {
            (void)vibe_app_network_close(handle);
            return -7;
        }
    }

    /* Drain: keep reading until a quiet window after the first byte, or
     * the overall deadline. HTTP/1.0 servers close after the response. */
    deadline = vibe_app_millis() + 8000u;
    quiet_ms = 0u;
    if (line0 != 0 && line0_max > 0) {
        line0[0] = '\0';
    }
    for (;;) {
        uint8_t chunk[256];
        int n = vibe_app_network_recv(handle, chunk, (uint32_t)sizeof(chunk));

        if (n > 0) {
            uint32_t i;

            for (i = 0; i < (uint32_t)n && total < body_max; ++i) {
                body[total++] = chunk[i];
            }
            if (total >= body_max) {
                break;
            }
            quiet_ms = 0u;
        } else {
            quiet_ms += 20u;
            if (total > 0u && quiet_ms >= 800u) {
                break;
            }
            if (vibe_app_millis() >= deadline) {
                break;
            }
        }
        vibe_app_yield();
        (void)vibe_app_sleep_ms(20u);
    }
    (void)vibe_app_network_close(handle);

    if (total == 0u) {
        return -4;
    }
    if (line0 != 0 && line0_max > 0) {
        uint32_t i = 0u;
        int o = 0;
        while (i < total && body[i] != '\r' && body[i] != '\n' && o < line0_max - 1) {
            line0[o++] = (char)body[i++];
        }
        line0[o] = '\0';
    }
    /* "HTTP/1.x NNN" */
    *status_out = 0u;
    if (total >= 12u && memcmp(body, "HTTP/1.", 7u) == 0) {
        uint32_t k = 8u;
        while (k < total && body[k] != ' ') {
            k++;
        }
        k++;
        while (k < total && body[k] >= '0' && body[k] <= '9') {
            *status_out = *status_out * 10u + (uint32_t)(body[k] - '0');
            k++;
        }
    }
    if (body_len_out != 0) {
        *body_len_out = total;
    }
    return (total >= body_max) ? -5 : 0;
}


/* Nameserver choice identical to dig (item 1.4): /runtime/resolv.conf
 * (mirrored by netmgrd from the lease) first, then status.dns_server. */
static int vibe_http_pick_server(const struct mk_network_status *status,
                                 char *server_out, int server_max) {
    const char *data = 0;
    int size = 0;
    char text[128];
    char *cursor;

    if (vibe_app_read_file("/runtime/resolv.conf", &data, &size) == 0 &&
        data != 0 && size > 0) {
        if (size >= (int)sizeof(text)) {
            size = (int)sizeof(text) - 1;
        }
        memcpy(text, data, (size_t)size);
        text[size] = '\0';
        cursor = text;
        while (*cursor != '\0') {
            char *line_end = cursor;
            char *value;

            while (*line_end != '\0' && *line_end != '\n') {
                ++line_end;
            }
            if (*line_end == '\n') {
                *line_end = '\0';
            }
            while (*cursor == ' ' || *cursor == '\t') {
                ++cursor;
            }
            value = cursor;
            while (*value != '\0' && *value != ' ' && *value != '\t') {
                ++value;
            }
            if (*value != '\0') {
                *value++ = '\0';
                while (*value == ' ' || *value == '\t') {
                    ++value;
                }
            }
            if (strcmp(cursor, "nameserver") == 0 && value[0] != '\0') {
                char *token_end = value;

                while (*token_end != '\0' && *token_end != ' ' && *token_end != '\t') {
                    ++token_end;
                }
                *token_end = '\0';
                snprintf(server_out, (size_t)server_max, "%s", value);
                return 0;
            }
            if (*line_end == '\0') {
                break;
            }
            cursor = line_end + 1;
        }
    }
    if (status != 0 && status->dns_server[0] != '\0' &&
        strcmp(status->dns_server, "-") != 0 &&
        strcmp(status->dns_server, "0.0.0.0") != 0) {
        snprintf(server_out, (size_t)server_max, "%s", status->dns_server);
        return 0;
    }
    return -1;
}

#endif
