#include <kernel/memory/heap.h>

/* Simple bump allocator for early kernel memory */
static uintptr_t g_heap_start = 0u;
static uintptr_t g_heap_ptr = 0u;
static uintptr_t g_heap_end = 0u;
static size_t g_heap_used = 0u;

void kernel_mm_init(uintptr_t heap_start, size_t heap_size) {
    g_heap_start = heap_start;
    g_heap_ptr = heap_start;
    g_heap_end = heap_start + heap_size;
    g_heap_used = 0u;
}

void *kernel_malloc(size_t size) {
    if (size == 0u) {
        return NULL;
    }
    
    /* Align to 8 bytes */
    if (size & 0x7u) {
        size += 8u - (size & 0x7u);
    }
    
    if (g_heap_ptr + size > g_heap_end) {
        return NULL;  /* Out of memory */
    }
    
    void *ptr = (void *)g_heap_ptr;
    g_heap_ptr += size;
    g_heap_used += size;
    
    return ptr;
}

void kernel_free(void *ptr) {
    (void)ptr;  /* No-op for bump allocator */
}

uintptr_t kernel_heap_start(void) {
    return g_heap_start;
}

uintptr_t kernel_heap_end(void) {
    return g_heap_end;
}

size_t kernel_heap_used(void) {
    return g_heap_used;
}

size_t kernel_heap_free(void) {
    return (g_heap_end > g_heap_ptr) ? (g_heap_end - g_heap_ptr) : 0u;
}
