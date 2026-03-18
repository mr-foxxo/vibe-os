/* MicroPython Real Integration for Vibe OS
 * Minimal wrapper to compile real MicroPython with bare-metal libc
 */

#include "../../headers/lang/vibe_libc.h"

/* va_list for variadic functions */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + sizeof(last))
#define va_arg(ap, type) (*(type *)((ap) += sizeof(type), (ap) - sizeof(type)))
#define va_end(ap) ((ap) = 0)

/* MicroPython config */
#define MICROPY_ENABLE_EXTERNAL_IMPORT 0
#define MICROPY_ENABLE_COMPILER 1
#define MICROPY_EMIT_X86 0
#define MICROPY_STACK_CHECK 0

/* Memory */
static uint8_t g_micropython_heap[2048 * 1024];  /* 2MB for MicroPython */
static size_t g_micropython_ptr = 0;

void *malloc(size_t size) {
    if (g_micropython_ptr + size > sizeof(g_micropython_heap)) return NULL;
    void *p = &g_micropython_heap[g_micropython_ptr];
    g_micropython_ptr += (size + 15) & ~15;
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
        size_t copy_size = size < 16384 ? size : 16384;
        memcpy(newp, ptr, copy_size);
    }
    return newp;
}

void free(void *ptr) { (void)ptr; }

/* Math stubs */
double floor(double x) { return (double)(long long)x; }
double ceil(double x) { long long n = (long long)x; return ((double)n < x) ? n + 1.0 : (double)n; }
double fabs(double x) { return x < 0 ? -x : x; }
double sqrt(double x) {
    if (x <= 0) return 0;
    double s = x, last = 0;
    while (s != last) {
        last = s;
        s = (s + x / s) / 2.0;
    }
    return s;
}

/* MicroPython stub init */
void mp_init_stub(void) {
    /* Placeholder for MicroPython initialization */
}
