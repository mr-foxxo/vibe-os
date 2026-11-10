/* Host-side unit test for the DNS wire helpers in
 * applications/ported/include/dns_client.h: name encoding, compressed-name
 * decoding, and A-record extraction from a real-shaped reply.
 *
 * The header only needs the pure functions here, so this links against
 * app_runtime.c for vibe_app_millis()/memset/memcpy and skips any socket
 * call. Diagnostics use a raw i386 write(2) because the runtime owns the
 * usual stdio symbols.
 *
 * Build+run: make test-dns
 */
#define EOF (-1)

#include "compat/include/compat.h"
#include "applications/ported/include/dns_client.h"

static void tprint(const char *s) {
    int len = 0;

    while (s[len] != '\0') {
        ++len;
    }
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory");
}

static int t_strlen(const char *s) {
    int n = 0;

    while (s[n] != '\0') ++n;
    return n;
}

static int failures;

#define CHECK(cond, name) do { \
    if (!(cond)) { tprint("FAIL "); tprint(name); tprint("\n"); ++failures; } \
    else { tprint("pass "); tprint(name); tprint("\n"); } \
} while (0)

int main(void) {
    /* ---- name encoding ---- */
    {
        uint8_t wire[64];
        int len = vibe_dns_encode_name("example.com", wire, (int)sizeof(wire));

        CHECK(len == 13, "encode example.com is 13 bytes");
        CHECK(wire[0] == 7u, "first label length is 7");
        CHECK(memcmp(wire + 1, "example", 7u) == 0, "first label bytes");
        CHECK(wire[8] == 3u, "second label length is 3");
        CHECK(memcmp(wire + 9, "com", 3u) == 0, "second label bytes");
        CHECK(wire[12] == 0u, "trailing root label");
    }
    CHECK(vibe_dns_encode_name("", (uint8_t[16]){0}, 16) < 0, "empty name rejected");
    CHECK(vibe_dns_encode_name("a..b", (uint8_t[16]){0}, 16) >= 0, "double dot tolerated");

    /* ---- query build ---- */
    {
        uint8_t query[VIBE_DNS_QUERY_BYTES];
        uint16_t txid = 0;
        int len = vibe_dns_build_query("example.com", query, (int)sizeof(query), &txid);

        CHECK(len == 12 + 13 + 4, "query length = header+name+qtype/qclass");
        CHECK(txid != 0u, "txid is non-zero");
        CHECK(vibe_dns_get16(query) == txid, "txid in header");
        CHECK((query[2] & 0x01u) != 0u, "RD flag set");
        CHECK(vibe_dns_get16(query + 4) == 1u, "qdcount is 1");
        CHECK(vibe_dns_get16(query + 12 + 13) == VIBE_DNS_TYPE_A, "qtype is A");
        CHECK(vibe_dns_get16(query + 12 + 13 + 2) == VIBE_DNS_CLASS_IN, "qclass is IN");
    }

    /* ---- compressed name decoding + A record extraction ----
     * Reply shape: header, question example.com A IN, answer with a
     * pointer back to offset 12 and rdlength 4 -> 93.184.216.34. */
    {
        uint8_t reply[64];
        int offset = 0;
        char canonical[64];
        char wire[32];
        uint8_t addr[4];
        int name_len = vibe_dns_encode_name("example.com", (uint8_t *)wire, (int)sizeof(wire));
        int rc;

        memset(reply, 0, sizeof(reply));
        vibe_dns_put16(reply, 0x1234u);      /* id */
        reply[2] = 0x81u;                     /* QR + RD */
        reply[3] = 0x80u;                     /* RA, rcode 0 */
        vibe_dns_put16(reply + 4, 1u);        /* qdcount */
        vibe_dns_put16(reply + 6, 1u);        /* ancount */
        offset = 12;
        memcpy(reply + offset, wire, (size_t)name_len);
        offset += name_len;
        vibe_dns_put16(reply + offset, (uint16_t)VIBE_DNS_TYPE_A);
        offset += 2;
        vibe_dns_put16(reply + offset, (uint16_t)VIBE_DNS_CLASS_IN);
        offset += 2;
        /* answer: name pointer 0xC00C, type A, class IN, ttl 0, rdlength 4 */
        reply[offset++] = 0xC0u;
        reply[offset++] = 0x0Cu;
        vibe_dns_put16(reply + offset, (uint16_t)VIBE_DNS_TYPE_A);
        offset += 2;
        vibe_dns_put16(reply + offset, (uint16_t)VIBE_DNS_CLASS_IN);
        offset += 2;
        reply[offset++] = 0u; reply[offset++] = 0u;
        reply[offset++] = 0u; reply[offset++] = 0x3Cu;
        vibe_dns_put16(reply + offset, 4u);
        offset += 2;
        reply[offset++] = 93u;
        reply[offset++] = 184u;
        reply[offset++] = 216u;
        reply[offset++] = 34u;

        memset(canonical, 0, sizeof(canonical));
        rc = vibe_dns_parse_answer(reply, offset, canonical, (int)sizeof(canonical), addr);
        CHECK(rc == 0, "parse_answer finds the A record");
        CHECK(addr[0] == 93u && addr[1] == 184u && addr[2] == 216u && addr[3] == 34u,
              "address bytes 93.184.216.34");
        CHECK(strcmp(canonical, "example.com") == 0, "canonical name decoded through the pointer");
    }

    /* ---- ancount == 0 must be a miss, not a crash ---- */
    {
        uint8_t reply[12];
        uint8_t addr[4];
        char canonical[8];

        memset(reply, 0, sizeof(reply));
        vibe_dns_put16(reply + 4, 1u);
        vibe_dns_put16(reply + 6, 0u);
        CHECK(vibe_dns_parse_answer(reply, 12, canonical, (int)sizeof(canonical), addr) < 0,
              "zero answers reports miss");
    }

    /* ---- a compression loop must be rejected, not hang ---- */
    {
        uint8_t loop[14];
        uint8_t addr[4];
        char canonical[16];

        memset(loop, 0, sizeof(loop));
        vibe_dns_put16(loop + 6, 1u); /* one answer */
        loop[12] = 0xC0u;
        loop[13] = 0x0Cu;            /* points at itself */
        CHECK(vibe_dns_parse_answer(loop, 14, canonical, (int)sizeof(canonical), addr) < 0,
              "self-referential pointer rejected");
    }

    /* ---- ipv4 text parsing used for server selection ---- */
    {
        uint8_t ip[4];

        CHECK(vibe_dns_parse_ipv4("10.0.2.3", ip) == 0 &&
              ip[0] == 10u && ip[1] == 0u && ip[2] == 2u && ip[3] == 3u,
              "parse 10.0.2.3");
        CHECK(vibe_dns_parse_ipv4("256.0.0.1", ip) != 0, "reject octet > 255");
        CHECK(vibe_dns_parse_ipv4("1.2.3", ip) != 0, "reject short address");
    }

    if (failures == 0) {
        tprint("ALL PASS\n");
        return 0;
    }
    tprint("FAILURES: ");
    {
        char buf[4];
        int i = 0;
        int v = failures;

        buf[i++] = (char)('0' + (v % 10));
        buf[i] = '\0';
        tprint(buf);
    }
    tprint("\n");
    return failures;
}