/* Minimal libc implementations for QuickJS on bare metal */

#include "../headers/lang/vibe_libc.h"

/* va_list for variadic functions */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + sizeof(last))
#define va_arg(ap, type) (*(type *)((ap) += sizeof(type), (ap) - sizeof(type)))
#define va_end(ap) ((ap) = 0)

/* Memory management */
static uint8_t g_vibe_heap[512 * 1024];
static size_t g_vibe_heap_ptr = 0;

void *malloc(size_t size) {
    if (g_vibe_heap_ptr + size > sizeof(g_vibe_heap)) return NULL;
    void *p = &g_vibe_heap[g_vibe_heap_ptr];
    g_vibe_heap_ptr += (size + 15) & ~15;
    return p;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    void *newp = malloc(size);
    if (newp && ptr) memcpy(newp, ptr, size < 8192 ? size : 8192);
    return newp;
}

void free(void *ptr) { (void)ptr; }

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

char *strcat(char *dest, const char *src) {
    strcpy(dest + strlen(dest), src);
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

char *strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

/* Stub I/O functions */
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
int sprintf(char *str, const char *fmt, ...) { (void)str; (void)fmt; return 0; }
int snprintf(char *str, size_t size, const char *fmt, ...) { (void)str; (void)size; (void)fmt; return 0; }
int vprintf(const char *fmt, va_list ap) { (void)fmt; (void)ap; return 0; }
int vfprintf(FILE *f, const char *fmt, va_list ap) { (void)f; (void)fmt; (void)ap; return 0; }
int vsprintf(char *str, const char *fmt, va_list ap) { (void)str; (void)fmt; (void)ap; return 0; }
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) { (void)str; (void)size; (void)fmt; (void)ap; return 0; }

FILE *fopen(const char *filename, const char *mode) { (void)filename; (void)mode; return NULL; }
FILE *fdopen(int fd, const char *mode) { (void)fd; (void)mode; return NULL; }
int fclose(FILE *stream) { (void)stream; return 0; }
int fflush(FILE *stream) { (void)stream; return 0; }
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) { (void)ptr; (void)size; (void)nmemb; (void)stream; return 0; }
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) { (void)ptr; (void)size; (void)nmemb; (void)stream; return 0; }
int fgetc(FILE *stream) { (void)stream; return EOF; }
int fputc(int c, FILE *stream) { (void)c; (void)stream; return c; }
int fputs(const char *s, FILE *stream) { (void)s; (void)stream; return 0; }
int puts(const char *s) { (void)s; return 0; }
void clearerr(FILE *stream) { (void)stream; }
int feof(FILE *stream) { (void)stream; return 0; }
int ferror(FILE *stream) { (void)stream; return 0; }
int fseek(FILE *stream, long offset, int whence) { (void)stream; (void)offset; (void)whence; return 0; }
long ftell(FILE *stream) { (void)stream; return 0; }
void rewind(FILE *stream) { (void)stream; }

int atoi(const char *nptr) {
    int n = 0;
    if (!nptr) return 0;
    while (*nptr >= '0' && *nptr <= '9') n = n * 10 + (*nptr++ - '0');
    return n;
}

long atol(const char *nptr) {
    long n = 0;
    if (!nptr) return 0;
    while (*nptr >= '0' && *nptr <= '9') n = n * 10 + (*nptr++ - '0');
    return n;
}

long long atoll(const char *nptr) {
    long long n = 0;
    if (!nptr) return 0;
    while (*nptr >= '0' && *nptr <= '9') n = n * 10 + (*nptr++ - '0');
    return n;
}

