#include <kernel/kernel.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/input/input.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/process.h>
#include <kernel/hal/io.h>
#include <stdint.h>
#include <include/userland_api.h> /* syscall IDs */
#include <string.h>
#include <kernel/drivers/input/input.h>

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
    return 0;
}

static uint32_t sys_gfx_rect(uint32_t x, uint32_t y, uint32_t w,
                             uint32_t h, uint32_t color) {
    kernel_gfx_rect((int)x, (int)y, (int)w, (int)h, (uint8_t)(color & 0xFFu));
    return 0;
}

static uint32_t sys_gfx_text(uint32_t x, uint32_t y, uint32_t text_ptr,
                             uint32_t color, uint32_t e) {
    (void)e;
    const char *msg = (const char *)(uintptr_t)text_ptr;
    kernel_gfx_draw_text((int)x, (int)y, msg, (uint8_t)(color & 0xFFu));
    return 0;
}

static uint32_t sys_gfx_flip(uint32_t a, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    kernel_video_flip();
    return 0;
}

static uint32_t sys_gfx_blit8(uint32_t src_ptr, uint32_t packed_wh, uint32_t dst_x,
                              uint32_t dst_y, uint32_t scale) {
    int src_w = (int)(packed_wh & 0xFFFFu);
    int src_h = (int)((packed_wh >> 16) & 0xFFFFu);
    kernel_gfx_blit8((const uint8_t *)(uintptr_t)src_ptr,
                     src_w,
                     src_h,
                     (int)dst_x,
                     (int)dst_y,
                     (int)scale);
    return 0;
}

static uint32_t sys_gfx_leave(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    kernel_video_leave_graphics();
    return 0;
}

static uint32_t sys_gfx_set_mode(uint32_t width, uint32_t height, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)kernel_video_set_mode(width, height);
}

static uint32_t sys_gfx_set_palette(uint32_t ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)kernel_video_set_palette((const uint8_t *)(uintptr_t)ptr);
}

static uint32_t sys_gfx_get_palette(uint32_t ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)kernel_video_get_palette((uint8_t *)(uintptr_t)ptr);
}

