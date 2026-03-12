#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/utils.h>

#include "sectorc_internal.h"

void sectorc_write(const char *text) {
    console_write(text);
}

void sectorc_write_line(const char *text) {
    console_write(text);
    console_putc('\n');
}

void sectorc_putc(char c) {
    console_putc(c);
}

void sectorc_write_int(int value) {
    char buf[16];
    unsigned int uvalue;
    int pos = 0;

    if (value < 0) {
        sectorc_putc('-');
        uvalue = (unsigned int)(-(value + 1)) + 1u;
    } else {
        uvalue = (unsigned int)value;
    }

    if (uvalue == 0u) {
        sectorc_putc('0');
        return;
    }

    while (uvalue > 0u && pos < (int)sizeof(buf)) {
        buf[pos++] = (char)('0' + (uvalue % 10u));
        uvalue /= 10u;
    }

    while (pos > 0) {
        sectorc_putc(buf[--pos]);
    }
}

int sectorc_read_source(const char *path, const char **out_source) {
    int node = fs_resolve(path);

    if (node < 0 || g_fs_nodes[node].is_dir) {
        return -1;
    }

    *out_source = g_fs_nodes[node].data;
    return 0;
}

void sectorc_copy_string(char *dst, const char *src, int max_len) {
    str_copy_limited(dst, src, max_len);
}

int sectorc_string_equal(const char *a, const char *b) {
    return str_eq(a, b);
}

int sectorc_string_length(const char *text) {
    return str_len(text);
}

void sectorc_set_error(char *dst, int max_len, const char *message, int line) {
    int pos = 0;
    char num[16];
    int digits = 0;
    unsigned int uline;

    if (max_len <= 0) {
        return;
    }

    dst[0] = '\0';
    str_copy_limited(dst, "sectorc: ", max_len);
    pos = str_len(dst);

    while (*message != '\0' && pos < (max_len - 1)) {
        dst[pos++] = *message++;
    }

    if (line > 0 && pos < (max_len - 1)) {
        dst[pos++] = ' ';
        dst[pos++] = '(';
        dst[pos++] = 'l';
        dst[pos++] = 'i';
        dst[pos++] = 'n';
        dst[pos++] = 'h';
        dst[pos++] = 'a';
        dst[pos++] = ' ';
        uline = (unsigned int)line;
        if (uline == 0u) {
            num[digits++] = '0';
        } else {
            while (uline > 0u && digits < (int)sizeof(num)) {
                num[digits++] = (char)('0' + (uline % 10u));
                uline /= 10u;
            }
        }
        while (digits > 0 && pos < (max_len - 1)) {
            dst[pos++] = num[--digits];
        }
        if (pos < (max_len - 1)) {
            dst[pos++] = ')';
        }
    }

    dst[pos] = '\0';
}
