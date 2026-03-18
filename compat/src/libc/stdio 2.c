/*
 * Standard I/O Implementation
 * compat/src/libc/stdio.c
 * 
 * Core printf/fprintf implementation
 */

#include <compat/libc/stdio.h>
#include <compat/libc/stdlib.h>
#include <compat/libc/string.h>
#include <compat/libc/ctype.h>
#include <lang/include/vibe_app_runtime.h>

/* Simple FILE handling - minimal */

FILE *stdin = NULL;
FILE *stdout = NULL;  
FILE *stderr = NULL;

int putchar(int c) {
    vibe_app_console_putc((char)c);
    return c;
}

int puts(const char *s) {
    if (s) vibe_app_console_write(s);
    vibe_app_console_putc('\n');
    return 0;
}

int getchar(void) {
    return vibe_app_poll_key();
}

/* Core printf implementation */

static int vsnprintf_internal(char *str, size_t size, const char *fmt, va_list ap) {
    size_t written = 0;
    
    if (!fmt) return -1;
    
    while (*fmt && written < size - 1) {
        if (*fmt == '%') {
            fmt++;
            if (!*fmt) break;
            
            /* Handle format specifiers */
            if (*fmt == '%') {
                if (str) str[written] = '%';
                written++;
                fmt++;
            } else if (*fmt == 'd' || *fmt == 'i') {
                /* Integer */
                int val = va_arg(ap, int);
                char buf[32];
                int len = 0;
                
                if (val < 0) {
                    if (str && written < size) str[written] = '-';
                    written++;
                    val = -val;
                }
                
                if (val == 0) {
                    buf[len++] = '0';
                } else {
                    int tmp = val;
                    while (tmp > 0) { buf[len++] = '0' + (tmp % 10); tmp /= 10; }
                    for (int i = 0; i < len / 2; i++) {
                        char t = buf[i];
                        buf[i] = buf[len - 1 - i];
                        buf[len - 1 - i] = t;
                    }
                }
                
                for (int i = 0; i < len && written < size - 1; i++) {
                    if (str) str[written] = buf[i];
                    written++;
                }
                fmt++;
            } else if (*fmt == 'u') {
                /* Unsigned */
                unsigned int val = va_arg(ap, unsigned int);
                char buf[32];
                int len = 0;
                
                if (val == 0) {
                    buf[len++] = '0';
                } else {
                    while (val > 0) { buf[len++] = '0' + (val % 10); val /= 10; }
                    for (int i = 0; i < len / 2; i++) {
                        char t = buf[i];
                        buf[i] = buf[len - 1 - i];
                        buf[len - 1 - i] = t;
                    }
                }
                
                for (int i = 0; i < len && written < size - 1; i++) {
                    if (str) str[written] = buf[i];
                    written++;
                }
                fmt++;
            } else if (*fmt == 'x' || *fmt == 'X') {
                /* Hex */
                unsigned int val = va_arg(ap, unsigned int);
                char buf[32];
                int len = 0;
                bool upper = (*fmt == 'X');
                
                if (val == 0) {
                    buf[len++] = '0';
                } else {
                    while (val > 0) {
                        int digit = val % 16;
                        buf[len++] = (digit < 10) ? ('0' + digit) : 
                                     (upper ? 'A' : 'a') + (digit - 10);
                        val /= 16;
                    }
                    for (int i = 0; i < len / 2; i++) {
                        char t = buf[i];
                        buf[i] = buf[len - 1 - i];
                        buf[len - 1 - i] = t;
                    }
                }
                
                for (int i = 0; i < len && written < size - 1; i++) {
                    if (str) str[written] = buf[i];
                    written++;
                }
                fmt++;
            } else if (*fmt == 'f' || *fmt == 'g') {
                /* Float (simplified) */
                double val = va_arg(ap, double);
                /* Just print as int part for now */
                long int_part = (long)val;
                if (int_part < 0) {
                    if (str && written < size) str[written] = '-';
                    written++;
                    int_part = -int_part;
                }
                
                char buf[32];
                int len = 0;
                if (int_part == 0) {
                    buf[len++] = '0';
                } else {
                    long tmp = int_part;
                    while (tmp > 0) { buf[len++] = '0' + (tmp % 10); tmp /= 10; }
                    for (int i = 0; i < len / 2; i++) {
                        char t = buf[i];
                        buf[i] = buf[len - 1 - i];
                        buf[len - 1 - i] = t;
                    }
                }
                
                for (int i = 0; i < len && written < size - 1; i++) {
                    if (str) str[written] = buf[i];
                    written++;
                }
                if (str && written < size) str[written] = '.';
                written++;
                if (str && written < size) str[written] = '0';
                written++;
                fmt++;
            } else if (*fmt == 's') {
                /* String */
                const char *str_arg = va_arg(ap, const char *);
                while (str_arg && *str_arg && written < size - 1) {
                    if (str) str[written] = *str_arg;
                    written++;
                    str_arg++;
                }
                fmt++;
            } else if (*fmt == 'c') {
                /* Char */
                char c = (char)va_arg(ap, int);
                if (str) str[written] = c;
                written++;
                fmt++;
            } else if (*fmt == 'p') {
                /* Pointer */
                unsigned long ptr = va_arg(ap, unsigned long);
                const char *hex_prefix = "0x";
                while (*hex_prefix && written < size - 1) {
                    if (str) str[written] = *hex_prefix;
                    written++;
                    hex_prefix++;
                }
                
                /* Convert to hex */
                char buf[32];
                int len = 0;
                if (ptr == 0) {
                    buf[len++] = '0';
                } else {
                    while (ptr > 0) {
                        int digit = ptr % 16;
                        buf[len++] = (digit < 10) ? ('0' + digit) : ('a' + (digit - 10));
                        ptr /= 16;
                    }
                    for (int i = 0; i < len / 2; i++) {
                        char t = buf[i];
                        buf[i] = buf[len - 1 - i];
                        buf[len - 1 - i] = t;
                    }
                }
                
                for (int i = 0; i < len && written < size - 1; i++) {
                    if (str) str[written] = buf[i];
                    written++;
                }
                fmt++;
            } else {
                /* Unknown specifier - just copy */
                if (str) str[written] = *fmt;
                written++;
                fmt++;
            }
        } else if (*fmt == '\\' && fmt[1] == 'n') {
            if (str) str[written] = '\n';
            written++;
            fmt += 2;
        } else {
            if (str) str[written] = *fmt;
            written++;
            fmt++;
        }
    }
    
    if (str && written < size) str[written] = 0;
    return (int)written;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    if (size == 0 && str) return -1;
    return vsnprintf_internal(str, size, fmt, ap);
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *str, const char *fmt, va_list ap) {
    return vsnprintf(str, 0x7FFFFFFF, fmt, ap);
}

int sprintf(char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(str, fmt, ap);
    va_end(ap);
    return ret;
}

/* Console-based printf */

int vprintf(const char *fmt, va_list ap) {
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len > 0) {
        vibe_app_console_write(buf);
    }
    return len;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

/* FILE operations - minimal stubs */

FILE *fopen(const char *path, const char *mode) {
    (void)path; (void)mode;
    return NULL;  /* Stub */
}

FILE *fdopen(int fd, const char *mode) {
    (void)fd; (void)mode;
    return NULL;  /* Stub */
}

int fclose(FILE *f) {
    (void)f;
    return 0;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int fprintf(FILE *f, const char *fmt, ...) {
    /* For now, just print to console if it's stdout/stderr */
    if (!f) return -1;
    
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    if (!stream) return -1;
    return vprintf(fmt, ap);
}