static uint32_t sys_storage_load(uint32_t ptr, uint32_t size, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)kernel_storage_load((void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_save(uint32_t ptr, uint32_t size, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)kernel_storage_save((const void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_read_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                         uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)kernel_storage_read_sectors(lba, (void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_write_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                          uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)kernel_storage_write_sectors(lba, (const void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_total_sectors(uint32_t a, uint32_t b, uint32_t c,
                                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return kernel_storage_total_sectors();
}

static uint32_t sys_input_mouse(uint32_t state_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (state_ptr == 0)
        return 0;
    struct mouse_state *out = (struct mouse_state *)(uintptr_t)state_ptr;
    int x = 0, y = 0;
    uint8_t buttons = 0;
    if (!kernel_mouse_has_data())
        return 0;
    kernel_mouse_read(&x, &y, &buttons);
    out->x = x;
    out->y = y;
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
    uint32_t value;

    (void)a; (void)b; (void)c; (void)d; (void)e;
    __asm__ volatile("cli" : : : "memory");
    value = (uint32_t)kernel_keyboard_read();
    __asm__ volatile("sti" : : : "memory");
    return value;
}

static uint32_t sys_text_clear(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    kernel_text_clear();
    return 0;
}

static uint32_t sys_text_move_cursor(uint32_t a, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_text_move_cursor((int32_t)a);
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
    kernel_gfx_clear(0u);
    kernel_video_flip();
    struct video_mode *out = (struct video_mode *)(uintptr_t)out_ptr;
    struct video_mode *mode = kernel_video_get_mode();
    out->fb_addr = mode->fb_addr;
    out->width = mode->width;
    out->height = mode->height;
    out->pitch = mode->pitch;
    out->bpp = mode->bpp;
    return 0;
}

static uint32_t sys_gfx_caps(uint32_t out_ptr, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    struct video_capabilities *out;

    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0) {
        return (uint32_t)-1;
    }

    out = (struct video_capabilities *)(uintptr_t)out_ptr;
    kernel_video_get_capabilities(out);
    return 0;
}

static uint32_t sys_keyboard_set_layout(uint32_t name_ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    const char* name = (const char*)(uintptr_t)name_ptr;
    return (uint32_t)kernel_keyboard_set_layout(name);
}

static uint32_t sys_keyboard_get_layout(uint32_t buffer_ptr, uint32_t size, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    char* buffer = (char*)(uintptr_t)buffer_ptr;
    const char* name = kernel_keyboard_get_layout();
    uint32_t len = strlen(name);
    if (size > len) {
        strcpy(buffer, name);
        return len;
    }
    return 0;
}

static uint32_t sys_keyboard_get_available_layouts(uint32_t buffer_ptr, uint32_t size, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    char* buffer = (char*)(uintptr_t)buffer_ptr;
    kernel_keyboard_get_available_layouts(buffer, size);
    return 0;
}

static uint32_t sys_shutdown(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;

    kernel_debug_puts("sys_shutdown: poweroff requested\n");

    /* Common ACPI/QEMU/Bochs poweroff ports. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    __asm__ volatile("cli" : : : "memory");
    for (;;) {
        __asm__ volatile("hlt");
    }

    __builtin_unreachable();
}

void syscall_init(void) {
    /* register new kernel syscalls; numbers are defined in
       include/userland_api.h */
    syscall_table[SYSCALL_GFX_CLEAR] = sys_gfx_clear;
    syscall_table[SYSCALL_GFX_RECT] = sys_gfx_rect;
    syscall_table[SYSCALL_GFX_TEXT] = sys_gfx_text;
    syscall_table[SYSCALL_GFX_FLIP] = sys_gfx_flip;
    syscall_table[SYSCALL_GFX_BLIT8] = sys_gfx_blit8;
    syscall_table[SYSCALL_GFX_LEAVE] = sys_gfx_leave;
    syscall_table[SYSCALL_GFX_SET_MODE] = sys_gfx_set_mode;
    syscall_table[SYSCALL_GFX_SET_PALETTE] = sys_gfx_set_palette;
    syscall_table[SYSCALL_GFX_GET_PALETTE] = sys_gfx_get_palette;
    syscall_table[SYSCALL_STORAGE_LOAD] = sys_storage_load;
    syscall_table[SYSCALL_STORAGE_SAVE] = sys_storage_save;
    syscall_table[SYSCALL_STORAGE_READ_SECTORS] = sys_storage_read_sectors;
    syscall_table[SYSCALL_STORAGE_WRITE_SECTORS] = sys_storage_write_sectors;
    syscall_table[SYSCALL_STORAGE_TOTAL_SECTORS] = sys_storage_total_sectors;
    syscall_table[SYSCALL_INPUT_MOUSE] = sys_input_mouse;
    syscall_table[SYSCALL_INPUT_KEY] = sys_input_key;
    syscall_table[12] = sys_text_putc;     /* legacy text mode */
    syscall_table[13] = sys_text_clear;    /* legacy text mode */
    syscall_table[SYSCALL_TEXT_MOVE_CURSOR] = sys_text_move_cursor;
    syscall_table[SYSCALL_SLEEP] = sys_sleep;
    syscall_table[SYSCALL_TIME_TICKS] = sys_time_ticks;
    syscall_table[SYSCALL_GFX_INFO] = sys_gfx_info;
    syscall_table[SYSCALL_GFX_CAPS] = sys_gfx_caps;
    syscall_table[SYSCALL_GETPID] = sys_getpid;
    syscall_table[SYSCALL_YIELD] = sys_yield;
    syscall_table[SYSCALL_WRITE_DEBUG] = sys_write_debug;
    syscall_table[SYSCALL_KEYBOARD_SET_LAYOUT] = sys_keyboard_set_layout;
    syscall_table[SYSCALL_KEYBOARD_GET_LAYOUT] = sys_keyboard_get_layout;
    syscall_table[SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS] = sys_keyboard_get_available_layouts;
    syscall_table[SYSCALL_SHUTDOWN] = sys_shutdown;
}

/* dispatch routine called by ISR */
uint32_t syscall_dispatch_internal(uint32_t num, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d, uint32_t e) {
    if (num < MAX_SYSCALLS && syscall_table[num]) {
        return syscall_table[num](a, b, c, d, e);
    }
    return (uint32_t)-1;
}
