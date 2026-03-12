#include <kernel/kernel.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/input/input.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/process.h>
#include <stdint.h>
#include <include/userland_api.h> /* syscall IDs */

/* syscall table and helpers for the new kernel-side mechanism.  The
   legacy stage2 dispatch is still compiled into the image; eventually we
   will migrate completely to this table-driven approach. */

#define MAX_SYSCALLS 64
typedef uint32_t (*syscall_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static syscall_fn syscall_table[MAX_SYSCALLS];

static uint32_t sys_gfx_clear(uint32_t color, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_video_clear((uint8_t)(color & 0xFFu));
    kernel_video_flip();
    return 0;
}

static uint32_t sys_gfx_rect(uint32_t x, uint32_t y, uint32_t w,
                             uint32_t h, uint32_t color) {
    kernel_gfx_rect((int)x, (int)y, (int)w, (int)h, (uint8_t)(color & 0xFFu));
    kernel_video_flip();
    return 0;
}

static uint32_t sys_gfx_text(uint32_t x, uint32_t y, uint32_t text_ptr,
                             uint32_t color, uint32_t e) {
    (void)e;
    const char *msg = (const char *)(uintptr_t)text_ptr;
    kernel_gfx_draw_text((int)x, (int)y, msg, (uint8_t)(color & 0xFFu));
    kernel_video_flip();
    return 0;
}

static uint32_t sys_input_mouse(uint32_t state_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (state_ptr == 0)
        return 0;
    struct mouse_state *out = (struct mouse_state *)(uintptr_t)state_ptr;
    int8_t dx = 0, dy = 0;
    uint8_t buttons = 0;
    if (!kernel_mouse_has_data())
        return 0;
    kernel_mouse_read(&dx, &dy, &buttons);
    /* accumulate deltas into absolute coords (clamp on caller side if needed) */
    out->x += dx;
    out->y += dy;
    out->buttons = buttons;
    return 1;
}

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

static uint32_t sys_input_key(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)kernel_keyboard_read();
}

static uint32_t sys_text_clear(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    kernel_text_clear();
    return 0;
}

static uint32_t sys_text_putc(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_text_putc((char)(a & 0xFF));
    return 0;
}

static uint32_t sys_sleep(uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    __asm__ volatile("hlt");
    return 0;
}

static uint32_t sys_time_ticks(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return kernel_timer_get_ticks();
}

static uint32_t sys_gfx_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0)
        return (uint32_t)-1;
    struct video_mode *out = (struct video_mode *)(uintptr_t)out_ptr;
    struct video_mode *mode = kernel_video_get_mode();
    out->fb_addr = mode->fb_addr;
    out->width = mode->width;
    out->height = mode->height;
    out->pitch = mode->pitch;
    out->bpp = mode->bpp;
    return 0;
}

void syscall_init(void) {
    /* register new kernel syscalls; numbers are defined in
       include/userland_api.h */
    syscall_table[SYSCALL_GFX_CLEAR] = sys_gfx_clear;
    syscall_table[SYSCALL_GFX_RECT] = sys_gfx_rect;
    syscall_table[SYSCALL_GFX_TEXT] = sys_gfx_text;
    syscall_table[SYSCALL_INPUT_MOUSE] = sys_input_mouse;
    syscall_table[SYSCALL_INPUT_KEY] = sys_input_key;
    syscall_table[12] = sys_text_putc;     /* legacy text mode */
    syscall_table[13] = sys_text_clear;    /* legacy text mode */
    syscall_table[SYSCALL_SLEEP] = sys_sleep;
    syscall_table[SYSCALL_TIME_TICKS] = sys_time_ticks;
    syscall_table[SYSCALL_GFX_INFO] = sys_gfx_info;
    syscall_table[SYSCALL_GETPID] = sys_getpid;
    syscall_table[SYSCALL_YIELD] = sys_yield;
    syscall_table[SYSCALL_WRITE_DEBUG] = sys_write_debug;
}

/* dispatch routine called by ISR */
uint32_t syscall_dispatch_internal(uint32_t num, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d, uint32_t e) {
    if (num < MAX_SYSCALLS && syscall_table[num]) {
        return syscall_table[num](a, b, c, d, e);
    }
    return (uint32_t)-1;
}
