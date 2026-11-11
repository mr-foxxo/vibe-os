#include <kernel/memory/memory_init.h>
#include <kernel/memory/physmem.h>
#include <kernel/memory/paging.h>
#include <kernel/bootinfo.h>
#include <kernel/drivers/debug/debug.h>

void memory_subsystem_init(void) {
    const struct bootinfo *info = (const struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    int have_bootinfo = (info->magic == BOOTINFO_MAGIC &&
                         info->version == BOOTINFO_VERSION);

    if (have_bootinfo && (info->flags & BOOTINFO_FLAG_MEMINFO_VALID)) {
        kernel_debug_printf("memory: bootinfo mem base=%x size=%x end=%x\n",
                            (uint32_t)info->meminfo.largest_base,
                            (uint32_t)info->meminfo.largest_size,
                            (uint32_t)info->meminfo.largest_end);
    } else {
        kernel_debug_printf("memory: no bootinfo meminfo, using fallback\n");
    }

    physmem_init();
    paging_init();

    kernel_debug_printf("memory: subsystem ready usable=%uMB pages=%u free=%u\n",
                        (unsigned int)(physmem_usable_size() / (1024u * 1024u)),
                        (unsigned int)physmem_total_pages(),
                        (unsigned int)physmem_free_pages());
}
