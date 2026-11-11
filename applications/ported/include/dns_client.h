#ifndef VIBE_PORTED_DNS_CLIENT_H
#define VIBE_PORTED_DNS_CLIENT_H

/* Minimal DNS A-record client over the vibeOS UDP socket API.
 *
 * Shared by the host/dig ports. It sends one standard recursive query
 * (single question, RD=1) to the DNS server carried by the network status
 * - which is the server that arrived in the DHCP lease, or the one the
 * user set through SYSCALL_NETWORK_CONFIGURE_ETHERNET - waits for the
 * reply against a deadline, and walks the answer section for the first
 * A record. No kernel changes and no new ABI: the network service already
 * demuxes UDP by destination port to per-app sockets.
 */

#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"
#include <netinet/in.h>

#define VIBE_DNS_PORT 53
#define VIBE_DNS_TYPE_A 1u
#define VIBE_DNS_CLASS_IN 1u
#define VIBE_DNS_RECV_BYTES 512
#define VIBE_DNS_QUERY_BYTES 256
#define VIBE_DNS_MAX_HOPS 4

static __attribute__((unused)) void vibe_dns_put16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xffu);
}

static __attribute__((unused)) uint16_t vibe_dns_get16(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

/* Dotted name -> DNS wire format (length-prefixed labels, terminating 0).
 * Returns the encoded length, or -1 on malformed input. */
static __attribute__((unused)) int vibe_dns_encode_name(const char *name,
                                                         uint8_t *out,
                                                         int out_max) {
    int out_index = 0;
    const char *cursor = name;

    if (name == 0 || out == 0 || out_max < 2) {
        return -1;
    }
    while (*cursor != '\0') {
        int label_len = 0;
        int label_start;

        while (*cursor == '.') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        label_start = out_index;
        ++out_index; /* length byte, filled once the label ends */
        if (out_index >= out_max) {
            return -1;
        }
        while (*cursor != '\0' && *cursor != '.') {
            if (label_len >= 63 || out_index >= out_max) {
                return -1;
            }
            out[out_index++] = (uint8_t)*cursor++;
            ++label_len;
        }
        if (label_len == 0) {
            return -1;
        }
        out[label_start] = (uint8_t)label_len;
    }
    if (out_index == 0 || out_index + 1 > out_max) {
        return -1;
    }
    out[out_index++] = 0;
    return out_index;
}

/* Offset of the byte after a (possibly compressed) name, or -1. */
static __attribute__((unused)) int vibe_dns_skip_name(const uint8_t *msg,
                                                       int msg_len,
                                                       int offset) {
    int cursor = offset;
    int jumps = 0;
    int end = -1; /* first byte past the name in the ORIGINAL stream */

    if (msg == 0) {
        return -1;
    }
    while (cursor >= 0 && cursor < msg_len) {
        uint8_t len = msg[cursor];

        if ((len & 0xC0u) == 0xC0u) {
            if (cursor + 1 >= msg_len) {
                return -1;
            }
            if (end < 0) {
                end = cursor + 2; /* data resumes after the 2-byte pointer */
            }
            cursor = (int)(((uint16_t)(len & 0x3Fu) << 8) | (uint16_t)msg[cursor + 1]);
            if (++jumps > 64 || cursor < 0 || cursor >= msg_len) {
                return -1;
            }
            continue;
        }
        if (len == 0u) {
            return end >= 0 ? end : cursor + 1;
        }
        if (cursor + len >= msg_len) {
            return -1;
        }
        cursor += 1 + (int)len;
    }
    return -1;
}

/* Decode a (possibly compressed) name into dotted form. Depth bounds the
 * pointer-chain recursion seen in the wild; loops are rejected. */
static __attribute__((unused)) int vibe_dns_read_name(const uint8_t *msg,
                                                       int msg_len,
                                                       int offset,
                                                       char *out,
                                                       int out_max) {
    int written = 0;
    int cursor = offset;
    int jumps = 0;

    if (msg == 0 || out == 0 || out_max < 2) {
        return -1;
    }
    while (cursor >= 0 && cursor < msg_len) {
        uint8_t len = msg[cursor];

        if ((len & 0xC0u) == 0xC0u) {
            if (cursor + 1 >= msg_len) {
                return -1;
            }
            cursor = (int)(((uint16_t)(len & 0x3Fu) << 8) | (uint16_t)msg[cursor + 1]);
            if (++jumps > 64 || cursor < 0 || cursor >= msg_len) {
                return -1;
            }
            continue;
        }
        if (len == 0u) {
            out[written] = '\0';
            return written > 0 ? 0 : -1;
        }
        if (cursor + len >= msg_len) {
            return -1;
        }
        if (written > 0) {
            if (written + 1 >= out_max) {
                return -1;
            }
            out[written++] = '.';
        }
        for (int i = 0; i < (int)len; ++i) {
            if (written + 1 >= out_max) {
                return -1;
            }
            out[written++] = (char)msg[cursor + 1 + i];
        }
        cursor += 1 + (int)len;
    }
    return -1;
}

/* Parse "a.b.c.d" into four bytes. */
static __attribute__((unused)) int vibe_dns_parse_ipv4(const char *text, uint8_t out[4]) {
    unsigned int octet = 0u;
    unsigned int part = 0u;
    int saw_digit = 0;
    size_t index = 0u;

    if (text == 0 || text[0] == '\0' || out == 0) {
        return -1;
    }
    while (text[index] != '\0') {
        char ch = text[index];

        if (ch >= '0' && ch <= '9') {
            octet = (octet * 10u) + (unsigned int)(ch - '0');
            if (octet > 255u) {
                return -1;
            }
            saw_digit = 1;
        } else if (ch == '.') {
            if (!saw_digit || part >= 3u) {
                return -1;
            }
            out[part++] = (uint8_t)octet;
            octet = 0u;
            saw_digit = 0;
        } else {
            return -1;
        }
        ++index;
    }
    if (!saw_digit || part != 3u) {
        return -1;
    }
    out[3] = (uint8_t)octet;
    return 0;
}

/* Build the query for `name`; writes the txid into *txid_out. */
static __attribute__((unused)) int vibe_dns_build_query(const char *name,
                                                         uint8_t *query,
                                                         int query_max,
                                                         uint16_t *txid_out) {
    int name_len;
    uint16_t txid;

    if (name == 0 || query == 0 || txid_out == 0 || query_max < 12 + 16) {
        return -1;
    }
    memset(query, 0, (size_t)query_max);
    txid = (uint16_t)(vibe_app_millis() ^ ((uint32_t)(uintptr_t)query >> 3));
    if (txid == 0u) {
        txid = 0x4A53u;
    }
    *txid_out = txid;
    vibe_dns_put16(query, txid);
    query[2] = 0x01u; /* flags_hi: RD */
    query[3] = 0x00u;
    query[4] = 0u;
    query[5] = 1u;    /* qdcount = 1 */
    name_len = vibe_dns_encode_name(name, query + 12, query_max - 12 - 4);
    if (name_len < 0) {
        return -1;
    }
    vibe_dns_put16(query + 12 + name_len, (uint16_t)VIBE_DNS_TYPE_A);
    vibe_dns_put16(query + 12 + name_len + 2, (uint16_t)VIBE_DNS_CLASS_IN);
    return 12 + name_len + 4;
}

/* Walk the answer section for the first A record.
 * Returns 0 and fills out_addr/canonical when found. */
static __attribute__((unused)) int vibe_dns_parse_answer(const uint8_t *msg,
                                                          int msg_len,
                                                          char *canonical,
                                                          int canonical_max,
                                                          uint8_t out_addr[4]) {
    int ancount;
    int qdcount;
    int offset = 12;

    if (msg == 0 || msg_len < 12 || out_addr == 0) {
        return -1;
    }
    qdcount = (int)vibe_dns_get16(msg + 4);
    ancount = (int)vibe_dns_get16(msg + 6);
    if (ancount <= 0) {
        return -1;
    }
    for (int i = 0; i < qdcount; ++i) {
        offset = vibe_dns_skip_name(msg, msg_len, offset);
        if (offset < 0 || offset + 4 > msg_len) {
            return -1;
        }
        offset += 4; /* qtype + qclass */
    }
    for (int i = 0; i < ancount; ++i) {
        int record_name_start = offset;
        int record_name_end;
        int type;
        int class_code;
        int rdlength;

        record_name_end = vibe_dns_skip_name(msg, msg_len, offset);
        if (record_name_end < 0 || record_name_end + 10 > msg_len) {
            return -1;
        }
        type = (int)vibe_dns_get16(msg + record_name_end);
        class_code = (int)vibe_dns_get16(msg + record_name_end + 2);
        rdlength = (int)vibe_dns_get16(msg + record_name_end + 8);
        offset = record_name_end + 10;
        if (offset + rdlength > msg_len) {
            return -1;
        }
        if (type == (int)VIBE_DNS_TYPE_A && class_code == (int)VIBE_DNS_CLASS_IN &&
            rdlength == 4) {
            out_addr[0] = msg[offset];
            out_addr[1] = msg[offset + 1];
            out_addr[2] = msg[offset + 2];
            out_addr[3] = msg[offset + 3];
            if (canonical != 0 && canonical_max > 0) {
                /* The owner name starts at record_name_start; skip_name only
                 * reports where it ends. Reading from the end decodes garbage. */
                if (vibe_dns_read_name(msg, msg_len, record_name_start, canonical,
                                       canonical_max) != 0) {
                    canonical[0] = '\0';
                }
            }
            return 0;
        }
        offset += rdlength;
        if (offset > msg_len) {
            return -1;
        }
    }
    return -1;
}

/* Resolve `name` to an IPv4 address using `server_text` (dotted quad).
 * Returns 0 on success and fills out_addr + canonical. */
static __attribute__((unused)) int vibe_dns_resolve(const char *name,
                                                     const char *server_text,
                                                     uint8_t out_addr[4],
                                                     char *canonical,
                                                     int canonical_max) {
    struct sockaddr_in address;
    uint8_t query[VIBE_DNS_QUERY_BYTES];
    uint8_t reply[VIBE_DNS_RECV_BYTES];
    uint8_t server[4];
    uint16_t txid = 0;
    int query_len;
    int handle;
    int hops;

    if (name == 0 || server_text == 0 || out_addr == 0) {
        return -1;
    }
    if (vibe_dns_parse_ipv4(server_text, server) != 0) {
        return -1;
    }
    if (server[0] == 0u && server[1] == 0u && server[2] == 0u && server[3] == 0u) {
        return -1; /* 0.0.0.0 means "no server from the lease yet" */
    }
    query_len = vibe_dns_build_query(name, query, (int)sizeof(query), &txid);
    if (query_len < 0) {
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_len = (uint8_t)sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)VIBE_DNS_PORT);
    /* The kernel reads the 4 address bytes big-endian (be32_load), so the
     * octets must sit in memory in dotted order. Packing them with <<24
     * reverses them on little-endian and the query lands on 3.2.0.10
     * instead of 10.0.2.3 - that was the "no answer" root cause. */
    address.sin_addr.s_addr = (uint32_t)server[0] |
                              ((uint32_t)server[1] << 8) |
                              ((uint32_t)server[2] << 16) |
                              ((uint32_t)server[3] << 24);

    /* protocol MUST be IPPROTO_UDP (17): the kernel classifies sockets by
     * (domain, type, protocol) — a SOCK_DGRAM socket with protocol 0 matches
     * no branch in send/recv, so the query would be silently rejected. */
    handle = vibe_app_network_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle < 0) {
        return -3; /* socket() itself failed */
    }
    if (vibe_app_network_socket_connect(handle,
                                        (const struct sockaddr *)&address,
                                        sizeof(address)) != 0) {
        (void)vibe_app_network_close(handle);
        return -4; /* connect() rejected the sockaddr/port */
    }
    if (vibe_app_network_send(handle, query, (uint32_t)query_len) != query_len) {
        (void)vibe_app_network_close(handle);
        return -5; /* sendto/ARP/virtio path failed */
    }

    /* Two attempts: a single dropped datagram should not fail the lookup. */
    for (hops = 0; hops < 2; ++hops) {
        unsigned long long deadline = vibe_app_millis() + 1500u;

        if (hops > 0) {
            if (vibe_app_network_send(handle, query, (uint32_t)query_len) != query_len) {
                break;
            }
        }
        while (vibe_app_millis() < deadline) {
            int rc = vibe_app_network_recv(handle, reply, (uint32_t)sizeof(reply));

            if (rc >= 12) {
                if (vibe_dns_get16(reply) != txid) {
                    continue; /* not our transaction */
                }
                if ((reply[2] & 0x80u) == 0u) {
                    continue; /* not a response */
                }
                if ((reply[3] & 0x0Fu) == 3u) {
                    (void)vibe_app_network_close(handle);
                    return -2; /* NXDOMAIN */
                }
                if ((reply[3] & 0x0Fu) != 0u) {
                    (void)vibe_app_network_close(handle);
                    return -1; /* server failure / refused / etc. */
                }
                rc = vibe_dns_parse_answer(reply, rc, canonical, canonical_max, out_addr);
                (void)vibe_app_network_close(handle);
                return rc == 0 ? 0 : -1;
            }
            vibe_app_yield();
            (void)vibe_app_sleep_ms(10u);
        }
    }
    (void)vibe_app_network_close(handle);
    return -1;
}

#endif