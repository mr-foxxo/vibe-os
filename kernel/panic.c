#include <kernel/kernel.h>
#include <kernel/cpu/cpu.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/debug/debug.h>

/* Lock-free serial path (kernel/lock.c stuck detector). Panic must reach
 * the wire even when the deadlocked lock IS the serial lock: the nested
 * owner guard covers same-CPU reentry, but a foreign CPU holding the
 * serial lock would swallow a printf-based panic forever. */
extern void kernel_serial_puts_raw(const char *s);

void kernel_panic(const char *msg) {
    /* Raw first (survives any lock state), then the formatted paths. */
    kernel_serial_puts_raw("kernel_panic: ");
    kernel_serial_puts_raw(msg != 0 ? msg : "(null)");
    kernel_serial_puts_raw("\n");
    /* Serial next: the VGA text path can itself be dead (video state
     * corrupt); a silent panic in a QEMU headless run is undebuggable. */
    kernel_debug_printf("kernel_panic: %s\n", msg != 0 ? msg : "(null)");
    /* Face-1: which esp0 installations armed this death? (class #5, P4) */
    kernel_tss_install_ring_dump("panic");
    smp_tick_counts_dump();
    scheduler_frame_counts_dump();
    scheduler_save_ledger_dump();
    kernel_text_clear();
    kernel_text_puts("KERNEL PANIC:\n");
    if (msg) {
        kernel_text_puts(msg);
        kernel_text_putc('\n');
    }
    for (;;)
        __asm__ volatile("hlt");
}
