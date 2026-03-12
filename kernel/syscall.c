#include <kernel/kernel.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/process.h>
#include <stdint.h>
#include <include/userland_api.h> /* syscall IDs */

/* syscall table and helpers for the new kernel-side mechanism.  The
   legacy stage2 dispatch is still compiled into the image; eventually we
   will migrate completely to this table-driven approach. */

#define MAX_SYSCALLS 64
typedef uint32_t (*syscall_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static syscall_fn syscall_table[MAX_SYSCALLS];

static uint32_t sys_getpid(uint32_t a, uint32_t b, uint32_t c,
                           uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    process_t *cur = scheduler_current();
    return cur ? (uint32_t)cur->pid : 0u;
}

static uint32_t sys_yield(uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    yield();
    return 0;
}

static uint32_t sys_write_debug(uint32_t a, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    const char *msg = (const char *)(uintptr_t)a;
    if (msg)
        kernel_debug_puts(msg);
    return 0;
}

void syscall_init(void) {
    /* register new kernel syscalls; numbers are defined in
       include/userland_api.h */
    syscall_table[SYSCALL_GETPID] = sys_getpid;
    syscall_table[SYSCALL_YIELD] = sys_yield;
    syscall_table[SYSCALL_WRITE_DEBUG] = sys_write_debug;
}

/* dispatch routine called by new stub (not yet wired up).  Return value is
   placed in eax by the caller. */
uint32_t syscall_dispatch_internal(uint32_t num, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d, uint32_t e) {
    if (num < MAX_SYSCALLS && syscall_table[num]) {
        return syscall_table[num](a, b, c, d, e);
    }
    return (uint32_t)-1;
}
