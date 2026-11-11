#include <stdint.h>
#include <kernel/kernel.h>
#include <kernel/drivers/debug/debug.h>

/*
 * Kernel stack protector (COMPLETION_ACTION_PLAN P4, classe #5, adendo 31
 * probe). The elimination rounds (STALE-AT-ENTRY 0-in-deaths, CTX-DRIFT 0/6,
 * FRAME-BAD-AT-IRQEXIT 0/5) left exactly one mechanism class: a RET/IRET
 * consuming a return slot whose CONTENT was corrupted by a local buffer
 * overflow on the same, legitimately-pointed stack. That is the textbook
 * target of -fstack-protector, which the kernel explicitly did NOT build
 * with (the -fno-stack-protector flag was hardcoded in CFLAGS).
 *
 * The Makefile now compiles kernel objects with
 * `-fstack-protector -mstack-protector-guard=global`: with -fno-pic
 * -fno-pie this emits a DIRECT load of __stack_chk_guard below (no %gs TLS,
 * no GOT), which is exactly what a flat ring0 kernel can service. A fixed
 * canary suffices for THIS purpose: a linear local overflow must walk
 * through the canary word on its way to the saved return slot, so the check
 * fires at function epilogue time — naming the offender by the return
 * address inside the protected function (resolvable via `nm`). It is not a
 * security boundary against an attacker who controls stack content; the
 * attacker here is the content itself.
 */
uintptr_t __stack_chk_guard = 0x56494245u; /* "VIBE" */

void __attribute__((noreturn)) __stack_chk_fail(void) {
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);

    kernel_debug_printf("stack-chk: FAIL guard-corrupt caller=%x\n",
                        (unsigned int)ra);
    kernel_panic("stack-chk: local buffer overflow (P4 classe #5 candidate)");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
