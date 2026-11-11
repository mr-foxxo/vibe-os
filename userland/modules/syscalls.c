#include <userland/modules/include/syscalls.h>

#include <stdlib.h>
#include <string.h>

static uint8_t *g_sys_blit_clip_buffer = 0;
static size_t g_sys_blit_clip_buffer_capacity = 0u;

static const uint8_t *sys_pack_clipped_blit_region(const uint8_t *src,
                                                   int src_pitch,
                                                   int src_off_x,
                                                   int src_off_y,
                                                   int width,
                                                   int height) {
    size_t row_bytes;
    size_t total_bytes;
    uint8_t *dst;

    if (src == 0 || src_pitch <= 0 || width <= 0 || height <= 0 ||
        src_off_x < 0 || src_off_y < 0) {
        return 0;
    }

    row_bytes = (size_t)width;
    total_bytes = row_bytes * (size_t)height;
    if (total_bytes == 0u) {
        return 0;
    }
    if (total_bytes > g_sys_blit_clip_buffer_capacity) {
        dst = (uint8_t *)realloc(g_sys_blit_clip_buffer, total_bytes);
        if (dst == 0) {
            return 0;
        }
        g_sys_blit_clip_buffer = dst;
        g_sys_blit_clip_buffer_capacity = total_bytes;
    }

    dst = g_sys_blit_clip_buffer;
    for (int row = 0; row < height; ++row) {
        const uint8_t *src_row = src + ((size_t)(src_off_y + row) * (size_t)src_pitch) + (size_t)src_off_x;
        memcpy(dst + ((size_t)row * row_bytes), src_row, row_bytes);
    }
    return dst;
}

extern int clip_rect(int *out_x, int *out_y, int *out_w, int *out_h,
                     int x, int y, int w, int h) __attribute__((weak));
extern int clip_intersects(int x, int y, int w, int h) __attribute__((weak));

static inline int syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static void sys_text_measure(const char *text, int *out_w, int *out_h) {
    int line_len = 0;
    int max_len = 0;
    int line_count = 1;

    if (out_w == 0 || out_h == 0) {
        return;
    }

    *out_w = 0;
    *out_h = 0;
    if (text == 0 || *text == '\0') {
        return;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            if (line_len > max_len) {
                max_len = line_len;
            }
            line_len = 0;
            line_count += 1;
        } else {
            line_len += 1;
        }
        ++text;
    }

    if (line_len > max_len) {
        max_len = line_len;
    }
    *out_w = max_len > 0 ? (max_len * 6) - 1 : 0;
    *out_h = ((line_count - 1) * 8) + 7;
}

