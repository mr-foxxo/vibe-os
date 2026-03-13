#ifndef KERNEL_MEMORY_PHYSMEM_H
#define KERNEL_MEMORY_PHYSMEM_H

#include <stddef.h>
#include <stdint.h>

#define PHYSMEM_DYNAMIC_CAP_BYTES (3096u * 1024u * 1024u)

void physmem_init(void);
uintptr_t physmem_usable_base(void);
uintptr_t physmem_usable_end(void);
size_t physmem_usable_size(void);
void *alloc_phys_page(void);
void free_phys_page(void *page);

#endif /* KERNEL_MEMORY_PHYSMEM_H */
