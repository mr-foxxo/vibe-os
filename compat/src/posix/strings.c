#include <stddef.h>
#include <string.h>
#include <strings.h>

void bzero(void *s, size_t n) {
    memset(s, 0, n);
}

int bcmp(const void *s1, const void *s2, size_t n) {
    return memcmp(s1, s2, n);
}

void bcopy(const void *src, void *dst, size_t n) {
    memmove(dst, src, n);
}

char *index(const char *s, int c) {
    return strchr(s, c);
}

char *rindex(const char *s, int c) {
    return strrchr(s, c);
}

int strcasecmp(const char *s1, const char *s2) {
    if (s1 == 0) s1 = "";
    if (s2 == 0) s2 = "";
    while (*s1 != '\0' && *s2 != '\0') {
        int c1 = (unsigned char)*s1;
        int c2 = (unsigned char)*s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        ++s1;
        ++s2;
    }
    {
        int c1 = (unsigned char)*s1;
        int c2 = (unsigned char)*s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        return c1 - c2;
    }
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    if (s1 == 0) s1 = "";
    if (s2 == 0) s2 = "";
    while (n > 0 && *s1 != '\0' && *s2 != '\0') {
        int c1 = (unsigned char)*s1;
        int c2 = (unsigned char)*s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) return 0;
    {
        int c1 = (unsigned char)*s1;
        int c2 = (unsigned char)*s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        return c1 - c2;
    }
}

int ffs(int i) {
    int bit = 1;

    if (i == 0) return 0;
    if ((i & 0xffff) == 0) { bit += 16; i >>= 16; }
    if ((i & 0xff) == 0) { bit += 8; i >>= 8; }
    if ((i & 0xf) == 0) { bit += 4; i >>= 4; }
    if ((i & 0x3) == 0) { bit += 2; i >>= 2; }
    if ((i & 0x1) == 0) { bit += 1; }
    return bit;
}