static char sys_text_uppercase_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static uint8_t sys_text_font_row_bits(char c, int row) {
    c = sys_text_uppercase_char(c);
    switch (c) {
    case 'A': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
    case 'B': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return g[row]; }
    case 'C': { static const uint8_t g[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g[row]; }
    case 'D': { static const uint8_t g[7] = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}; return g[row]; }
    case 'E': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return g[row]; }
    case 'F': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return g[row]; }
    case 'G': { static const uint8_t g[7] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; return g[row]; }
    case 'H': { static const uint8_t g[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
    case 'I': { static const uint8_t g[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}; return g[row]; }
    case 'J': { static const uint8_t g[7] = {0x1F,0x02,0x02,0x02,0x12,0x12,0x0C}; return g[row]; }
    case 'K': { static const uint8_t g[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g[row]; }
    case 'L': { static const uint8_t g[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g[row]; }
    case 'M': { static const uint8_t g[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
    case 'N': { static const uint8_t g[7] = {0x11,0x11,0x19,0x15,0x13,0x11,0x11}; return g[row]; }
    case 'O': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'P': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g[row]; }
    case 'Q': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return g[row]; }
    case 'R': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g[row]; }
    case 'S': { static const uint8_t g[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g[row]; }
    case 'T': { static const uint8_t g[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
    case 'U': { static const uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'V': { static const uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return g[row]; }
    case 'W': { static const uint8_t g[7] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; return g[row]; }
    case 'X': { static const uint8_t g[7] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return g[row]; }
    case 'Y': { static const uint8_t g[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return g[row]; }
    case 'Z': { static const uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return g[row]; }
    case '0': { static const uint8_t g[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return g[row]; }
    case '1': { static const uint8_t g[7] = {0x04,0x0C,0x14,0x04,0x04,0x04,0x1F}; return g[row]; }
    case '2': { static const uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; return g[row]; }
    case '3': { static const uint8_t g[7] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}; return g[row]; }
    case '4': { static const uint8_t g[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return g[row]; }
    case '5': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}; return g[row]; }
    case '6': { static const uint8_t g[7] = {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}; return g[row]; }
    case '7': { static const uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return g[row]; }
    case '8': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return g[row]; }
    case '9': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}; return g[row]; }
    case '>': { static const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x04,0x08,0x10}; return g[row]; }
    case '<': { static const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x04,0x02,0x01}; return g[row]; }
    case ':': { static const uint8_t g[7] = {0x00,0x04,0x04,0x00,0x04,0x04,0x00}; return g[row]; }
    case '-': { static const uint8_t g[7] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return g[row]; }
    case '_': { static const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}; return g[row]; }
    case '.': { static const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; return g[row]; }
    case '/': { static const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x10,0x00,0x00}; return g[row]; }
    case '\\': { static const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x01,0x00,0x00}; return g[row]; }
    case '[': { static const uint8_t g[7] = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; return g[row]; }
    case ']': { static const uint8_t g[7] = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}; return g[row]; }
    case '=': { static const uint8_t g[7] = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}; return g[row]; }
    case '+': { static const uint8_t g[7] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}; return g[row]; }
    case '(':{ static const uint8_t g[7] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02}; return g[row]; }
    case ')':{ static const uint8_t g[7] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08}; return g[row]; }
    case '?':{ static const uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}; return g[row]; }
    case '!':{ static const uint8_t g[7] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04}; return g[row]; }
    case ' ': return 0x00;
    default: { static const uint8_t g[7] = {0x1F,0x01,0x05,0x09,0x11,0x00,0x11}; return g[row]; }
    }
}

static void sys_draw_text_fallback(int x, int y, uint8_t color, const char *text) {
    int origin_x = x;
    int cx = x;
    int cy = y;

    if (text == 0) {
        return;
    }

    while (*text != '\0') {
        char c = *text++;

        if (c == '\n') {
            cx = origin_x;
            cy += 8;
            continue;
        }

        for (int row = 0; row < 7; ++row) {
            uint8_t bits = sys_text_font_row_bits(c, row);
            int run_start = -1;

            for (int col = 0; col < 5; ++col) {
                if (bits & (1u << (4 - col))) {
                    if (run_start < 0) {
                        run_start = col;
                    }
                } else if (run_start >= 0) {
                    sys_rect(cx + run_start, cy + row, col - run_start, 1, color);
                    run_start = -1;
                }
            }

            if (run_start >= 0) {
                sys_rect(cx + run_start, cy + row, 5 - run_start, 1, color);
            }
        }

        cx += 6;
    }
}

static int sys_clip_rect_or_passthrough(int *out_x,
                                        int *out_y,
                                        int *out_w,
                                        int *out_h,
                                        int x,
                                        int y,
                                        int w,
                                        int h) {
    if (clip_rect != 0) {
        return clip_rect(out_x, out_y, out_w, out_h, x, y, w, h);
    }
    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
    return w > 0 && h > 0;
}

static int sys_clip_intersects_or_passthrough(int x, int y, int w, int h) {
    if (clip_intersects != 0) {
        return clip_intersects(x, y, w, h);
    }
    return w > 0 && h > 0;
}

int sys_poll_mouse(struct mouse_state *state) {
    return syscall5(SYSCALL_INPUT_MOUSE, (int)(uintptr_t)state, 0, 0, 0, 0);
}

int sys_poll_key(void) {
    return syscall5(SYSCALL_INPUT_KEY, 0, 0, 0, 0, 0);
}

/* Bug #19 item 0.12 (01/11): blocking key read (additive syscall 231).
 * timeout_ticks=0 waits indefinitely; returns key (>0) or -1. Interactive
 * loops use this instead of busy-polling SYSCALL_INPUT_KEY. */
int sys_poll_key_wait(unsigned int timeout_ticks) {
    return syscall5(SYSCALL_INPUT_KEY_WAIT, (int)timeout_ticks, 0, 0, 0, 0);
}

