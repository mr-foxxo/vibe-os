#include <kernel/drivers/network/wifi_data_plane.h>
#include <kernel/kernel_string.h>
#include <kernel/kernel.h>
#include <stddef.h>

#define WPA2_KEY_VERSION_1  1
#define EAPOL_VERSION       2
#define EAPOL_TYPE_KEY      3

#define EAPOL_KEY_INFO_TYPE_MASK        0x0007u
#define EAPOL_KEY_INFO_INSTALL          0x0040u
#define EAPOL_KEY_INFO_ACK              0x0080u
#define EAPOL_KEY_INFO_MIC              0x0100u
#define EAPOL_KEY_INFO_SECURE           0x0200u
#define EAPOL_KEY_INFO_ERROR            0x0400u
#define EAPOL_KEY_INFO_REQUEST          0x0800u
#define EAPOL_KEY_INFO_ENCRYPTED        0x1000u
#define EAPOL_KEY_INFO_SMK_MESSAGE      0x2000u

#define SHA1_BLOCK_SIZE 64
#define SHA1_DIGEST_SIZE 20

struct wpa2_ptk {
    uint8_t kck[16];
    uint8_t kek[16];
    uint8_t tk[16];
};

struct eapol_key {
    uint8_t version;
    uint8_t type;
    uint16_t length;
    uint8_t key_info_version;
    uint16_t key_length;
    uint64_t replay_counter;
    uint8_t key_nonce[32];
    uint8_t key_iv[16];
    uint8_t key_rsc[8];
    uint8_t key_id[8];
    uint8_t key_mic[16];
    uint16_t key_data_length;
} __attribute__((packed));

#define EAPOL_KEY_HDR_SIZE 95u

struct wpa2_handshake {
    int step;
    uint8_t pmk[32];
    struct wpa2_ptk ptk;
    uint8_t anonce[32];
    uint8_t snonce[32];
    uint64_t replay_counter;
    uint32_t timeout;
};

static struct wpa2_handshake g_handshake;

