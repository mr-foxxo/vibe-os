/* mruby Real Integration for Vibe OS
 * Minimal wrapper to compile real mruby with bare-metal libc
 */

#include "../../headers/lang/vibe_libc.h"

/* va_list for variadic functions */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + sizeof(last))
#define va_arg(ap, type) (*(type *)((ap) += sizeof(type), (ap) - sizeof(type)))
#define va_end(ap) ((ap) = 0)

/* mruby config */
#define ENABLE_DEBUG 0
#define MRB_DEBUG_HOOK_ENABLE 0
#define MRB_METHOD_CACHE 1

/* Memory */
static uint8_t g_mruby_heap[1024 * 1024];  /* 1MB for mruby */
static size_t g_mruby_ptr = 0;

void *malloc(size_t size) {
    if (g_mruby_ptr + size > sizeof(g_mruby_heap)) return NULL;
    void *p = &g_mruby_heap[g_mruby_ptr];
    g_mruby_ptr += (size + 15) & ~15;
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

/* Math stubs */
double floor(double x) { return (double)(long long)x; }
double ceil(double x) { long long n = (long long)x; return ((double)n < x) ? n + 1.0 : (double)n; }
double fabs(double x) { return x < 0 ? -x : x; }

/* mruby core compilation */
/* Note: This would include the main mruby source files */
/* For now, we'll mark this as a stub that will be expanded */

/* This is a placeholder - real mruby compilation requires:
 * - mruby/src/vm.c
 * - mruby/src/value.c
 * - mruby/src/string.c
 * - mruby/src/array.c
 * - And many other core modules
 * These should be gradually added with proper configuration
 */

void mrb_stub_init(void) {
    /* Stub for mruby initialization */
}
