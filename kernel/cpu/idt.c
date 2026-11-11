#include <stdint.h>
#include <kernel/kernel.h>
#include <kernel/interrupt.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/scheduler.h>
#include <kernel/cpu/cpu.h>
#include <kernel/smp.h>

#define IDT_ENTRIES 256

/* lock-free raw serial (panic path) — declared locally; see kernel/panic.c */
extern void kernel_serial_puts_raw(const char *s);
#define IRQ0_VECTOR 0x20
#define IRQ15_VECTOR 0x2F
#define SYSCALL_VECTOR 0x80
#define YIELD_VECTOR 0x81
#define SMP_WAKE_VECTOR 0x82
#define SMP_TIMER_VECTOR 0x83

/* These symbols are provided by the assembly stubs in kernel_asm/isr.asm. */
extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq2_stub(void);
extern void irq3_stub(void);
extern void irq4_stub(void);
extern void irq5_stub(void);
extern void irq6_stub(void);
extern void irq7_stub(void);
extern void irq8_stub(void);
extern void irq9_stub(void);
extern void irq10_stub(void);
extern void irq11_stub(void);
extern void irq12_stub(void);
extern void irq13_stub(void);
extern void irq14_stub(void);
extern void irq15_stub(void);
extern void syscall_stub(void);
extern void yield_stub(void);
extern void smp_wakeup_stub(void);
extern void smp_timer_stub(void);

extern void divide_error_stub(void);
extern void debug_stub(void);
extern void int3_stub(void);
extern void invalid_opcode_stub(void);
extern void invalid_tss_stub(void);
extern void segment_not_present_stub(void);
extern void stack_fault_stub(void);
extern void general_protection_stub(void);
extern void page_fault_stub(void);
extern void double_fault_stub(void);

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry g_idt[IDT_ENTRIES];
static struct idt_ptr g_idt_ptr;

static void idt_set_gate(uint8_t vector, uint32_t handler, uint8_t type_attr) {
    g_idt[vector].offset_low = (uint16_t)(handler & 0xFFFFu);
    g_idt[vector].selector = 0x08;
    g_idt[vector].zero = 0;
    g_idt[vector].type_attr = type_attr;
    g_idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFFu);
}

void kernel_idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; ++i) {
        g_idt[i].offset_low = 0;
        g_idt[i].selector = 0;
        g_idt[i].zero = 0;
        g_idt[i].type_attr = 0;
        g_idt[i].offset_high = 0;
    }

    /* exception handlers */
    idt_set_gate(0,  (uint32_t)divide_error_stub,        0x8E);
    idt_set_gate(1,  (uint32_t)debug_stub,               0x8E);
    /* Probe vector: a G0-only DR7 enable traps IFF the CPU supports hw
     * breakpoints; with the gate present the probe is safe everywhere. */
    idt_set_gate(3,  (uint32_t)int3_stub,                0x8E);
    idt_set_gate(6,  (uint32_t)invalid_opcode_stub,       0x8E);
    idt_set_gate(10, (uint32_t)invalid_tss_stub,          0x8E);
    idt_set_gate(11, (uint32_t)segment_not_present_stub,  0x8E);
    idt_set_gate(12, (uint32_t)stack_fault_stub,          0x8E);
    idt_set_gate(13, (uint32_t)general_protection_stub,   0x8E);
    idt_set_gate(14, (uint32_t)page_fault_stub,           0x8E);
    idt_set_gate(8,  (uint32_t)double_fault_stub,         0x8E);

    /* irq handlers */
    idt_set_gate(IRQ0_VECTOR + 0,  (uint32_t)irq0_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 1,  (uint32_t)irq1_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 2,  (uint32_t)irq2_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 3,  (uint32_t)irq3_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 4,  (uint32_t)irq4_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 5,  (uint32_t)irq5_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 6,  (uint32_t)irq6_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 7,  (uint32_t)irq7_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 8,  (uint32_t)irq8_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 9,  (uint32_t)irq9_stub,  0x8E);
    idt_set_gate(IRQ0_VECTOR + 10, (uint32_t)irq10_stub, 0x8E);
    idt_set_gate(IRQ0_VECTOR + 11, (uint32_t)irq11_stub, 0x8E);
    idt_set_gate(IRQ0_VECTOR + 12, (uint32_t)irq12_stub, 0x8E);
    idt_set_gate(IRQ0_VECTOR + 13, (uint32_t)irq13_stub, 0x8E);
    idt_set_gate(IRQ0_VECTOR + 14, (uint32_t)irq14_stub, 0x8E);
    idt_set_gate(IRQ15_VECTOR,     (uint32_t)irq15_stub, 0x8E);
    idt_set_gate(SYSCALL_VECTOR,   (uint32_t)syscall_stub, 0xEF);
    idt_set_gate(YIELD_VECTOR,     (uint32_t)yield_stub, 0x8E);
    idt_set_gate(SMP_WAKE_VECTOR,  (uint32_t)smp_wakeup_stub, 0x8E);
    idt_set_gate(SMP_TIMER_VECTOR, (uint32_t)smp_timer_stub, 0x8E);

    g_idt_ptr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    g_idt_ptr.base = (uint32_t)(uintptr_t)&g_idt[0];
    __asm__ volatile("lidt %0" : : "m"(g_idt_ptr));
}

