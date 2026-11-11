#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"
#include "applications/ported/include/http_client.h"

static int curl_starts_with(const char *text, const char *prefix) {
    if (text == 0 || prefix == 0) {
        return 0;
    }
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static const char *curl_resolve_file_target(const char *arg) {
    if (arg == 0) {
        return 0;
    }
    if (curl_starts_with(arg, "file://")) {
        return arg + 7;
    }
    if (arg[0] == '/') {
        return arg;
    }
    return 0;
}

static void curl_usage(void) {
    fprintf(stderr, "usage: curl <url|file>\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;
    const char *data = 0;
    int size = 0;

    if (argc != 2) {
        curl_usage();
        return 1;
    }

    target = curl_resolve_file_target(argv[1]);
    if (target != 0) {
        if (vibe_app_read_file(target, &data, &size) != 0 || data == 0 || size < 0) {
            fprintf(stderr, "curl: failed to read %s\n", target);
            return 1;
        }
        if (size > 0) {
            (void)fwrite(data, 1u, (size_t)size, stdout);
            if (data[size - 1] != '\n') {
                putchar('\n');
            }
        }
        printf("curl: file-ok target=%s bytes=%d\n", target, size);
        netdiag_debugf("curl: file-ok target=%s bytes=%d\n", target, size);
        return 0;
    }

    if (netdiag_load_snapshot("curl", &info, &status) != 0) {
        return 1;
    }
    if (netdiag_require_real_packet_path("curl", "remote transfer", &info, &status) != 0) {
        return 1;
    }

    /* Item 1.6: HTTP GET over the stack's own TCP client. Literal IPv4 or
     * a real A-record query (item 1.4 resolver); failures are named,
     * never a silent empty success. */
    if (curl_starts_with(argv[1], "http://")) {
        static uint8_t body[VIBE_HTTP_BODY_MAX];
        char server[40];
        char line0[64];
        uint32_t body_len = 0u;
        uint32_t status_code = 0u;
        int hrc;
        const char *srv = 0;

        server[0] = '\0';
        if (vibe_http_pick_server(&status, server, (int)sizeof(server)) == 0) {
            srv = server;
        }
        hrc = vibe_http_get(argv[1], srv, body, (uint32_t)sizeof(body),
                            &body_len, &status_code, line0, (int)sizeof(line0));
        if (hrc == 0 || hrc == -5) {
            char body0[20];
            uint32_t i = 0u;
            int o = 0;

            /* first bytes of the BODY (past the header block) so the proof
             * carries served content, not just a status line */
            while (i + 3u < body_len &&
                   !(body[i] == '\r' && body[i + 1] == '\n' &&
                     body[i + 2] == '\r' && body[i + 3] == '\n')) {
                i++;
            }
            if (i + 3u < body_len) {
                i += 4u;
            }
            while (i < body_len && o < 16 && body[i] != '\r' && body[i] != '\n') {
                body0[o++] = (char)body[i++];
            }
            body0[o] = '\0';
            /* Witness first, body second: the console echo is per-char IPC
             * and slow under TCG; the proof of a real GET must not wait on
             * the transcription finishing. */
            netdiag_debugf("curl: http-ok url=%s status=%u bytes=%u line0=%s body0=%s\n",
                           argv[1], (unsigned)status_code, (unsigned)body_len, line0, body0);
            if (body_len > 0u) {
                (void)fwrite(body, 1u, body_len, stdout);
            }
            printf("curl: http-ok url=%s status=%u bytes=%u\n",
                   argv[1], (unsigned)status_code, (unsigned)body_len);
            return 0;
        }
        fprintf(stderr, "curl: http failed url=%s rc=%d\n", argv[1], hrc);
        netdiag_debugf("curl: http-fail url=%s rc=%d dns=%s\n",
                       argv[1], hrc, srv != 0 ? srv : "-");
        return 1;
    }

    fprintf(stderr,
            "curl: remote transfer not implemented yet url=%s active=%s dns=%s\n",
            argv[1],
            status.active_if[0] != '\0' ? status.active_if : "-",
            status.dns_server[0] != '\0' ? status.dns_server : "-");
    netdiag_debugf("curl: remote transfer pending url=%s active=%s dns=%s\n",
                   argv[1],
                   status.active_if[0] != '\0' ? status.active_if : "-",
                   status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 1;
}
