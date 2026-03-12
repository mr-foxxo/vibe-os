#ifndef KERNEL_MEMORY_PHYSMEM_H
#define KERNEL_MEMORY_PHYSMEM_H

/* Physical memory manager stubs */

#include <stddef.h>

void physmem_init(void);
void *alloc_phys_page(void);
void free_phys_page(void *page);

#endif /* KERNEL_MEMORY_PHYSMEM_H */
