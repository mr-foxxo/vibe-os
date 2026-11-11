#include <monetary.h>
#include <compat/posix/errno.h>
#include <compat/libc/string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

ssize_t strfmon(char *s, size_t max, const char *format, ...) {
    va_list ap;
    size_t pos = 0;
    int i;

    if (!s || max == 0 || !format) {
        errno = EINVAL;
        return -1;
    }

    va_start(ap, format);
    for (i = 0; format[i] != '\0' && pos < max; i++) {
        if (format[i] == '%') {
            int width = 0;
            int precision = -1;
            int conv_done = 0;

            i++;
            if (format[i] == '=') {
                i++;
                if (format[i]) i++;
            }
            if (format[i] == '^') {
                i++;
            }
            if (format[i] == '+' || format[i] == '(') {
                i++;
            }
            if (!conv_done && format[i] == '!') {
                i++;
            }
            if (format[i] == '-') {
                i++;
            }
            while (format[i] >= '0' && format[i] <= '9') {
                width = width * 10 + (format[i] - '0');
                i++;
            }
            (void)width;
            if (format[i] == '#') {
                i++;
            }
            if (format[i] == '.') {
                i++;
                precision = 0;
                while (format[i] >= '0' && format[i] <= '9') {
                    precision = precision * 10 + (format[i] - '0');
                    i++;
                }
            }
            if (format[i] == 'i' || format[i] == 'n') {
                double val = va_arg(ap, double);
                char num_buf[64];
                int int_part;
                int frac_part;
                int neg = 0;
                size_t num_len;
                int p;

                if (val < 0) { neg = 1; val = -val; }
                if (precision < 0) precision = 2;
                int_part = (int)val;
                frac_part = (int)((val - (double)int_part) * 100.0 + 0.5);
                if (frac_part >= 100) { int_part++; frac_part -= 100; }
                {
                    int ti = int_part;
                    int digit_count = 0;
                    int nlen = 0;

                    if (neg) num_buf[nlen++] = '-';
                    num_buf[nlen++] = '$';
                    if (ti == 0) {
                        num_buf[nlen++] = '0';
                    } else {
                        int digits[32];
                        int dc = 0;
                        while (ti > 0) { digits[dc++] = ti % 10; ti /= 10; }
                        for (p = dc - 1; p >= 0; p--) {
                            if (digit_count > 0 && digit_count % 3 == 0) num_buf[nlen++] = ',';
                            num_buf[nlen++] = '0' + digits[p];
                            digit_count++;
                        }
                    }
                    num_buf[nlen++] = '.';
                    num_buf[nlen++] = '0' + (frac_part / 10);
                    num_buf[nlen++] = '0' + (frac_part % 10);
                    num_buf[nlen] = '\0';
                    num_len = (size_t)nlen;
                }
                {
                    size_t copy_len = num_len;
                    if (pos + copy_len > max - 1) copy_len = max - 1 - pos;
                    memcpy(s + pos, num_buf, copy_len);
                    pos += copy_len;
                }
                conv_done = 1;
            }
            if (!conv_done) {
                if (pos < max - 1) s[pos++] = format[i];
            }
        } else {
            if (pos < max - 1) s[pos++] = format[i];
        }
    }
    va_end(ap);

    s[pos] = '\0';
    return (ssize_t)pos;
}
