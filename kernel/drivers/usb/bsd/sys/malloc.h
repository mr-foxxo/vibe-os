#ifndef _SYS_MALLOC_H_
#define _SYS_MALLOC_H_

#include <stddef.h>
#include <kernel/memory/heap.h>

#define M_NOWAIT 0x01
#define M_WAITOK 0x02
#define M_ZERO 0x04

#define M_DEVBUF 0
#define M_USB 1
#define M_USBHC 2
#define M_TEMP 3
#define M_USBDEV 4

static inline void *malloc(size_t size, int type, int flags) {
    (void)type; (void)flags;
    return kernel_malloc(size);
}

static inline void free(void *addr, int type, size_t size) {
    (void)type; (void)size;
    kernel_free(addr);
}

static inline void *mallocarray(size_t nmemb, size_t size, int type, int flags) {
    (void)type; (void)flags;
    if (nmemb && size > (size_t)-1 / nmemb) return NULL;
    return kernel_malloc(nmemb * size);
}

static inline void *realloc(void *addr, size_t size, int type, int flags) {
    (void)type; (void)flags;
    return kernel_realloc(addr, size);
}

#endif