static void sha1_transform(uint32_t *state, const uint8_t *block) {
    uint32_t w[80];
    uint32_t a, b, c, d, e, temp;
    int i;

    for (i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 80; ++i) {
        w[i] = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = (w[i] << 1) | (w[i] >> 31);
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

    for (i = 0; i < 80; ++i) {
        if (i < 20) {
            temp = ((a << 5) | (a >> 27)) + ((b & c) | (~b & d)) + e + w[i] + 0x5A827999u;
        } else if (i < 40) {
            temp = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + w[i] + 0x6ED9EBA1u;
        } else if (i < 60) {
            temp = ((a << 5) | (a >> 27)) + ((b & c) | (b & d) | (c & d)) + e + w[i] + 0x8F1BBCDCu;
        } else {
            temp = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + w[i] + 0xCA62C1D6u;
        }
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void sha1_init_state(uint32_t *state) {
    state[0] = 0x67452301u;
    state[1] = 0xEFCDAB89u;
    state[2] = 0x98BADCFEu;
    state[3] = 0x10325476u;
    state[4] = 0xC3D2E1F0u;
}

static void sha1_hash(const uint8_t *data, uint32_t len, uint8_t *out) {
    uint32_t state[5];
    uint64_t bit_count = (uint64_t)len * 8u;
    uint8_t block[SHA1_BLOCK_SIZE];
    uint32_t i, remaining, offset = 0;

    sha1_init_state(state);

    while (len >= SHA1_BLOCK_SIZE) {
        sha1_transform(state, data + offset);
        offset += SHA1_BLOCK_SIZE;
        len -= SHA1_BLOCK_SIZE;
    }

    remaining = len;
    memset(block, 0, SHA1_BLOCK_SIZE);
    memcpy(block, data + offset, remaining);
    block[remaining] = 0x80u;

    if (remaining >= 56) {
        sha1_transform(state, block);
        memset(block, 0, SHA1_BLOCK_SIZE);
    }

    for (i = 0; i < 8; ++i) {
        block[56 + i] = (uint8_t)((bit_count >> (56 - i * 8)) & 0xFFu);
    }
    sha1_transform(state, block);

    for (i = 0; i < 5; ++i) {
        out[i * 4] = (uint8_t)((state[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = (uint8_t)((state[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = (uint8_t)((state[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = (uint8_t)(state[i] & 0xFFu);
    }
}

static void hmac_sha1(const uint8_t *key, uint32_t key_len,
                      const uint8_t *msg, uint32_t msg_len,
                      uint8_t *out) {
    uint8_t k_ipad[SHA1_BLOCK_SIZE];
    uint8_t k_opad[SHA1_BLOCK_SIZE];
    uint8_t tk[SHA1_DIGEST_SIZE];
    uint8_t inner[SHA1_BLOCK_SIZE + 64];
    uint32_t i;

    if (key_len > SHA1_BLOCK_SIZE) {
        sha1_hash(key, key_len, tk);
        key = tk;
        key_len = SHA1_DIGEST_SIZE;
    }

    memset(k_ipad, 0, SHA1_BLOCK_SIZE);
    memset(k_opad, 0, SHA1_BLOCK_SIZE);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);

    for (i = 0; i < SHA1_BLOCK_SIZE; ++i) {
        k_ipad[i] ^= 0x36u;
        k_opad[i] ^= 0x5Cu;
    }

    memcpy(inner, k_ipad, SHA1_BLOCK_SIZE);
    memcpy(inner + SHA1_BLOCK_SIZE, msg, msg_len);
    sha1_hash(inner, SHA1_BLOCK_SIZE + msg_len, tk);

    memcpy(inner, k_opad, SHA1_BLOCK_SIZE);
    memcpy(inner + SHA1_BLOCK_SIZE, tk, SHA1_DIGEST_SIZE);
    sha1_hash(inner, SHA1_BLOCK_SIZE + SHA1_DIGEST_SIZE, out);
}

static void pbkdf2_sha1(const uint8_t *password, uint32_t pw_len,
                        const uint8_t *salt, uint32_t salt_len,
                        uint32_t iterations, uint32_t dk_len, uint8_t *dk) {
    uint32_t block_count = (dk_len + SHA1_DIGEST_SIZE - 1u) / SHA1_DIGEST_SIZE;
    uint32_t i, j, k;

    for (i = 1u; i <= block_count; ++i) {
        uint8_t u[SHA1_DIGEST_SIZE];
        uint8_t t[SHA1_DIGEST_SIZE];
        uint8_t block_salt[64];
        uint32_t block_salt_len = salt_len + 4;

        memcpy(block_salt, salt, salt_len);
        block_salt[salt_len] = (uint8_t)((i >> 24) & 0xFFu);
        block_salt[salt_len + 1] = (uint8_t)((i >> 16) & 0xFFu);
        block_salt[salt_len + 2] = (uint8_t)((i >> 8) & 0xFFu);
        block_salt[salt_len + 3] = (uint8_t)(i & 0xFFu);

        hmac_sha1(password, pw_len, block_salt, block_salt_len, u);
        memcpy(t, u, SHA1_DIGEST_SIZE);

        for (j = 1u; j < iterations; ++j) {
            hmac_sha1(password, pw_len, u, SHA1_DIGEST_SIZE, u);
            for (k = 0u; k < SHA1_DIGEST_SIZE; ++k) {
                t[k] ^= u[k];
            }
        }

        for (j = 0u; j < SHA1_DIGEST_SIZE; ++j) {
            uint32_t idx = (i - 1u) * SHA1_DIGEST_SIZE + j;
            if (idx < dk_len) {
                dk[idx] = t[j];
            }
        }
    }
}

static void wpa2_derive_pmk(const uint8_t *passphrase, int pass_len,
                            const uint8_t *ssid, int ssid_len, uint8_t *pmk) {
    pbkdf2_sha1(passphrase, (uint32_t)pass_len, ssid, (uint32_t)ssid_len,
                4096u, 32u, pmk);
}

static void hmac_sha1_prf(const uint8_t *key, uint32_t key_len,
                          const uint8_t *label, uint32_t label_len,
                          const uint8_t *context, uint32_t context_len,
                          uint32_t out_len, uint8_t *out) {
    uint32_t i, j, offset = 0u;
    uint8_t input[256];
    uint32_t input_len;
    uint8_t digest[SHA1_DIGEST_SIZE];
    uint8_t counter;

    for (i = 0u; offset < out_len; ++i) {
        counter = (uint8_t)(i + 1u);
        input_len = 0u;
        input[input_len++] = counter;
        memcpy(input + input_len, label, label_len);
        input_len += label_len;
        input[input_len++] = 0u;
        memcpy(input + input_len, context, context_len);
        input_len += context_len;

        input[input_len++] = (uint8_t)((out_len >> 8) & 0xFFu);
        input[input_len++] = (uint8_t)(out_len & 0xFFu);

        hmac_sha1(key, key_len, input, input_len, digest);

        for (j = 0u; j < SHA1_DIGEST_SIZE && offset < out_len; ++j) {
            out[offset++] = digest[j];
        }
    }
}

static void wpa2_derive_ptk(const uint8_t *pmk,
                            const uint8_t *ap_mac,
                            const uint8_t *sta_mac,
                            const uint8_t *anonce,
                            const uint8_t *snonce,
                            struct wpa2_ptk *ptk) {
    uint8_t context[76];
    uint8_t label[] = "Pairwise key expansion";
    int cmp;

    cmp = memcmp(ap_mac, sta_mac, 6);
    if (cmp <= 0) {
        memcpy(context, ap_mac, 6);
        memcpy(context + 6, sta_mac, 6);
    } else {
        memcpy(context, sta_mac, 6);
        memcpy(context + 6, ap_mac, 6);
    }

    cmp = memcmp(anonce, snonce, 32);
    if (cmp <= 0) {
        memcpy(context + 12, anonce, 32);
        memcpy(context + 44, snonce, 32);
    } else {
        memcpy(context + 12, snonce, 32);
        memcpy(context + 44, anonce, 32);
    }

    hmac_sha1_prf(pmk, 32, label, (uint32_t)(sizeof(label) - 1u),
                  context, 76u, 48u, (uint8_t *)ptk);
}

static int wpa2_verify_mic(const uint8_t *kck, const uint8_t *frame, uint16_t frame_len) {
    const struct eapol_key *key = (const struct eapol_key *)frame;
    uint8_t saved_mic[16];
    uint8_t computed_mic[20];

    memcpy(saved_mic, key->key_mic, 16);
    memset((uint8_t *)key->key_mic, 0, 16);

    hmac_sha1(kck, 16, frame, frame_len, computed_mic);

    memcpy((uint8_t *)key->key_mic, saved_mic, 16);

    return (memcmp(computed_mic, saved_mic, 16) == 0) ? 0 : -1;
}

static void wpa2_build_mic(const uint8_t *kck, uint8_t *frame, uint16_t frame_len) {
    uint8_t mic[20];
    struct eapol_key *key = (struct eapol_key *)frame;

    memset(key->key_mic, 0, 16);
    hmac_sha1(kck, 16, frame, frame_len, mic);
    memcpy(key->key_mic, mic, 16);
}

__attribute__((unused))
static void wpa2_aes_wrap(const uint8_t *kek, const uint8_t *plain, uint32_t n, uint8_t *cipher) {
    uint8_t a[8];
    uint8_t b[16];
    uint8_t r[8][8];
    uint32_t i, j, t;

    memcpy(a, plain, 8);
    for (i = 0u; i < n; ++i) {
        memcpy(r[i], plain + 8 + i * 8, 8);
    }

    for (j = 0u; j <= 5u; ++j) {
        for (i = 0u; i < n; ++i) {
            memset(b, 0, 16);
            memcpy(b, a, 8);
            memcpy(b + 8, r[i], 8);

            for (uint32_t x = 0u; x < 16; ++x) {
                b[x] ^= kek[x];
            }

            t = n * j + i + 1u;
            a[7] ^= (uint8_t)(t & 0xFFu);
            a[6] ^= (uint8_t)((t >> 8) & 0xFFu);

            memcpy(a, b, 8);
            memcpy(r[i], b + 8, 8);
        }
    }

    memcpy(cipher, a, 8);
    for (i = 0u; i < n; ++i) {
        memcpy(cipher + 8 + i * 8, r[i], 8);
    }
}

static void wpa2_aes_unwrap(const uint8_t *kek, const uint8_t *cipher, uint32_t n, uint8_t *plain) {
    uint8_t a[8];
    uint8_t b[16];
    uint8_t r[8][8];
    uint32_t i, j, t;

    memcpy(a, cipher, 8);
    for (i = 0u; i < n; ++i) {
        memcpy(r[i], cipher + 8 + i * 8, 8);
    }

    for (j = 5u; j > 0u; --j) {
        for (i = n; i > 0u; --i) {
            t = n * (j - 1u) + i;
            a[7] ^= (uint8_t)(t & 0xFFu);
            a[6] ^= (uint8_t)((t >> 8) & 0xFFu);

            memset(b, 0, 16);
            memcpy(b, a, 8);
            memcpy(b + 8, r[i - 1], 8);

            for (uint32_t x = 0u; x < 16; ++x) {
                b[x] ^= kek[x];
            }

            memcpy(a, b, 8);
            memcpy(r[i - 1], b + 8, 8);
        }
    }

    memcpy(plain, a, 8);
    for (i = 0u; i < n; ++i) {
        memcpy(plain + 8 + i * 8, r[i], 8);
    }
}

static uint16_t wpa2_build_eapol_response(uint8_t *frame,
                                          const struct wpa2_handshake *hs,
                                          int msg_num) {
    struct eapol_key *key = (struct eapol_key *)frame;
    uint16_t key_info = 0;
    (void)key_info;

    memset(key, 0, 95);  /* Only zero the fixed header, not key_data */

    key->version = EAPOL_VERSION;
    key->type = EAPOL_TYPE_KEY;
    key->length = 0;
    key->key_length = 16;
    key->key_data_length = 0;

    key->replay_counter = hs->replay_counter;

    if (msg_num == 2) {
        key_info = EAPOL_KEY_INFO_TYPE_MASK | EAPOL_KEY_INFO_MIC;
        key->key_info_version = (uint8_t)(WPA2_KEY_VERSION_1 | 0x01u);
        key->key_length = 0;
        memcpy(key->key_nonce, hs->snonce, 32);
    } else if (msg_num == 4) {
        key_info = EAPOL_KEY_INFO_SECURE | EAPOL_KEY_INFO_MIC;
        key->key_info_version = (uint8_t)(WPA2_KEY_VERSION_1 | 0x03u);
    }

    return 95;  /* offset of key_data - don't include empty payload */
}

static int wpa2_process_eapol_key(const uint8_t *frame,
                                  uint16_t frame_len,
                                  struct wpa2_handshake *hs,
                                  struct wifi_device *dev,
                                  uint8_t *response, uint16_t *response_len) {
    const struct eapol_key *key = (const struct eapol_key *)frame;

    if (frame_len < EAPOL_KEY_HDR_SIZE) return -1;

    if ((key->replay_counter & 0xFFu) <= (hs->replay_counter & 0xFFu) &&
        key->replay_counter != 0) {
        return -1;
    }
    hs->replay_counter = key->replay_counter;

    if (hs->step == 0) {
        memcpy(hs->anonce, key->key_nonce, 32);

        for (int i = 0; i < 32; ++i) {
            uint32_t r = (uint32_t)(i * 0x9E3779B9u);
            hs->snonce[i] = (uint8_t)((r >> (i % 4u) * 8u) & 0xFFu);
        }

        wpa2_derive_ptk(hs->pmk, dev->current_ap.bssid.octets,
                        dev->mac_addr, hs->anonce, hs->snonce, &hs->ptk);

        *response_len = wpa2_build_eapol_response(response, hs, 2);
        wpa2_build_mic(hs->ptk.kck, response, *response_len);
        hs->step = 1;
        return 1;
    } else if (hs->step == 1) {
        if (wpa2_verify_mic(hs->ptk.kck, frame, frame_len) != 0) {
            return -1;
        }

        hs->step = 2;

        *response_len = wpa2_build_eapol_response(response, hs, 4);
        wpa2_build_mic(hs->ptk.kck, response, *response_len);
        hs->step = 3;
        return 2;
    }

    return -1;
}

int wpa2_handshake_start(struct wifi_device *dev,
                         const struct wifi_credentials *creds) {
    if (!dev || !creds) return -1;

    for (int i = 0; i < 32; ++i) {
        uint32_t r = (uint32_t)(i * 0x9E3779B9u + 0xDEADBEEFu);
        g_handshake.snonce[i] = (uint8_t)((r >> (i % 4u) * 8u) & 0xFFu);
    }

    wpa2_derive_pmk((const uint8_t *)creds->passphrase,
                    (int)strlen((const char *)creds->passphrase),
                    (const uint8_t *)creds->ssid,
                    (int)strlen((const char *)creds->ssid),
                    g_handshake.pmk);

    g_handshake.step = 0;
    g_handshake.replay_counter = 0;
    g_handshake.timeout = 0;

    dev->connection_state = WIFI_STATE_ASSOCIATING;
    return 0;
}

int wpa2_handshake_continue(struct wifi_device *dev, const uint8_t *frame, uint16_t len) {
    int result;
    uint8_t response[512];
    uint16_t response_len = 0;

    if (!dev || !frame) return -1;

    result = wpa2_process_eapol_key(frame, len, &g_handshake, dev, response, &response_len);

    if (result == 1 || result == 2) {
        if (response_len > 0) {
            if (wifi_device_send_frame(dev, response, response_len) != 0) return -1;
        }
        if (result == 2) {
            g_handshake.step = 3;
        }
    }

    if (g_handshake.step >= 3) {
        dev->connection_state = WIFI_STATE_CONNECTED;
        return 0;
    }

    return (result < 0) ? result : 0;
}

int wpa2_handshake_get_state(void) {
    return g_handshake.step;
}

const struct wpa2_ptk *wpa2_get_ptk(void) {
    if (g_handshake.step >= 3) {
        return &g_handshake.ptk;
    }
    return 0;
}

int wpa2_derive_gtk(const uint8_t *kek, const uint8_t *encrypted_gtk,
                    uint32_t gtk_len, uint8_t *gtk) {
    if (!kek || !encrypted_gtk || !gtk || gtk_len < 24) return -1;

    wpa2_aes_unwrap(kek, encrypted_gtk, (gtk_len - 8u) / 8u, gtk);
    return 0;
}
