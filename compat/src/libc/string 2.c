/*
 * String Functions Implementation
 * compat/src/libc/string.c
 */

#include <compat/libc/string.h>
#include <compat/libc/stdlib.h>
#include <stdint.h>
#include <stddef.h>

/* Memory operations */

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    if (dest < src) {
        return memcpy(dest, src, n);
    }
    uint8_t *d = (uint8_t *)dest + n;
    const uint8_t *s = (const uint8_t *)src + n;
    while (n--) *--d = *--s;
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    while (n--) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = (const uint8_t *)s;
    while (n--) {
        if (*p == (uint8_t)c) return (void *)p;
        p++;
    }
    return NULL;
}

/* String operations */

size_t strlen(const char *s) {
    size_t n = 0;
    if (s) while (s[n]) n++;
    return n;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = 0;
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    while (n && (*d++ = *src++)) n--;
    *d = 0;
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    if (!s1) s1 = "";
    if (!s2) s2 = "";
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1) s1 = "";
    if (!s2) s2 = "";
    while (n && *s1 && *s1 == *s2) { s1++; s2++; n--; }
    if (!n) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strcasecmp(const char *s1, const char *s2) {
    unsigned char c1, c2;
    while (1) {
        c1 = *(unsigned char *)s1++;
        c2 = *(unsigned char *)s2++;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        if (!c1) break;
    }
    return 0;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    unsigned char c1, c2;
    while (n--) {
        c1 = *(unsigned char *)s1++;
        c2 = *(unsigned char *)s2++;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        if (!c1) break;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if ((char)c == 0) return (char *)s;
    return NULL;
}

char *strrchr(const char *s, int c) {
    char *r = NULL;
    while (*s) {
        if (*s == (char)c) r = (char *)s;
        s++;
    }
    if ((char)c == 0) return (char *)s;
    return r;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
        haystack++;
    }
    return NULL;
}

char *strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n) {
            unsigned char hc = *(unsigned char *)h;
            unsigned char nc = *(unsigned char *)n;
            if (hc >= 'A' && hc <= 'Z') hc += 32;
            if (nc >= 'A' && nc <= 'Z') nc += 32;
            if (hc != nc) break;
            h++; n++;
        }
        if (!*n) return (char *)haystack;
        haystack++;
    }
    return NULL;
}

char *strtok(char *str, const char *delim) {
    static char *context = NULL;
    return strtok_r(str, delim, &context);
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *start;
    if (!str) str = *saveptr;
    if (!str || !*str) { *saveptr = ""; return NULL; }
    
    /* Skip leading delimiters */
    while (*str && strchr(delim, *str)) str++;
    if (!*str) { *saveptr = ""; return NULL; }
    
    start = str;
    while (*str && !strchr(delim, *str)) str++;
    
    if (*str) {
        *str = 0;
        *saveptr = str + 1;
    } else {
        *saveptr = "";
    }
    return start;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (s[n] && strchr(accept, s[n])) n++;
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (s[n] && !strchr(reject, s[n])) n++;
    return n;
}

char *strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

char *strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len > n) len = n;
    char *p = malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = 0;
    }
    return p;
}
