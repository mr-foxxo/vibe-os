#include "../include/compat/libc/string.h"
#include <stdint.h>
#include <stdlib.h>

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

char *strdup(const char *s) {
    size_t len;
    char *copy;
    if (!s) return 0;
    len = strlen(s);
    copy = malloc(len + 1);
    if (!copy) return 0;
    memcpy(copy, s, len + 1);
    return copy;
}

char *strndup(const char *s, size_t n) {
    size_t len;
    char *copy;
    if (!s) return 0;
    len = strlen(s);
    if (len > n) len = n;
    copy = malloc(len + 1);
    if (!copy) return 0;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *s;
    if (!delim || !*delim) return str;
    if (str) {
        *saveptr = str;
    }
    if (!*saveptr) return 0;
    s = *saveptr;
    while (*s && strchr(delim, *s)) s++;
    if (!*s) {
        *saveptr = 0;
        return 0;
    }
    str = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s) {
        *s = '\0';
        *saveptr = s + 1;
    } else {
        *saveptr = 0;
    }
    return str;
}

char *strcasestr(const char *haystack, const char *needle) {
    size_t i;
    if (!haystack || !needle) return 0;
    if (*needle == 0) return (char *)haystack;
    for (; *haystack; haystack++) {
        for (i = 0; needle[i]; i++) {
            char h = haystack[i];
            char n = needle[i];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) break;
        }
        if (needle[i] == '\0') return (char *)haystack;
    }
    return 0;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    if (s) while (n < maxlen && s[n]) n++;
    return n;
}

static const char *g_sig_str(int sig) {
    switch (sig) {
    case 1: return "Hangup";
    case 2: return "Interrupt";
    case 3: return "Quit";
    case 4: return "Illegal instruction";
    case 5: return "Trace/BPT trap";
    case 6: return "Abort trap";
    case 7: return "EMT trap";
    case 8: return "Floating point exception";
    case 9: return "Killed";
    case 10: return "Bus error";
    case 11: return "Segmentation fault";
    case 12: return "Bad system call";
    case 13: return "Broken pipe";
    case 14: return "Alarm clock";
    case 15: return "Terminated";
    default: return "Unknown signal";
    }
}

char *strsignal(int sig) {
    return (char *)g_sig_str(sig);
}

void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--) {
        p--;
        if (*p == (unsigned char)c) return (void *)p;
    }
    return 0;
}

size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dst_len = 0;
    size_t src_len;
    if (!dst || !src) return 0;
    while (dst_len < size && dst[dst_len]) dst_len++;
    src_len = strlen(src);
    if (dst_len == size) return size + src_len;
    if (src_len < size - dst_len) {
        memcpy(dst + dst_len, src, src_len + 1);
    } else {
        memcpy(dst + dst_len, src, size - dst_len - 1);
        dst[size - 1] = '\0';
    }
    return dst_len + src_len;
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t src_len;
    if (!dst || !src) return 0;
    src_len = strlen(src);
    if (size == 0) return src_len;
    if (src_len >= size) {
        memcpy(dst, src, size - 1);
        dst[size - 1] = '\0';
    } else {
        memcpy(dst, src, src_len + 1);
    }
    return src_len;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return NULL;
}