int sys_next_input_event(struct input_event *event) {
    return syscall5(SYSCALL_INPUT_EVENT, (int)(uintptr_t)event, 0, 0, 0, 0);
}

void sys_clear(uint8_t color) {
    (void)syscall5(SYSCALL_GFX_CLEAR, color, 0, 0, 0, 0);
}

void sys_rect(int x, int y, int w, int h, uint8_t color) {
    int clipped_x;
    int clipped_y;
    int clipped_w;
    int clipped_h;

    if (!sys_clip_rect_or_passthrough(&clipped_x, &clipped_y, &clipped_w, &clipped_h,
                                      x, y, w, h)) {
        return;
    }
    (void)syscall5(SYSCALL_GFX_RECT, clipped_x, clipped_y, clipped_w, clipped_h, color);
}

void sys_text(int x, int y, uint8_t color, const char *text) {
    int w;
    int h;
    int clipped_x;
    int clipped_y;
    int clipped_w;
    int clipped_h;

    sys_text_measure(text, &w, &h);
    if (w <= 0 || h <= 0) {
        return;
    }

    if (!sys_clip_rect_or_passthrough(&clipped_x, &clipped_y, &clipped_w, &clipped_h,
                                      x, y, w, h)) {
        return;
    }

    if (clipped_x == x && clipped_y == y && clipped_w == w && clipped_h == h) {
        (void)syscall5(SYSCALL_GFX_TEXT, x, y, (int)(uintptr_t)text, color, 0);
        return;
    }

    sys_draw_text_fallback(x, y, color, text);
}

void sys_present(void) {
    (void)syscall5(SYSCALL_GFX_FLIP, VIDEO_PRESENT_AUTO, 0, 0, 0, 0);
}

void sys_present_dirty(void) {
    (void)syscall5(SYSCALL_GFX_FLIP, VIDEO_PRESENT_DIRTY, 0, 0, 0, 0);
}

void sys_present_full(void) {
    (void)syscall5(SYSCALL_GFX_FLIP, VIDEO_PRESENT_FULL, 0, 0, 0, 0);
}

int sys_gfx_set_present_policy(uint32_t policy) {
    return syscall5(SYSCALL_GFX_SET_PRESENT_POLICY, (int)policy, 0, 0, 0, 0);
}

int sys_gfx_set_present_copy_override(uint32_t kind) {
    return syscall5(SYSCALL_GFX_SET_PRESENT_COPY_OVERRIDE, (int)kind, 0, 0, 0, 0);
}

int sys_video_present_submit(uint32_t mode, uint32_t *sequence_out) {
    return syscall5(SYSCALL_VIDEO_PRESENT_SUBMIT,
                    (int)mode,
                    (int)(uintptr_t)sequence_out,
                    0,
                    0,
                    0);
}

int sys_video_event_subscribe(void) {
    return syscall5(SYSCALL_VIDEO_EVENT_SUBSCRIBE, 0, 0, 0, 0, 0);
}

int sys_video_event_receive(struct mk_video_event *event, uint32_t timeout_ticks) {
    return syscall5(SYSCALL_VIDEO_EVENT_RECV, (int)(uintptr_t)event, (int)timeout_ticks, 0, 0, 0);
}

void sys_leave_graphics(void) {
    (void)syscall5(SYSCALL_GFX_LEAVE, 0, 0, 0, 0, 0);
}

int sys_gfx_set_mode(uint32_t width, uint32_t height, uint8_t bpp) {
    return syscall5(SYSCALL_GFX_SET_MODE, (int)width, (int)height, (int)bpp, 0, 0);
}

int sys_gfx_set_palette(const uint8_t *rgb_triplets) {
    return syscall5(SYSCALL_GFX_SET_PALETTE, (int)(uintptr_t)rgb_triplets, 0, 0, 0, 0);
}

int sys_gfx_get_palette(uint8_t *rgb_triplets) {
    return syscall5(SYSCALL_GFX_GET_PALETTE, (int)(uintptr_t)rgb_triplets, 0, 0, 0, 0);
}

