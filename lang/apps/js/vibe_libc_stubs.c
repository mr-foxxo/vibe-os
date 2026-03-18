/* Minimal libc stubs for QuickJS on Vibe OS
 * Provides bare minimum set of stdlib functions
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* Memory */
void *malloc(size_t size) {
    extern void *vibe_app_malloc(size_t size);
    return vibe_app_malloc(size);
}

void free(void *ptr) {
    extern void vibe_app_free(void *ptr);
    vibe_app_free(ptr);
}

void *realloc(void *ptr, size_t size) {
    extern void *vibe_app_realloc(void *ptr, size_t size);
    return vibe_app_realloc(ptr, size);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

/* String */
void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    return memcpy(dest, src, n);
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (!s1[i]) break;
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++)) {}
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    for (size_t i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    return dest;
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
    const char *p = NULL;
    while (*s) {
        if (*s == (char)c) p = s;
        s++;
    }
    return (char *)p;
}

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) {
        strcpy(p, s);
    }
    return p;
}

/* Math */
double fabs(double x) {
    return x < 0 ? -x : x;
}

double floor(double x) {
    return (double)(long)x;
}

double ceil(double x) {
    long i = (long)x;
    return (x == (double)i) ? (double)i : (double)(i + 1);
}

double sqrt(double x) {
    if (x < 0) return 0;
    if (x == 0) return 0;
    double guess = x / 2;
    for (int i = 0; i < 20; i++) {
        guess = (guess + x / guess) / 2;
    }
    return guess;
}

double pow(double x, double y) {
    if (y == 0) return 1;
    if (y < 0) return 1 / pow(x, -y);
    double result = 1;
    while (y >= 1) {
        if ((int)y % 2 == 1) result *= x;
        x *= x;
        y /= 2;
    }
    return result;
}

/* stdio (minimal) */
int printf(const char *format, ...) {
    (void)format;
    return 0;
}

int fprintf(void *stream, const char *format, ...) {
    (void)stream;
    (void)format;
    return 0;
}

int sprintf(char *str, const char *format, ...) {
    (void)str;
    (void)format;
    return 0;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    (void)str;
    (void)size;
    (void)format;
    return 0;
}

void *fopen(const char *path, const char *mode) {
    (void)path;
    (void)mode;
    return NULL;
}

void fclose(void *stream) {
    (void)stream;
}

size_t fread(void *ptr, size_t size, size_t nmemb, void *stream) {
    (void)ptr;
    (void)size;
    (void)nmemb;
    (void)stream;
    return 0;
}

int fseek(void *stream, long offset, int whence) {
    (void)stream;
    (void)offset;
    (void)whence;
    return -1;
}

/* Misc */
int abs(int x) {
    return x < 0 ? -x : x;
}

long labs(long x) {
    return x < 0 ? -x : x;
}

int rand(void) {
    static unsigned long seed = 1;
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) & 0x7fff;
}

void srand(unsigned int s) {
    (void)s;
}