static int exception_is_user_mode(uint32_t cs) {
    return (cs & 0x3u) == 0x3u;
}

static void exception_kill_user_task(const char *name,
                                     uint32_t error_code,
                                     uint32_t eip,
                                     uint32_t cs,
                                     uint32_t extra) {
    process_t *current = scheduler_current();

    kernel_debug_printf("exception: %s user pid=%d err=%x eip=%x cs=%x extra=%x\n",
                        name,
                        current != NULL ? current->pid : -1,
                        (unsigned int)error_code,
                        (unsigned int)eip,
                        (unsigned int)cs,
                        (unsigned int)extra);
    if (current == NULL) {
        kernel_panic(name);
    }

    scheduler_terminate_task(current);
    yield();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* simple handlers that just panic */
/* INT3 vector-3: no error code pushed (matches divide_error's zero);
 * used by the debug-support probe and as a tripwire for accidental int3
 * execution (logged, never fatal). */
void int3_handler(uint32_t eip, uint32_t cs) {
    static volatile uint32_t g_int3_seen;

    if (g_int3_seen < 4u) {
        g_int3_seen++;
        kernel_debug_printf("int3: eip=%x cs=%x\n", (unsigned int)eip,
                            (unsigned int)cs);
    }
}

void divide_error_handler(uint32_t eip, uint32_t cs) {
    if (exception_is_user_mode(cs)) {
        exception_kill_user_task("Divide Error", 0u, eip, cs, 0u);
    }
    kernel_panic("Divide Error");
}

/*
 * #DB — hardware write-watchpoints on the victim process_t record
 * (P4 classe #5, adendo 36). DR0/DR1 are armed by scheduler_dr_maintain;
 * every debug exception is classified (and possibly printed) by the
 * scheduler, which owns the legit-writer brackets. The store has already
 * completed when #DB fires (x86 write watchpoints trigger after the
 * instruction), so classification only names — it cannot suppress.
 * USER-mode writers must NOT iret back: the error-code stubs do not restore
 * data segments, so resuming into user code would leave kernel ds/es
 * installed — user-mode writers are reported and the writer task is killed;
 * kernel-mode writers are named and execution continues (budget/disarm in
 * scheduler.c keeps it finite).
 */
void debug_handler(uint32_t error_code, uint32_t eip, uint32_t cs) {
    uint32_t dr6;
    uint32_t dr6_clear;

    (void)error_code;
    __asm__ volatile("mov %%dr6, %0" : "=r"(dr6));
    dr6_clear = 0u;
    /* SELF-PROTECTION (measured: 9 legit bracketed events then a silent
     * freeze — a nested #DB over an already-deep handler stack dies in
     * double fault). Disarm this CPU's watchpoints at entry BEFORE doing
     * anything else that touches the stack: scheduler_dr_maintain re-arms
     * idempotently from the next tick, and the budget caps the round-trip. */
    __asm__ volatile("movl %0, %%dr6\n\t"
                     "movl %0, %%dr7"
                     :
                     : "r"(dr6_clear)
                     : "memory");
    if (scheduler_db_probe_capture(eip) == 0) {
        scheduler_db_event(dr6, eip, cs, kernel_cpu_index());
    }
    if (exception_is_user_mode(cs)) {
        exception_kill_user_task("Debug watchpoint (user writer)", 0u, eip, cs,
                                 dr6);
    }
}

/*
 * Raw kernel backtrace for the class #5 wall (COMPLETION_ACTION_PLAN P4).
 * The real-time smp2 death is `#UD kernel-mode eip=3` — a call/jump through
 * a near-NULL function pointer — and gdb cannot be attached (it suppresses
 * the race). Scan the current kernel stack for words inside .text (linker
 * symbols __kernel_text_start/__kernel_text_end) and print them with their
 * slot index; resolving the pcs afterwards via `nm build/kernel.elf` names
 * the caller chain of the wild jump. One-shot, bounded scan: a dead boot
 * must not flood or over-read the stack.
 */
extern char __kernel_text_start[];
extern char __kernel_text_end[];

static void invalid_opcode_backtrace(void) {
    static int done = 0;
    volatile const uint32_t *sp;
    uint32_t base;
    uint32_t top;
    uint32_t printed = 0u;
    uint32_t i;

    if (done != 0) {
        return;
    }
    done = 1;

    __asm__ volatile("mov %%esp, %0" : "=r"(base));
    base = (base + 3u) & ~3u;
    sp = (volatile const uint32_t *)(uintptr_t)base;
    top = 64u; /* 256 bytes of stack: enough for the caller chain, bounded */

    kernel_debug_printf("ud-bt: sp=%x text=[%x,%x)\n",
                        (unsigned int)base,
                        (unsigned int)(uintptr_t)__kernel_text_start,
                        (unsigned int)(uintptr_t)__kernel_text_end);
    for (i = 0u; i < top && printed < 10u; ++i) {
        uint32_t w = sp[i];
        if (w >= (uint32_t)(uintptr_t)__kernel_text_start &&
            w <  (uint32_t)(uintptr_t)__kernel_text_end) {
            kernel_debug_printf("ud-bt: [%u] 0x%x\n", (unsigned int)i, w);
            printed += 1u;
        }
    }
}

void invalid_opcode_handler(uint32_t eip, uint32_t cs) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[4][11]; /* eip, cs, bytes[0], bytes[1] */

    if (exception_is_user_mode(cs)) {
        exception_kill_user_task("Invalid Opcode", 0u, eip, cs, 0u);
    }

    invalid_opcode_backtrace();

    /* helper to format 32-bit hex */
    #define FMT32(val, dst) do { \
        uint32_t v = (val);       \
        (dst)[0]='0'; (dst)[1]='x'; \
        for (int i=0;i<8;i++) (dst)[2+i]=hex[(v>>(28-4*i))&0xF]; \
        (dst)[10]='\0'; \
    } while (0)

    uint8_t b0=*((volatile uint8_t*)eip);
    uint8_t b1=*((volatile uint8_t*)(eip+1));

    FMT32(eip, buf[0]);
    FMT32(cs, buf[1]);
    FMT32((uint32_t)b0, buf[2]);
    FMT32((uint32_t)b1, buf[3]);

    /* Serial too: a screen-only death is invisible in headless QEMU runs.
     * cpu= names WHICH core faulted (class #5 is real-time smp2 only; a
     * kernel-mode #UD on the AP vs the BSP points at different code). */
    kernel_debug_printf("exception: #UD kernel-mode cpu=%u eip=%x cs=%x b0=%x b1=%x (halting)\n",
                        (unsigned int)kernel_cpu_index(),
                        (unsigned int)eip, (unsigned int)cs,
                        (unsigned int)b0, (unsigned int)b1);
    /* Face-1: the last esp0 installs name which kernel stack this CPU was
     * armed to take traps on when it died (class #5, P4). */
    kernel_tss_install_ring_dump("ud");
    smp_tick_counts_dump();
    scheduler_frame_counts_dump();
    scheduler_save_ledger_dump();
    kernel_text_puts("\n#UD EIP="); kernel_text_puts(buf[0]);
    kernel_text_puts(" CS=");       kernel_text_puts(buf[1]);
    kernel_text_puts(" B0=");       kernel_text_puts(buf[2]);
    kernel_text_puts(" B1=");       kernel_text_puts(buf[3]);
    kernel_text_puts("\nHalting.");
    for (;;) __asm__ volatile("hlt");

    #undef FMT32
}

