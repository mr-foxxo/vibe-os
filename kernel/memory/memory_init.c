#include <kernel/memory/memory_init.h>
#include <kernel/memory/physmem.h>
#include <kernel/memory/paging.h>

/* Top-level memory initialization stub. */

void memory_subsystem_init(void) {
    // call the lower-level subsystems
    physmem_init();
    paging_init();
    // heap setup through kernel_mm_init is handled by boot code
}
