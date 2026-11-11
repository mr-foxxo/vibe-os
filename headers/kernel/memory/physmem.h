#ifndef KERNEL_MEMORY_PHYSMEM_H
#define KERNEL_MEMORY_PHYSMEM_H

#include <stddef.h>
#include <stdint.h>

#define PHYSMEM_DYNAMIC_CAP_BYTES (0x100000000ull)
#define PHYSMEM_MAX_PHYS_ADDR 0xFFFFFFFFu
#define PHYSMEM_PAGE_SIZE 4096u
#define PHYSMEM_MAX_TRACKED_PAGES 1048576u

void physmem_init(void);
uintptr_t physmem_usable_base(void);
uintptr_t physmem_usable_end(void);
size_t physmem_usable_size(void);
size_t physmem_total_pages(void);
size_t physmem_free_pages(void);
void physmem_reserve_range(uintptr_t start, size_t size);
void physmem_release_range(uintptr_t start, size_t size);
void *alloc_phys_page(void);
void free_phys_page(void *page);
void *alloc_phys_pages(size_t count);
void free_phys_pages(void *page, size_t count);

#endif /* KERNEL_MEMORY_PHYSMEM_H */
