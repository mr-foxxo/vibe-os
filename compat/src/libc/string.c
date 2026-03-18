#include "../include/compat/libc/string.h"
#include <stdint.h>

/* String functions */
size_t strlen(const char *s) {
    size_t n = 0;
    if (s) while (s[n]) n++;
    return n;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = s1, *b = s2;
    while (n--) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return 0;
}

void *memmove(void *dest, const void *src, size_t n) {
    if (dest < src) return memcpy(dest, src, n);
    uint8_t *d = (uint8_t*)dest + n;
    const uint8_t *s = (const uint8_t*)src + n;
    while (n--) *--d = *--s;
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    if (!s1) s1 = "";
    if (!s2) s2 = "";
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1) s1 = "";
    if (!s2) s2 = "";
    while (n && *s1 && *s1 == *s2) { s1++; s2++; n--; }
    if (!n) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
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

char *strchr(const char *s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    if (c == 0) return (char*)s;
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    if (s) {
        do {
            if (*s == (char)c) {
                last = s;
            }
        } while (*s++);
    }
    return (char*)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (*needle == 0) return (char*)haystack;
    
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
    }
    return NULL;
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

/* strtok - not a safe function but acceptable for VibeOS apps */
static char *g_strtok_saved = NULL;

char *strtok(char *str, const char *delim) {
    if (!delim || !*delim) return str;
    
    if (str) {
        g_strtok_saved = str;
    }
    if (!g_strtok_saved) return NULL;
    
    char *s = g_strtok_saved;
    
    // Skip leading delimiters
    while (*s && strchr(delim, *s)) s++;
    
    if (!*s) {
        g_strtok_saved = NULL;
        return NULL;
    }
    
    char *token = s;
    
    // Find next delimiter
    while (*s && !strchr(delim, *s)) s++;
    
    if (*s) {
        *s = 0;
        g_strtok_saved = s + 1;
    } else {
        g_strtok_saved = NULL;
    }
    
    return token;
}