static void exception_log(const char *name,
                          uint32_t error_code,
                          uint32_t eip,
                          uint32_t cs,
                          uint32_t extra) {
    kernel_debug_printf("exception: %s err=%x eip=%x cs=%x extra=%x\n",
                        name,
                        (unsigned int)error_code,
                        (unsigned int)eip,
                        (unsigned int)cs,
                        (unsigned int)extra);
}

void invalid_tss_handler(uint32_t error_code, uint32_t eip, uint32_t cs) {
    if (exception_is_user_mode(cs)) {
        exception_kill_user_task("Invalid TSS", error_code, eip, cs, 0u);
    }
    exception_log("Invalid TSS", error_code, eip, cs, 0u);
    kernel_panic("Invalid TSS");
}

void segment_not_present_handler(uint32_t error_code, uint32_t eip, uint32_t cs) {
    if (exception_is_user_mode(cs)) {
        exception_kill_user_task("Segment Not Present", error_code, eip, cs, 0u);
    }
    exception_log("Segment Not Present", error_code, eip, cs, 0u);
    kernel_panic("Segment Not Present");
}

void stack_fault_handler(uint32_t error_code, uint32_t eip, uint32_t cs) {
    if (exception_is_user_mode(cs)) {
        exception_kill_user_task("Stack Fault", error_code, eip, cs, 0u);
    }
    exception_log("Stack Fault", error_code, eip, cs, 0u);
    kernel_panic("Stack Fault");
}

