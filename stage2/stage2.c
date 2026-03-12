#include <kernel/kernel.h>  /* found via -Ikernel/include */

/* stub entrypoint remains for compatibility; it simply forwards
   to the new kernel code while we incrementally migrate functionality */

__attribute__((noreturn, section(".entry"))) void stage2_entry(void) {
    kernel_entry();
}