void sys_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    int packed_wh;

    if (src == 0 || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return;
    }
    if (scale == 1) {
        int clipped_x;
        int clipped_y;
        int clipped_w;
        int clipped_h;
        int src_off_x;
        int src_off_y;

        if (!sys_clip_rect_or_passthrough(&clipped_x, &clipped_y, &clipped_w, &clipped_h,
                                          dst_x, dst_y, src_w, src_h)) {
            return;
        }
        src_off_x = clipped_x - dst_x;
        src_off_y = clipped_y - dst_y;
        if (src_off_x != 0 || src_off_y != 0 ||
            clipped_w != src_w || clipped_h != src_h) {
            src = sys_pack_clipped_blit_region(src,
                                               src_w,
                                               src_off_x,
                                               src_off_y,
                                               clipped_w,
                                               clipped_h);
            if (src == 0) {
                return;
            }
        }
        src_w = clipped_w;
        src_h = clipped_h;
        dst_x = clipped_x;
        dst_y = clipped_y;
    }

    packed_wh = ((src_h & 0xFFFF) << 16) | (src_w & 0xFFFF);
    (void)syscall5(SYSCALL_GFX_BLIT8,
                   (int)(uintptr_t)src,
                   packed_wh,
                   dst_x,
                   dst_y,
                   scale);
}

void sys_gfx_blit8_present(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    int packed_wh;

    if (src == 0 || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return;
    }
    if (scale == 1) {
        int clipped_x;
        int clipped_y;
        int clipped_w;
        int clipped_h;
        int src_off_x;
        int src_off_y;

        if (!sys_clip_rect_or_passthrough(&clipped_x, &clipped_y, &clipped_w, &clipped_h,
                                          dst_x, dst_y, src_w, src_h)) {
            return;
        }
        src_off_x = clipped_x - dst_x;
        src_off_y = clipped_y - dst_y;
        if (src_off_x != 0 || src_off_y != 0 ||
            clipped_w != src_w || clipped_h != src_h) {
            src = sys_pack_clipped_blit_region(src,
                                               src_w,
                                               src_off_x,
                                               src_off_y,
                                               clipped_w,
                                               clipped_h);
            if (src == 0) {
                return;
            }
        }
        src_w = clipped_w;
        src_h = clipped_h;
        dst_x = clipped_x;
        dst_y = clipped_y;
    }

    packed_wh = ((src_h & 0xFFFF) << 16) | (src_w & 0xFFFF);
    (void)syscall5(SYSCALL_GFX_BLIT8_PRESENT,
                   (int)(uintptr_t)src,
                   packed_wh,
                   dst_x,
                   dst_y,
                   scale);
}

void sys_gfx_blit8_stretch(const uint8_t *src, int src_w, int src_h,
                           int dst_x, int dst_y, int dst_w, int dst_h) {
    int packed_src_wh = ((src_h & 0xFFFF) << 16) | (src_w & 0xFFFF);
    int packed_dst_wh = ((dst_h & 0xFFFF) << 16) | (dst_w & 0xFFFF);

    if (src == 0 || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }
    if (!sys_clip_intersects_or_passthrough(dst_x, dst_y, dst_w, dst_h)) {
        return;
    }
    (void)syscall5(SYSCALL_GFX_BLIT8_STRETCH,
                   (int)(uintptr_t)src,
                   packed_src_wh,
                   dst_x,
                   dst_y,
                   packed_dst_wh);
}