/* strtol - from glibc */
long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long acc = 0;
    int c;
    int neg = 0, any = 0;
    
    if (!nptr) return 0;
    
    do {
        c = *s++;
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    
    if (c == '-') {
        neg = 1;
        c = *s++;
    } else if (c == '+') {
        c = *s++;
    }
    
    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }
    
    if (base == 0) base = c == '0' ? 8 : 10;
    
    for (; c != '\0'; c = *s++) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else break;
        
        if (d >= base) break;
        any = 1;
        acc = acc * base + d;
    }
    
    if (endptr) *endptr = (char *)(any ? s - 1 : nptr);
    return neg ? -(long)acc : (long)acc;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long long acc = 0;
    int c;
    int neg = 0, any = 0;
    
    if (!nptr) return 0;
    
    do {
        c = *s++;
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    
    if (c == '-') {
        neg = 1;
        c = *s++;
    } else if (c == '+') {
        c = *s++;
    }
    
    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }
    
    if (base == 0) base = c == '0' ? 8 : 10;
    
    for (; c != '\0'; c = *s++) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else break;
        
        if (d >= base) break;
        any = 1;
        acc = acc * base + d;
    }
    
    if (endptr) *endptr = (char *)(any ? s - 1 : nptr);
    return neg ? -(long long)acc : (long long)acc;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long acc = 0;
    int c;
    int any = 0;
    
    if (!nptr) return 0;
    
    do {
        c = *s++;
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    
    if (c == '-' || c == '+') c = *s++;
    
    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }
    
    if (base == 0) base = c == '0' ? 8 : 10;
    
    for (; c != '\0'; c = *s++) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else break;
        
        if (d >= base) break;
        any = 1;
        acc = acc * base + d;
    }
    
    if (endptr) *endptr = (char *)(any ? s - 1 : nptr);
    return acc;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long long acc = 0;
    int c;
    int any = 0;
    
    if (!nptr) return 0;
    
    do {
        c = *s++;
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    
    if (c == '-' || c == '+') c = *s++;
    
    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }
    
    if (base == 0) base = c == '0' ? 8 : 10;
    
    for (; c != '\0'; c = *s++) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else break;
        
        if (d >= base) break;
        any = 1;
        acc = acc * base + d;
    }
    
    if (endptr) *endptr = (char *)(any ? s - 1 : nptr);
    return acc;
}

/* Basic strtod from glibc */
double strtod(const char *nptr, char **endptr) {
    const char *s = nptr;
    double result = 0.0;
    int neg = 0;
    
    if (!nptr) return 0.0;
    
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (*s - '0');
        s++;
    }
    
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            result += (*s - '0') * frac;
            frac *= 0.1;
            s++;
        }
    }
    
    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_neg = 0;
        if (*s == '-') { exp_neg = 1; s++; }
        else if (*s == '+') s++;
        
        int exp = 0;
        while (*s >= '0' && *s <= '9') {
            exp = exp * 10 + (*s - '0');
            s++;
        }
        
        double mult = 1.0;
        for (int i = 0; i < exp; i++) mult *= exp_neg ? 0.1 : 10.0;
        result *= mult;
    }
    
    if (endptr) *endptr = (char *)s;
    return neg ? -result : result;
}

/* Additional glibc string functions */
char *strtok_r(char *s, const char *delim, char **save_ptr) {
    char *end;
    
    if (s == NULL) s = *save_ptr;
    if (s == NULL) return NULL;
    
    /* Skip leading delimiters */
    s += strspn(s, delim);
    if (*s == '\0') {
        *save_ptr = NULL;
        return NULL;
    }
    
    /* Find end of token */
    end = s + strcspn(s, delim);
    if (*end != '\0') {
        *end = '\0';
        *save_ptr = end + 1;
    } else {
        *save_ptr = NULL;
    }
    
    return s;
}

size_t strspn(const char *s, const char *accept) {
    size_t ret = 0;
    while (*s && strchr(accept, *s++)) ret++;
    return ret;
}

size_t strcspn(const char *s, const char *reject) {
    size_t ret = 0;
    while (*s && !strchr(reject, *s++)) ret++;
    return ret;
}

double atof(const char *nptr) {
    return strtod(nptr, NULL);
}

void exit(int status) { (void)status; while(1); }
void abort(void) { while(1); }
int atexit(void (*func)(void)) { (void)func; return 0; }

int abs(int j) { return j < 0 ? -j : j; }
long labs(long j) { return j < 0 ? -j : j; }
long long llabs(long long j) { return j < 0 ? -j : j; }

unsigned int sleep(unsigned int seconds) { (void)seconds; return 0; }
int usleep(unsigned int microseconds) { (void)microseconds; return 0; }
