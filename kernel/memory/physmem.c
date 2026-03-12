#include <kernel/memory/physmem.h>

/* Simple dummy physical allocator - does nothing meaningful
   but exists to satisfy references during build. */

void physmem_init(void) {
    // TODO: initialize bitmap or free list of physical frames
}

void *alloc_phys_page(void) {
    return NULL; // placeholder
}

void free_phys_page(void *page) {
    (void)page;
    // placeholder
}