void sys_gfx_blit8_stretch_present(const uint8_t *src, int src_w, int src_h,
                                   int dst_x, int dst_y, int dst_w, int dst_h) {
    int packed_src_wh = ((src_h & 0xFFFF) << 16) | (src_w & 0xFFFF);
    int packed_dst_wh = ((dst_h & 0xFFFF) << 16) | (dst_w & 0xFFFF);

    if (src == 0 || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }
    if (!sys_clip_intersects_or_passthrough(dst_x, dst_y, dst_w, dst_h)) {
        return;
    }
    (void)syscall5(SYSCALL_GFX_BLIT8_STRETCH_PRESENT,
                   (int)(uintptr_t)src,
                   packed_src_wh,
                   dst_x,
                   dst_y,
                   packed_dst_wh);
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

int sys_storage_write_sectors(uint32_t lba, const void *src, uint32_t sector_count) {
    return syscall5(SYSCALL_STORAGE_WRITE_SECTORS,
                    (int)lba,
                    (int)(uintptr_t)src,
                    (int)sector_count,
                    0,
                    0);
}

uint32_t sys_storage_total_sectors(void) {
    return (uint32_t)syscall5(SYSCALL_STORAGE_TOTAL_SECTORS, 0, 0, 0, 0, 0);
}

uint32_t sys_storage_partition_start_lba(void) {
    return (uint32_t)syscall5(SYSCALL_STORAGE_PARTITION_START_LBA, 0, 0, 0, 0, 0);
}

int sys_storage_backend_load(void *dst, uint32_t size) {
    return syscall5(SYSCALL_STORAGE_BACKEND_LOAD, (int)(uintptr_t)dst, (int)size, 0, 0, 0);
}

int sys_storage_backend_save(const void *src, uint32_t size) {
    return syscall5(SYSCALL_STORAGE_BACKEND_SAVE, (int)(uintptr_t)src, (int)size, 0, 0, 0);
}

int sys_storage_backend_read_sectors(uint32_t lba, void *dst, uint32_t sector_count) {
    return syscall5(SYSCALL_STORAGE_BACKEND_READ_SECTORS,
                    (int)lba,
                    (int)(uintptr_t)dst,
                    (int)sector_count,
                    0,
                    0);
}

int sys_storage_backend_write_sectors(uint32_t lba, const void *src, uint32_t sector_count) {
    return syscall5(SYSCALL_STORAGE_BACKEND_WRITE_SECTORS,
                    (int)lba,
                    (int)(uintptr_t)src,
                    (int)sector_count,
                    0,
                    0);
}

uint32_t sys_storage_backend_total_sectors(void) {
    return (uint32_t)syscall5(SYSCALL_STORAGE_BACKEND_TOTAL_SECTORS, 0, 0, 0, 0, 0);
}

uint32_t sys_storage_backend_partition_start_lba(void) {
    return (uint32_t)syscall5(SYSCALL_STORAGE_BACKEND_PARTITION_START_LBA, 0, 0, 0, 0, 0);
}

int sys_open(const char *path, int flags) {
    return syscall5(SYSCALL_OPEN, (int)(uintptr_t)path, flags, 0, 0, 0);
}

int sys_read(int fd, void *buf, uint32_t count) {
    return syscall5(SYSCALL_READ, fd, (int)(uintptr_t)buf, (int)count, 0, 0);
}

int sys_write(int fd, const void *buf, uint32_t count) {
    return syscall5(SYSCALL_WRITE, fd, (int)(uintptr_t)buf, (int)count, 0, 0);
}

int sys_close(int fd) {
    return syscall5(SYSCALL_CLOSE, fd, 0, 0, 0, 0);
}

off_t sys_lseek(int fd, off_t offset, int whence) {
    return (off_t)syscall5(SYSCALL_LSEEK, fd, (int)offset, whence, 0, 0);
}

int sys_stat(const char *path, struct stat *buf) {
    return syscall5(SYSCALL_STAT, (int)(uintptr_t)path, (int)(uintptr_t)buf, 0, 0, 0);
}

int sys_fstat(int fd, struct stat *buf) {
    return syscall5(SYSCALL_FSTAT, fd, (int)(uintptr_t)buf, 0, 0, 0);
}

int sys_launch_info(struct userland_launch_info *info) {
    return syscall5(SYSCALL_LAUNCH_INFO, (int)(uintptr_t)info, 0, 0, 0, 0);
}

int sys_launch_builtin_user(uint32_t target) {
    return syscall5(SYSCALL_LAUNCH_BUILTIN_USER, (int)target, 0, 0, 0, 0);
}

int sys_task_snapshot(struct task_snapshot_summary *summary,
                      struct task_snapshot_entry *entries,
                      uint32_t max_entries) {
    return syscall5(SYSCALL_TASK_SNAPSHOT,
                    (int)(uintptr_t)summary,
                    (int)(uintptr_t)entries,
                    (int)max_entries,
                    0,
                    0);
}

int sys_memory_status(struct mk_memory_status *status) {
    return syscall5(SYSCALL_MEMORY_STATUS,
                    (int)(uintptr_t)status,
                    0,
                    0,
                    0,
                    0);
}

int sys_memory_budget(struct mk_memory_budget *budget) {
    return syscall5(SYSCALL_MEMORY_BUDGET,
                    (int)(uintptr_t)budget,
                    0,
                    0,
                    0,
                    0);
}

int sys_task_terminate(uint32_t pid) {
    return syscall5(SYSCALL_TASK_TERMINATE, (int)pid, 0, 0, 0, 0);
}

int sys_task_create(uintptr_t entry, void *arg, uint32_t stack_size, uint32_t task_class) {
    return syscall5(SYSCALL_TASK_CREATE,
                    (int)entry,
                    (int)(uintptr_t)arg,
                    (int)stack_size,
                    (int)task_class,
                    0);
}

int sys_launch_app(const char *name) {
    return syscall5(SYSCALL_LAUNCH_APP, (int)(uintptr_t)name, 0, 0, 0, 0);
}

int sys_launch_app_argv(int argc, char **argv) {
    return syscall5(SYSCALL_LAUNCH_APP, (int)(uintptr_t)argv, argc, 0, 0, 0);
}

int sys_task_event_subscribe_mask(uint32_t event_mask, uint32_t task_class_mask) {
    return syscall5(SYSCALL_TASK_EVENT_SUBSCRIBE,
                    (int)event_mask,
                    (int)task_class_mask,
                    0,
                    0,
                    0);
}

int sys_task_event_subscribe(void) {
    return sys_task_event_subscribe_mask(MK_TASK_EVENT_MASK_LIFECYCLE,
                                         MK_TASK_CLASS_MASK_ALL);
}

int sys_task_event_receive(struct mk_task_event *event, uint32_t timeout_ticks) {
    return syscall5(SYSCALL_TASK_EVENT_RECV,
                    (int)(uintptr_t)event,
                    (int)timeout_ticks,
                    0,
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

int sys_gfx_caps(struct video_capabilities *caps) {
    return syscall5(SYSCALL_GFX_CAPS, (int)(uintptr_t)caps, 0, 0, 0, 0);
}

int sys_gfx_bench(struct video_bench_info *bench) {
    return syscall5(SYSCALL_GFX_BENCH, (int)(uintptr_t)bench, 0, 0, 0, 0);
}

int sys_audio_get_info(struct mk_audio_info *info) {
    return syscall5(SYSCALL_AUDIO_GETINFO, (int)(uintptr_t)info, 0, 0, 0, 0);
}

int sys_audio_get_status(struct audio_status *status) {
    return syscall5(SYSCALL_AUDIO_GET_STATUS, (int)(uintptr_t)status, 0, 0, 0, 0);
}

int sys_audio_set_params(const struct audio_swpar *params) {
    return syscall5(SYSCALL_AUDIO_SET_PARAMS, (int)(uintptr_t)params, 0, 0, 0, 0);
}

int sys_audio_start(void) {
    return syscall5(SYSCALL_AUDIO_START, 0, 0, 0, 0, 0);
}

int sys_audio_stop(void) {
    return syscall5(SYSCALL_AUDIO_STOP, 0, 0, 0, 0, 0);
}

int sys_audio_write(const void *data, uint32_t size) {
    return syscall5(SYSCALL_AUDIO_WRITE, (int)(uintptr_t)data, (int)size, 0, 0, 0);
}

int sys_audio_write_async(const void *data, uint32_t size) {
    return syscall5(SYSCALL_AUDIO_WRITE_ASYNC, (int)(uintptr_t)data, (int)size, 0, 0, 0);
}

int sys_audio_event_subscribe(void) {
    return syscall5(SYSCALL_AUDIO_EVENT_SUBSCRIBE, 0, 0, 0, 0, 0);
}

int sys_audio_event_receive(struct mk_audio_event *event, uint32_t timeout_ticks) {
    return syscall5(SYSCALL_AUDIO_EVENT_RECV,
                    (int)(uintptr_t)event,
                    (int)timeout_ticks,
                    0,
                    0,
                    0);
}

int sys_audio_read(void *data, uint32_t size) {
    return syscall5(SYSCALL_AUDIO_READ, (int)(uintptr_t)data, (int)size, 0, 0, 0);
}

int sys_audio_control_info(uint32_t index, struct mk_audio_control_info *info) {
    return syscall5(SYSCALL_AUDIO_CONTROL_INFO, (int)index, (int)(uintptr_t)info, 0, 0, 0);
}

int sys_audio_mixer_read(mixer_ctrl_t *control) {
    return syscall5(SYSCALL_AUDIO_MIXER_READ, (int)(uintptr_t)control, 0, 0, 0, 0);
}

int sys_audio_mixer_write(const mixer_ctrl_t *control) {
    return syscall5(SYSCALL_AUDIO_MIXER_WRITE, (int)(uintptr_t)control, 0, 0, 0, 0);
}

int sys_audio_play_asset(const char *path) {
    return syscall5(SYSCALL_AUDIO_PLAY_ASSET, (int)(uintptr_t)path, 0, 0, 0, 0);
}

int sys_network_get_info(struct mk_network_info *info) {
    return syscall5(SYSCALL_NETWORK_GETINFO, (int)(uintptr_t)info, 0, 0, 0, 0);
}

int sys_network_get_status(struct mk_network_status *status) {
    return syscall5(SYSCALL_NETWORK_GET_STATUS, (int)(uintptr_t)status, 0, 0, 0, 0);
}

int sys_network_socket_list(uint32_t index, struct mk_network_socket_list_entry *entry) {
    return syscall5(SYSCALL_NETWORK_SOCKET_LIST, (int)index, (int)(uintptr_t)entry, 0, 0, 0);
}

int sys_network_scan(uint32_t index, struct mk_network_scan_info *info) {
    return syscall5(SYSCALL_NETWORK_SCAN, (int)index, (int)(uintptr_t)info, 0, 0, 0);
}

int sys_network_connect_wifi(const struct mk_network_connect_request *request) {
    return syscall5(SYSCALL_NETWORK_CONNECT_WIFI, (int)(uintptr_t)request, 0, 0, 0, 0);
}

int sys_network_connect_ethernet(const char *if_name) {
    return syscall5(SYSCALL_NETWORK_CONNECT_ETHERNET, (int)(uintptr_t)if_name, 0, 0, 0, 0);
}

int sys_network_configure_ethernet(const struct mk_network_ethernet_config *config) {
    return syscall5(SYSCALL_NETWORK_CONFIGURE_ETHERNET, (int)(uintptr_t)config, 0, 0, 0, 0);
}

int sys_network_disconnect(const char *if_name) {
    return syscall5(SYSCALL_NETWORK_DISCONNECT, (int)(uintptr_t)if_name, 0, 0, 0, 0);
}

int sys_network_socket(uint32_t domain, uint32_t type, uint32_t protocol) {
    return syscall5(SYSCALL_NETWORK_SOCKET, (int)domain, (int)type, (int)protocol, 0, 0);
}

int sys_network_bind(int handle, const struct sockaddr *address, uint32_t address_length) {
    return syscall5(SYSCALL_NETWORK_BIND,
                    handle,
                    (int)(uintptr_t)address,
                    (int)address_length,
                    0,
                    0);
}

int sys_network_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length) {
    return syscall5(SYSCALL_NETWORK_CONNECT,
                    handle,
                    (int)(uintptr_t)address,
                    (int)address_length,
                    0,
                    0);
}

int sys_network_send(int handle, const void *data, uint32_t size) {
    return syscall5(SYSCALL_NETWORK_SEND,
                    handle,
                    (int)(uintptr_t)data,
                    (int)size,
                    0,
                    0);
}

int sys_network_recv(int handle, void *buffer, uint32_t size) {
    return syscall5(SYSCALL_NETWORK_RECV,
                    handle,
                    (int)(uintptr_t)buffer,
                    (int)size,
                    0,
                    0);
}

int sys_network_sendto(int handle, const void *data, uint32_t size,
                       const struct sockaddr *dest_addr, uint32_t addrlen) {
    return syscall5(SYSCALL_NETWORK_SENDTO,
                    handle,
                    (int)(uintptr_t)data,
                    (int)size,
                    (int)(uintptr_t)dest_addr,
                    (int)addrlen);
}

int sys_network_recvfrom(int handle, void *buffer, uint32_t size,
                         struct sockaddr *src_addr, uint32_t *addrlen) {
    return syscall5(SYSCALL_NETWORK_RECVFROM,
                    handle,
                    (int)(uintptr_t)buffer,
                    (int)size,
                    (int)(uintptr_t)src_addr,
                    (int)(uintptr_t)addrlen);
}

int sys_network_close(int handle) {
    return syscall5(SYSCALL_NETWORK_CLOSE, handle, 0, 0, 0, 0);
}

int sys_network_listen(int handle, int backlog) {
    return syscall5(SYSCALL_NETWORK_LISTEN, handle, backlog, 0, 0, 0);
}

int sys_network_accept(int handle) {
    return syscall5(SYSCALL_NETWORK_ACCEPT, handle, 0, 0, 0, 0);
}

int sys_network_event_subscribe(void) {
    return syscall5(SYSCALL_NETWORK_EVENT_SUBSCRIBE, 0, 0, 0, 0, 0);
}

int sys_network_event_receive(struct mk_network_event *event, uint32_t timeout_ticks) {
    return syscall5(SYSCALL_NETWORK_EVENT_RECV,
                    (int)(uintptr_t)event,
                    (int)timeout_ticks,
                    0,
                    0,
                    0);
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

void sys_text_clear(void) {
    (void)syscall5(SYSCALL_TEXT_CLEAR, 0, 0, 0, 0, 0);
}

void sys_text_putc(char c) {
    (void)syscall5(SYSCALL_TEXT_PUTC, (int)(uint8_t)c, 0, 0, 0, 0);
}

int sys_text_write(const char *msg) {
    return syscall5(SYSCALL_TEXT_WRITE, (int)(uintptr_t)msg, 0, 0, 0, 0);
}

int sys_text_move_cursor(int delta) {
    return syscall5(SYSCALL_TEXT_MOVE_CURSOR, delta, 0, 0, 0, 0);
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

int sys_service_receive(struct mk_message *message) {
    return syscall5(SYSCALL_SERVICE_RECV, (int)(uintptr_t)message, 0, 0, 0, 0);
}

int sys_service_send(const struct mk_message *message) {
    return syscall5(SYSCALL_SERVICE_SEND, (int)(uintptr_t)message, 0, 0, 0, 0);
}

int sys_message_post(struct mk_async_message *message) {
    return syscall5(SYSCALL_MESSAGE_POST, (int)(uintptr_t)message, 0, 0, 0, 0);
}

int sys_message_receive(struct mk_async_message *message, uint32_t timeout_ticks) {
    return syscall5(SYSCALL_MESSAGE_RECV,
                    (int)(uintptr_t)message,
                    (int)timeout_ticks,
                    0,
                    0,
                    0);
}

int sys_service_subscribe(uint32_t service_type) {
    return syscall5(SYSCALL_SERVICE_SUBSCRIBE, (int)service_type, 0, 0, 0, 0);
}

int sys_service_pid(uint32_t service_type) {
    return syscall5(SYSCALL_SERVICE_PID, (int)service_type, 0, 0, 0, 0);
}

int sys_service_restart(uint32_t service_type) {
    return syscall5(SYSCALL_SERVICE_RESTART, (int)service_type, 0, 0, 0, 0);
}

int sys_service_health(uint32_t service_type, struct mk_service_health_stats *stats) {
    return syscall5(SYSCALL_SERVICE_HEALTH, (int)(uintptr_t)stats, (int)service_type, 0, 0, 0);
}

int sys_service_event_receive(uint32_t service_type,
                              struct mk_service_event *event,
                              uint32_t timeout_ticks) {
    return syscall5(SYSCALL_SERVICE_EVENT_RECV,
                    (int)service_type,
                    (int)(uintptr_t)event,
                    (int)timeout_ticks,
                    0,
                    0);
}

uint32_t sys_transfer_size(uint32_t transfer_id) {
    return (uint32_t)syscall5(SYSCALL_TRANSFER_SIZE, (int)transfer_id, 0, 0, 0, 0);
}

int sys_transfer_read(uint32_t transfer_id, void *dst, uint32_t size) {
    return syscall5(SYSCALL_TRANSFER_READ,
                    (int)transfer_id,
                    (int)(uintptr_t)dst,
                    (int)size,
                    0,
                    0);
}

int sys_transfer_write(uint32_t transfer_id, const void *src, uint32_t size) {
    return syscall5(SYSCALL_TRANSFER_WRITE,
                    (int)transfer_id,
                    (int)(uintptr_t)src,
                    (int)size,
                    0,
                    0);
}

void sys_shutdown(void) {
    (void)syscall5(SYSCALL_SHUTDOWN, 0, 0, 0, 0, 0);
}