void general_protection_handler(uint32_t error_code, uint32_t eip, uint32_t cs) {
    if (exception_is_user_mode(cs)) {
        exception_kill_user_task("General Protection", error_code, eip, cs, 0u);
    }
    exception_log("General Protection", error_code, eip, cs, 0u);
    kernel_panic("General Protection");
}

/*
 * Face-1 spatial dissection (COMPLETION_ACTION_PLAN P4, adendo 8). When a
 * fault touches the read-only .text (illegal write cr2 inside .text, or the
 * faulting eip inside .text), dump the surrounding words of BOTH addresses.
 * The spatial pattern of the `f0 53`-style fragments — contiguous run vs
 * scattered — is what distinguishes a LINEAR overflow from a write through
 * an object with the wrong base. Pure reads of an already-mapped RO page:
 * cannot fault recursively, cannot block. Budgeted so it never floods.
 */
extern char __kernel_text_start[];
extern char __kernel_text_end[];

static void pf_dissect_line(const char *tag, uintptr_t addr, uintptr_t ts, uintptr_t te) {
    uintptr_t base;
    volatile const uint32_t *w;
    uint32_t i;

    if (addr < ts + 32u || addr + 32u >= te) {
        kernel_debug_printf("%s=%x (near text edge, no window)\n", tag, (unsigned int)addr);
        return;
    }
    base = (addr & ~(uintptr_t)3u) - 32u;
    w = (volatile const uint32_t *)base;
    kernel_debug_printf("%s=%x window[-32..+12]{", tag, (unsigned int)addr);
    for (i = 0u; i < 11u; ++i) {
        kernel_debug_printf("%x ", w[i]);
    }
    kernel_debug_printf("} text=[%x,%x)\n", (unsigned int)ts, (unsigned int)te);
}

void page_fault_handler(uint32_t error_code, uint32_t eip, uint32_t cs) {
    uint32_t cr2 = 0u;

    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    {
        uintptr_t ts = (uintptr_t)__kernel_text_start;
        uintptr_t te = (uintptr_t)__kernel_text_end;
        static volatile uint32_t g_pf_dissect_budget = 8u;

        if (g_pf_dissect_budget != 0u &&
            ((cr2 >= ts && cr2 < te) || (eip >= ts && eip < te))) {
            g_pf_dissect_budget--;
            kernel_debug_printf("pf-text: err=%x eip=%x cr2=%x\n",
                                (unsigned int)error_code, (unsigned int)eip, (unsigned int)cr2);
            pf_dissect_line("pf-text: CR2", (uintptr_t)cr2, ts, te);
            pf_dissect_line("pf-text: EIP", (uintptr_t)eip, ts, te);
        }
    }

    if (exception_is_user_mode(cs)) {
        exception_kill_user_task("Page Fault", error_code, eip, cs, cr2);
    }
    exception_log("Page Fault", error_code, eip, cs, cr2);
    kernel_panic("Page Fault");
}

void double_fault_handler(uint32_t error_code, uint32_t eip, uint32_t cs) {
    exception_log("Double Fault", error_code, eip, cs, 0u);
    kernel_panic("Double Fault");
}
