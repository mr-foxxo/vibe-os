#include <userland/modules/include/syscalls.h>

static inline int syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

int sys_poll_mouse(struct mouse_state *state) {
    return syscall5(SYSCALL_INPUT_MOUSE, (int)(uintptr_t)state, 0, 0, 0, 0);
}

int sys_poll_key(void) {
    return syscall5(SYSCALL_INPUT_KEY, 0, 0, 0, 0, 0);
}

void sys_clear(uint8_t color) {
    (void)syscall5(SYSCALL_GFX_CLEAR, color, 0, 0, 0, 0);
}

void sys_rect(int x, int y, int w, int h, uint8_t color) {
    (void)syscall5(SYSCALL_GFX_RECT, x, y, w, h, color);
}

void sys_text(int x, int y, uint8_t color, const char *text) {
    (void)syscall5(SYSCALL_GFX_TEXT, x, y, (int)(uintptr_t)text, color, 0);
}

void sys_present(void) {
    (void)syscall5(SYSCALL_GFX_FLIP, 0, 0, 0, 0, 0);
}

void sys_leave_graphics(void) {
    (void)syscall5(SYSCALL_GFX_LEAVE, 0, 0, 0, 0, 0);
}

int sys_gfx_set_mode(uint32_t width, uint32_t height) {
    return syscall5(SYSCALL_GFX_SET_MODE, (int)width, (int)height, 0, 0, 0);
}

int sys_storage_load(void *dst, uint32_t size) {
    return syscall5(SYSCALL_STORAGE_LOAD, (int)(uintptr_t)dst, (int)size, 0, 0, 0);
}

int sys_storage_save(const void *src, uint32_t size) {
    return syscall5(SYSCALL_STORAGE_SAVE, (int)(uintptr_t)src, (int)size, 0, 0, 0);
}

int sys_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count) {
    return syscall5(SYSCALL_STORAGE_READ_SECTORS,
                    (int)lba,
                    (int)(uintptr_t)dst,
                    (int)sector_count,
                    0,
                    0);
}

void sys_sleep(void) {
    (void)syscall5(SYSCALL_SLEEP, 0, 0, 0, 0, 0);
}

uint32_t sys_ticks(void) {
    return (uint32_t)syscall5(SYSCALL_TIME_TICKS, 0, 0, 0, 0, 0);
}
int sys_gfx_info(struct video_mode *mode) {
    return syscall5(SYSCALL_GFX_INFO, (int)(uintptr_t)mode, 0, 0, 0, 0);
}

int sys_getpid(void) {
    return syscall5(SYSCALL_GETPID, 0, 0, 0, 0, 0);
}

void sys_yield(void) {
    (void)syscall5(SYSCALL_YIELD, 0, 0, 0, 0, 0);
}

void sys_write_debug(const char *msg) {
    (void)syscall5(SYSCALL_WRITE_DEBUG, (int)(uintptr_t)msg, 0, 0, 0, 0);
}

int sys_keyboard_set_layout(const char *name) {
    return syscall5(SYSCALL_KEYBOARD_SET_LAYOUT, (int)(uintptr_t)name, 0, 0, 0, 0);
}

int sys_keyboard_get_layout(char *buffer, int size) {
    return syscall5(SYSCALL_KEYBOARD_GET_LAYOUT, (int)(uintptr_t)buffer, size, 0, 0, 0);
}

int sys_keyboard_get_available_layouts(char *buffer, int size) {
    return syscall5(SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS, (int)(uintptr_t)buffer, size, 0, 0, 0);
}
