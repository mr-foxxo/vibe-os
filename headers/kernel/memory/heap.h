#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stdint.h>
#include <stddef.h>

/* Early physical memory allocator (bump) - internal names */
void kernel_mm_init(uintptr_t heap_start, size_t heap_size);
void *kernel_malloc(size_t size);
void kernel_free(void *ptr);
uintptr_t kernel_heap_start(void);
uintptr_t kernel_heap_end(void);
size_t kernel_heap_used(void);
size_t kernel_heap_free(void);

/* public-facing API (aliases) */
static inline void memory_init(uintptr_t start, size_t size) {
    kernel_mm_init(start, size);
}

static inline void *kmalloc(size_t sz) {
    return kernel_malloc(sz);
}

static inline void kfree(void *ptr) {
    kernel_free(ptr);
}

#endif /* KERNEL_MEMORY_HEAP_H */
