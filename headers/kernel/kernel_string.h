#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <stddef.h>
#include <stdint.h>

/* minimal replacements for standard string/memory routines */

static inline void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

static inline void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < (uint8_t *)src) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

static inline void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

static inline int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p1 = (const uint8_t *)a;
    const uint8_t *p2 = (const uint8_t *)b;
    while (n--) {
        if (*p1 != *p2) {
            return (int)(*p1 - *p2);
        }
        p1++;
        p2++;
    }
    return 0;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)((unsigned char)*a - (unsigned char)*b);
}

static inline size_t strlen(const char *s) {
    size_t len = 0u;
    while (s != 0 && s[len] != '\0') {
        ++len;
    }
    return len;
}

static inline char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dst;
}

static inline char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && *src) {
        *d++ = *src++;
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dst;
}

#endif /* KERNEL_STRING_H */
