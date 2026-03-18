/* QuickJS Real Integration for Vibe OS
 * Minimal wrapper to compile real QuickJS-ng with bare-metal libc
 */

#include "../../headers/lang/vibe_libc.h"

/* va_list for variadic functions */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + sizeof(last))
#define va_arg(ap, type) (*(type *)((ap) += sizeof(type), (ap) - sizeof(type)))
#define va_end(ap) ((ap) = 0)

/* Feature flags to minimize QuickJS footprint */
#define CONFIG_BIGNUM 0
#define CONFIG_AGENT 0
#define CONFIG_ATOMICS 0
#define CONFIG_VERSION "13_vibe"
#define CONFIG_DEBUGGER 0

/* Disable regex for bare-metal */
#define WITHOUT_REGEX 1

/* Memory */
static uint8_t g_quickjs_heap[1024 * 1024];  /* 1MB for QuickJS */
static size_t g_quickjs_ptr = 0;

void *malloc(size_t size) {
    if (g_quickjs_ptr + size > sizeof(g_quickjs_heap)) return NULL;
    void *p = &g_quickjs_heap[g_quickjs_ptr];
    g_quickjs_ptr += (size + 15) & ~15;
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
    if (newp && ptr) {
        size_t copy_size = size < 8192 ? size : 8192;
        memcpy(newp, ptr, copy_size);
    }
    return newp;
}

void free(void *ptr) { (void)ptr; }

/* Math functions */
double floor(double x) {
    return (double)(long long)x;
}

double ceil(double x) {
    long long n = (long long)x;
    if ((double)n < x) n++;
    return (double)n;
}

double fabs(double x) {
    return x < 0 ? -x : x;
}

double pow(double x, double y) {
    if (y == 0) return 1.0;
    if (y == 1) return x;
    double result = 1.0;
    int neg = y < 0;
    y = fabs(y);
    while (y > 0) {
        if ((long long)y & 1) result *= x;
        x *= x;
        y = floor(y / 2.0);
    }
    return neg ? 1.0 / result : result;
}

double sqrt(double x) {
    if (x < 0) return 0;
    if (x == 0) return 0;
    double s = x;
    double last = 0;
    while (s != last) {
        last = s;
        s = (s + x / s) / 2.0;
    }
    return s;
}

/* Stubs for things QuickJS might call */
int isnan(double x) { return x != x; }
int isinf(double x) { return x != 0 && x + x == x; }
int isfinite(double x) { return !isnan(x) && !isinf(x); }

/* QuickJS core source */
#include "quickjs.c"
