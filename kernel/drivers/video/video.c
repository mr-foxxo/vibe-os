#include <kernel/bootinfo.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/video/drm/drm.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/input/input.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/microkernel/video.h>
#include <kernel/cpu/cpu.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/paging.h>
#include <kernel/driver_manager.h>
#include <string.h>

#define GRAPHICS_MIN_WIDTH 640u
#define GRAPHICS_MIN_HEIGHT 480u
#define GRAPHICS_MAX_WIDTH 4096u
#define GRAPHICS_MAX_HEIGHT 2160u
#define GRAPHICS_BPP 8u
#define VIDEO_MODE_8BIT 8u
#define VIDEO_MODE_16BIT 16u
#define VIDEO_MODE_24BIT 24u
#define GRAPHICS_MIN_FB_ADDR 0x00100000u
#define VIDEO_BACKBUFFER_MAX_BYTES (4u * 1024u * 1024u)
#define VIDEO_BACKBUFFER_HEAP_RESERVE (2u * 1024u * 1024u)

#ifndef VIDEO_DRM_TEST_FORCE_HANDOFF_FAIL
#define VIDEO_DRM_TEST_FORCE_HANDOFF_FAIL 0
#endif

#define VGA_PEL_MASK 0x3C6u
#define VGA_DAC_WRITE_INDEX 0x3C8u
#define VGA_DAC_DATA 0x3C9u
enum kernel_video_backend_kind {
    KERNEL_VIDEO_BACKEND_NONE = 0,
    KERNEL_VIDEO_BACKEND_LEGACY_LFB,
    KERNEL_VIDEO_BACKEND_FAST_LFB
};

enum kernel_video_present_kind {
    KERNEL_VIDEO_PRESENT_BYTE_LOOP = 0,
    KERNEL_VIDEO_PRESENT_REP_MOVSD,
    KERNEL_VIDEO_PRESENT_MOVNTDQ
};

enum kernel_video_auto_present_policy {
    KERNEL_VIDEO_AUTO_PRESENT_DIRTY = 0,
    KERNEL_VIDEO_AUTO_PRESENT_FULLSCREEN
};

struct kernel_video_backend_ops {
    const char *name;
    int supports_shadow;
    void (*activate)(const struct video_mode *mode);
    void (*present_full)(void);
    void (*present_rect)(const struct kernel_video_rect *rect);
};

struct kernel_video_perf {
    uint32_t active_width;
    uint32_t active_height;
    uint32_t active_pitch;
    uint32_t gpu_vendor_id;
    uint32_t gpu_device_id;
    uint32_t gpu_revision;
    uint32_t detected_gpu_vendor_id;
    uint32_t detected_gpu_device_id;
    uint32_t detected_gpu_revision;
    uint32_t cpu_family;
    uint32_t cpu_model;
    uint32_t cpu_stepping;
    uint32_t fill_ticks;
    uint32_t present_ticks;
    uint32_t frame_ticks;
    uint32_t fullscreen_direct_ticks;
    uint32_t fullscreen_blit_present_ticks;
    uint32_t microkernel_frame_ticks;
    uint32_t microkernel_flip_ticks;
    uint32_t microkernel_blit_ticks;
    uint32_t microkernel_stretch_ticks;
    size_t frame_bytes;
    size_t backbuffer_bytes;
    size_t heap_free_before;
    size_t heap_free_after;
    uint32_t cpu_has_pat;
    uint32_t cpu_has_sse2;
    uint32_t wc_enabled;
    uint32_t backend_kind;
    uint32_t present_copy_kind;
    uint32_t present_copy_override_kind;
    uint32_t native_backend_kind;
    uint32_t detected_native_backend_kind;
    char cpu_vendor[13];
};

static struct video_mode g_mode;
static volatile uint8_t *g_fb = 0;
static uint8_t *g_backbuf = 0;
static uint8_t *g_shadow_backbuf = 0;
static size_t g_shadow_backbuf_capacity = 0u;
/*
 * LFB aperture actually reserved+mapped for the framebuffer (set by
 * kernel_video_set_fb_window from entry.c). The boot text shim must clamp
 * writes by this, not by mode->pitch*height: a 640x480x32 frame is 1.2 MB
 * while the mapped window can be smaller — writing "inside the mode" past
 * the aperture is how the smp2 #PF (err=3, cr2 inside kernel .text)
 * happened (COMPLETION_ACTION_PLAN P4, classe #5 face-2).
 */
static uintptr_t g_fb_window_base = 0u;
static size_t g_fb_window_bytes = 0u;
static uint8_t g_palette[256u * 3u];
static int g_palette_ready = 0;
static int g_graphics_enabled = 0;
static int g_video_initialized = 0;
static int g_backbuf_alloc_failed = 0;
static int g_direct_fullscreen_present_dirty = 0;
static enum kernel_video_backend_kind g_backend_kind = KERNEL_VIDEO_BACKEND_NONE;
static enum kernel_video_present_kind g_present_kind = KERNEL_VIDEO_PRESENT_BYTE_LOOP;
static uint32_t g_present_copy_override_kind = VIDEO_PRESENT_COPY_OVERRIDE_AUTO;
static enum kernel_video_auto_present_policy g_auto_present_policy =
    KERNEL_VIDEO_AUTO_PRESENT_DIRTY;
static struct kernel_video_perf g_video_perf = {0};
static const struct kernel_video_backend_ops *g_backend_ops = 0;
static struct kernel_video_rect g_dirty_rect = {0, 0, 0, 0};
static int g_dirty_full = 0;
static int g_dirty_pending = 0;

extern int kernel_video_bios_set_mode(uint16_t mode);

static int kernel_video_has_graphics_mode(void);
static int kernel_video_backbuffer_is_lfb(void);
static size_t kernel_video_frame_bytes(void);
static int kernel_video_fast_backend_usable(const struct video_mode *mode);
static int kernel_video_backend_prefers_safe_present_copy(void);
static enum kernel_video_backend_kind kernel_video_choose_boot_backend(const struct video_mode *mode);
static void kernel_video_log_state(const char *reason);
static const char *kernel_video_present_kind_name(enum kernel_video_present_kind kind);
static void kernel_video_log_handoff(const char *stage, const char *source);
static uint32_t kernel_video_measure_fill_time(void);
static uint32_t kernel_video_measure_present_time(void);
static uint32_t kernel_video_measure_frame_time(void);
static uint32_t kernel_video_measure_fullscreen_direct_time(void);
static uint32_t kernel_video_measure_fullscreen_blit_present_time(void);
static uint32_t kernel_video_measure_microkernel_frame_time(void);
static uint32_t kernel_video_measure_microkernel_flip_time(void);
static uint32_t kernel_video_measure_microkernel_blit_time(void);
static uint32_t kernel_video_measure_microkernel_stretch_time(void);
static void __attribute__((unused)) kernel_video_record_benchmarks(void);
static void kernel_video_activate_backend(enum kernel_video_backend_kind kind,
                                          const struct video_mode *mode);
static void kernel_video_select_present_kind(void);
static int kernel_video_cpu_prefers_conservative_stream_copy(void);
static void kernel_video_present_rect_internal(const struct kernel_video_rect *rect);
static void kernel_video_mark_full_dirty(void);
static void kernel_video_reset_dirty_state(void);
static void kernel_video_present_auto(void);
static void kernel_video_resync_shadow_backbuffer(void);
static void kernel_video_bind_legacy_lfb(const struct video_mode *mode);
static void kernel_video_bind_fast_lfb(const struct video_mode *mode);
static int kernel_video_find_alternate_catalog_mode(uint32_t *width_out,
                                                    uint32_t *height_out);
static void kernel_video_run_drm_recovery_selftest(void);
static void kernel_video_present_full_legacy_lfb(void);
static void kernel_video_present_rect_legacy_lfb(const struct kernel_video_rect *rect);
static void kernel_video_present_full_fast_lfb(void);
static void kernel_video_present_rect_fast_lfb(const struct kernel_video_rect *rect);
static size_t kernel_video_blit8_source_pitch(const uint8_t *src, int src_w, int src_h);
static void kernel_video_blit8_stretch_to_target(const uint8_t *src, int src_w, int src_h,
                                                 int dst_x, int dst_y, int dst_w, int dst_h,
                                                 uint8_t *dst, uint32_t pitch, uint32_t width,
                                                 uint32_t height);
static void kernel_video_blit8_stretch_to_backbuffer(const uint8_t *src, int src_w, int src_h,
                                                     int dst_x, int dst_y, int dst_w, int dst_h,
                                                     int mark_dirty);

static const struct kernel_video_backend_ops g_backend_legacy_lfb_ops = {
    "legacy_lfb",
    0,
    kernel_video_bind_legacy_lfb,
    kernel_video_present_full_legacy_lfb,
    kernel_video_present_rect_legacy_lfb
};

static const struct kernel_video_backend_ops g_backend_fast_lfb_ops = {
    "fast_lfb",
    1,
    kernel_video_bind_fast_lfb,
    kernel_video_present_full_fast_lfb,
    kernel_video_present_rect_fast_lfb
};

static void kernel_video_detect_cpu_features(void) {
    const struct kernel_cpu_topology *topology = kernel_cpu_topology();

    g_video_perf.cpu_has_pat = kernel_cpu_has_pat() ? 1u : 0u;
    g_video_perf.cpu_has_sse2 = kernel_cpu_has_sse2() ? 1u : 0u;
    if (topology != 0) {
        g_video_perf.cpu_family = topology->cpuid_family;
        g_video_perf.cpu_model = topology->cpuid_model;
        g_video_perf.cpu_stepping = topology->cpuid_stepping;
        memcpy(g_video_perf.cpu_vendor, topology->vendor, sizeof(g_video_perf.cpu_vendor));
        g_video_perf.cpu_vendor[sizeof(g_video_perf.cpu_vendor) - 1u] = '\0';
    } else {
        g_video_perf.cpu_family = 0u;
        g_video_perf.cpu_model = 0u;
        g_video_perf.cpu_stepping = 0u;
        g_video_perf.cpu_vendor[0] = '\0';
    }
    kernel_video_select_present_kind();
}

static void kernel_video_select_present_kind(void) {
    if (g_present_copy_override_kind == VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP) {
        g_present_kind = KERNEL_VIDEO_PRESENT_BYTE_LOOP;
        return;
    }
    if (g_present_copy_override_kind == VIDEO_PRESENT_COPY_OVERRIDE_REP_MOVSD) {
        g_present_kind = KERNEL_VIDEO_PRESENT_REP_MOVSD;
        return;
    }
    if (g_present_copy_override_kind == VIDEO_PRESENT_COPY_OVERRIDE_MOVNTDQ) {
        if (kernel_cpu_sse_enabled() &&
            paging_pat_wc_enabled() &&
            !kernel_video_backbuffer_is_lfb()) {
            g_present_kind = KERNEL_VIDEO_PRESENT_MOVNTDQ;
            return;
        }
        g_present_kind = KERNEL_VIDEO_PRESENT_REP_MOVSD;
        return;
    }

    if (!kernel_video_backbuffer_is_lfb()) {
        if (!kernel_video_backend_prefers_safe_present_copy()) {
            if (kernel_cpu_sse_enabled() && paging_pat_wc_enabled()) {
                if (kernel_video_cpu_prefers_conservative_stream_copy()) {
                    g_present_kind = KERNEL_VIDEO_PRESENT_REP_MOVSD;
                    return;
                }
                g_present_kind = KERNEL_VIDEO_PRESENT_MOVNTDQ;
                return;
            }
            g_present_kind = KERNEL_VIDEO_PRESENT_REP_MOVSD;
            return;
        }
        g_present_kind = KERNEL_VIDEO_PRESENT_BYTE_LOOP;
        return;
    }
    g_present_kind = KERNEL_VIDEO_PRESENT_REP_MOVSD;
}

static int kernel_video_cpu_prefers_conservative_stream_copy(void) {
    const struct kernel_cpu_topology *topology = kernel_cpu_topology();

    if (topology == 0 || topology->cpuid_supported == 0u) {
        return 0;
    }

    /*
     * Older Intel Core/Pentium/Celeron parts can exhibit visible corruption in
     * the first scanlines of a WC-mapped framebuffer when we drive presents via
     * movntdq under concurrent DMA load (notably during desktop audio startup).
     * Keeping those CPUs on rep movsd is slower but materially more stable,
     * while newer parts such as Ivy Bridge and later keep the faster path.
     */
    if (strcmp(topology->vendor, "GenuineIntel") == 0) {
        return topology->cpuid_family == 6u && topology->cpuid_model < 0x1Au;
    }

    /*
     * Apply the same conservative choice to very old AMD family-0Fh parts,
     * where write-combined streaming stores are also more likely to misbehave
     * against legacy LFB mappings than plain cached copies.
     */
    if (strcmp(topology->vendor, "AuthenticAMD") == 0) {
        return topology->cpuid_family <= 0x0Fu;
    }

    return 0;
}

static void kernel_video_copy_row_byte(uint8_t *dst, const volatile uint8_t *src, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) {
        dst[i] = src[i];
    }
}

static void kernel_video_copy_row_safe(volatile uint8_t *dst, const uint8_t *src, size_t bytes) {
    if (dst == 0 || src == 0 || bytes == 0u) {
        return;
    }
    for (size_t i = 0; i < bytes; ++i) {
        dst[i] = src[i];
    }
}

static void kernel_video_flush_safe_present_write(const struct kernel_video_rect *rect) {
    size_t row_off;
    size_t tail_off;
    volatile uint8_t posted;

    if (rect == 0 || g_fb == 0 || rect->w <= 0 || rect->h <= 0) {
        return;
    }

    row_off = (size_t)(rect->y + rect->h - 1) * g_mode.pitch;
    tail_off = row_off + (size_t)(rect->x + rect->w - 1);
    posted = g_fb[tail_off];
    (void)posted;
    __asm__ volatile("" : : : "memory");
}

static void kernel_video_copy_row_rep_movsd(uint8_t *dst, const volatile uint8_t *src, size_t bytes) {
    size_t dwords = bytes / 4u;
    size_t tail = bytes & 3u;

    if (dwords != 0u) {
        __asm__ volatile("cld; rep movsl"
                         : "+D"(dst), "+S"(src), "+c"(dwords)
                         :
                         : "memory");
    }

    for (size_t i = 0; i < tail; ++i) {
        dst[i] = src[i];
    }
}

/*
 * SSE2 stream copy is only reached behind runtime CPUID gates, so the xmm
 * target is enabled per-function here. It must NOT be a whole-TU -msse2:
 * that lets the compiler emit CMOVcc (a P6 instruction) into i586-safe
 * boot-path functions, and real Pentium (no CMOV, no SSE) triple-faults
 * during kernel_video_init -> silent boot hang before the IDT exists.
 */
__attribute__((target("sse2")))
static void kernel_video_copy_row_movntdq(volatile uint8_t *dst_volatile,
                                          const uint8_t *src,
                                          size_t bytes) {
    uint8_t *dst = (uint8_t *)(uintptr_t)dst_volatile;
    size_t blocks64;
    size_t blocks16;

    while (bytes != 0u && (((uintptr_t)dst) & 0x0Fu) != 0u) {
        *dst++ = *src++;
        --bytes;
    }

    blocks64 = bytes / 64u;
    while (blocks64-- != 0u) {
        __asm__ volatile(
            "movdqu   0(%0), %%xmm0\n\t"
            "movdqu  16(%0), %%xmm1\n\t"
            "movdqu  32(%0), %%xmm2\n\t"
            "movdqu  48(%0), %%xmm3\n\t"
            "movntdq %%xmm0,   0(%1)\n\t"
            "movntdq %%xmm1,  16(%1)\n\t"
            "movntdq %%xmm2,  32(%1)\n\t"
            "movntdq %%xmm3,  48(%1)\n\t"
            :
            : "r"(src), "r"(dst)
            : "memory", "xmm0", "xmm1", "xmm2", "xmm3");
        src += 64u;
        dst += 64u;
        bytes -= 64u;
    }

    blocks16 = bytes / 16u;
    while (blocks16-- != 0u) {
        __asm__ volatile(
            "movdqu   0(%0), %%xmm0\n\t"
            "movntdq %%xmm0, 0(%1)\n\t"
            :
            : "r"(src), "r"(dst)
            : "memory", "xmm0");
        src += 16u;
        dst += 16u;
        bytes -= 16u;
    }

    __asm__ volatile("sfence" : : : "memory");
    kernel_video_copy_row_rep_movsd(dst, (const volatile uint8_t *)src, bytes);
}

static void kernel_video_copy_to_lfb(volatile uint8_t *dst, const uint8_t *src, size_t bytes) {
    if (g_present_kind == KERNEL_VIDEO_PRESENT_MOVNTDQ) {
        kernel_video_copy_row_movntdq(dst, src, bytes);
        return;
    }
    if (g_present_kind == KERNEL_VIDEO_PRESENT_REP_MOVSD) {
        kernel_video_copy_row_rep_movsd((uint8_t *)(uintptr_t)dst, src, bytes);
        return;
    }
    for (size_t i = 0; i < bytes; ++i) {
        dst[i] = src[i];
    }
}

static const char *kernel_video_backend_name_internal(enum kernel_video_backend_kind kind) {
    switch (kind) {
    case KERNEL_VIDEO_BACKEND_LEGACY_LFB:
        return "legacy_lfb";
    case KERNEL_VIDEO_BACKEND_FAST_LFB:
        return "fast_lfb";
    default:
        return "none";
    }
}

const char *kernel_video_backend_name(void) {
    if (g_backend_ops != 0 && g_backend_ops->name != 0) {
        return g_backend_ops->name;
    }
    return kernel_video_backend_name_internal(g_backend_kind);
}

void kernel_video_refresh_backend(void) {
    enum kernel_video_backend_kind preferred_backend;

    if (!kernel_video_has_graphics_mode()) {
        return;
    }

    preferred_backend = kernel_video_choose_boot_backend(&g_mode);
    if (preferred_backend == g_backend_kind) {
        return;
    }

    kernel_video_activate_backend(preferred_backend, &g_mode);
    kernel_video_log_state("backend refresh");
}

static int kernel_video_bpp_valid(uint8_t bpp) {
    return bpp == VIDEO_MODE_8BIT ||
           bpp == VIDEO_MODE_16BIT ||
           bpp == VIDEO_MODE_24BIT;
}

static int kernel_video_has_graphics_mode(void) {
    return g_graphics_enabled &&
           g_fb != 0 &&
           g_backbuf != 0 &&
           g_mode.fb_addr >= GRAPHICS_MIN_FB_ADDR &&
           g_mode.width >= GRAPHICS_MIN_WIDTH &&
           g_mode.width <= GRAPHICS_MAX_WIDTH &&
           g_mode.height >= GRAPHICS_MIN_HEIGHT &&
           g_mode.height <= GRAPHICS_MAX_HEIGHT &&
           kernel_video_bpp_valid(g_mode.bpp) &&
           g_mode.pitch >= g_mode.width;
}

static int kernel_video_mode_usable(const struct video_mode *mode) {
    if (mode == 0) {
        return 0;
    }

    return mode->fb_addr >= GRAPHICS_MIN_FB_ADDR &&
           mode->width >= GRAPHICS_MIN_WIDTH &&
           mode->width <= GRAPHICS_MAX_WIDTH &&
           mode->height >= GRAPHICS_MIN_HEIGHT &&
           mode->height <= GRAPHICS_MAX_HEIGHT &&
           kernel_video_bpp_valid(mode->bpp) &&
           mode->pitch >= ((uint32_t)mode->width * (uint32_t)mode->bpp / 8u);
}

static int kernel_video_boot_forces_legacy_video(void) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;

    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return 0;
    }

    return (bootinfo->flags & BOOTINFO_FLAG_FORCE_LEGACY_VIDEO) != 0u;
}

static uint32_t kernel_video_mode_bit(uint16_t width, uint16_t height) {
    if (width == 640u && height == 480u) {
        return VIDEO_RES_640X480;
    }
    if (width == 800u && height == 600u) {
        return VIDEO_RES_800X600;
    }
    if (width == 1024u && height == 768u) {
        return VIDEO_RES_1024X768;
    }
    if (width == 1360u && height == 768u) {
        return VIDEO_RES_1360X768;
    }
    if (width == 1366u && height == 768u) {
        return VIDEO_RES_1366X768;
    }
    if (width == 1920u && height == 1080u) {
        return VIDEO_RES_1920X1080;
    }
    return 0u;
}

static void kernel_video_add_mode_cap(struct video_capabilities *caps,
                                      uint16_t width,
                                      uint16_t height) {
    uint32_t i;

    if (caps == 0 || width == 0u || height == 0u) {
        return;
    }

    caps->supported_modes |= kernel_video_mode_bit(width, height);
    for (i = 0u; i < caps->mode_count; ++i) {
        if (caps->mode_width[i] == width && caps->mode_height[i] == height) {
            return;
        }
    }
    if (caps->mode_count >= VIDEO_MODE_LIST_MAX) {
        return;
    }

    caps->mode_width[caps->mode_count] = width;
    caps->mode_height[caps->mode_count] = height;
    caps->mode_count += 1u;
}

static void kernel_video_load_boot_catalog(struct video_capabilities *caps) {
    const volatile struct bootinfo *bootinfo;
    uint32_t mode_count;

    if (caps == 0) {
        return;
    }

    bootinfo = (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return;
    }

    mode_count = bootinfo->video.mode_count;
    if (mode_count > BOOTINFO_MAX_VESA_MODES) {
        mode_count = BOOTINFO_MAX_VESA_MODES;
    }

    for (uint32_t i = 0u; i < mode_count; ++i) {
        const volatile struct bootinfo_video_mode *mode = &bootinfo->video.modes[i];

        if (mode->width < GRAPHICS_MIN_WIDTH ||
            mode->height < GRAPHICS_MIN_HEIGHT ||
            !kernel_video_bpp_valid(mode->bpp)) {
            continue;
        }
        kernel_video_add_mode_cap(caps, mode->width, mode->height);
    }
}

static int kernel_video_find_catalog_mode(uint32_t width,
                                          uint32_t height,
                                          uint8_t *index_out,
                                          uint16_t *mode_out) {
    const volatile struct bootinfo *bootinfo;
    uint32_t mode_count;

    if (index_out != 0) {
        *index_out = BOOTINFO_VIDEO_INDEX_NONE;
    }
    if (mode_out != 0) {
        *mode_out = 0u;
    }

    bootinfo = (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    mode_count = bootinfo->video.mode_count;
    if (mode_count > BOOTINFO_MAX_VESA_MODES) {
        mode_count = BOOTINFO_MAX_VESA_MODES;
    }

    for (uint32_t i = 0u; i < mode_count; ++i) {
        const volatile struct bootinfo_video_mode *mode = &bootinfo->video.modes[i];

        if ((uint32_t)mode->width != width ||
            (uint32_t)mode->height != height ||
            !kernel_video_bpp_valid(mode->bpp) ||
            mode->mode == 0u) {
            continue;
        }

        if (index_out != 0) {
            *index_out = (uint8_t)i;
        }
        if (mode_out != 0) {
            *mode_out = mode->mode;
        }
        return 0;
    }

    return -1;
}

static int kernel_video_find_alternate_catalog_mode(uint32_t *width_out,
                                                    uint32_t *height_out) {
    const volatile struct bootinfo *bootinfo;
    uint32_t mode_count;

    if (width_out != 0) {
        *width_out = 0u;
    }
    if (height_out != 0) {
        *height_out = 0u;
    }

    bootinfo = (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    mode_count = bootinfo->video.mode_count;
    if (mode_count > BOOTINFO_MAX_VESA_MODES) {
        mode_count = BOOTINFO_MAX_VESA_MODES;
    }

    for (uint32_t i = 0u; i < mode_count; ++i) {
        const volatile struct bootinfo_video_mode *mode = &bootinfo->video.modes[i];

        if (mode->mode == 0u ||
            !kernel_video_bpp_valid(mode->bpp) ||
            mode->width < GRAPHICS_MIN_WIDTH ||
            mode->height < GRAPHICS_MIN_HEIGHT) {
            continue;
        }
        if ((uint32_t)mode->width == g_mode.width &&
            (uint32_t)mode->height == g_mode.height) {
            continue;
        }

        if (width_out != 0) {
            *width_out = mode->width;
        }
        if (height_out != 0) {
            *height_out = mode->height;
        }
        return 0;
    }

    return -1;
}

static void kernel_video_mark_catalog_active(uint8_t index) {
    volatile struct bootinfo *bootinfo;

    if (index == BOOTINFO_VIDEO_INDEX_NONE) {
        return;
    }

    bootinfo = (volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return;
    }
    if ((uint32_t)index >= bootinfo->video.mode_count) {
        return;
    }

    bootinfo->video.active_index = index;
    bootinfo->video.selected_index = index;
}

static int kernel_video_backbuffer_is_lfb(void) {
    return g_backbuf != 0 && g_fb != 0 &&
           (uintptr_t)g_backbuf == (uintptr_t)g_fb;
}

static int kernel_video_backend_supports_shadow(void) {
    return g_backend_ops != 0 && g_backend_ops->supports_shadow;
}

static int kernel_video_heap_ready(void) {
    return kernel_heap_end() > kernel_heap_start();
}

static size_t kernel_video_frame_bytes(void) {
    if (!kernel_video_has_graphics_mode()) {
        return 0u;
    }
    return (size_t)g_mode.pitch * (size_t)g_mode.height;
}

static size_t kernel_video_blit8_source_pitch(const uint8_t *src, int src_w, int src_h) {
    uintptr_t backbuf_addr;
    uintptr_t src_addr;
    size_t frame_bytes;
    size_t offset;
    size_t start_x;
    size_t start_y;

    if (src == 0 || src_w <= 0 || src_h <= 0) {
        return 0u;
    }

    if (!kernel_video_has_graphics_mode() || g_backbuf == 0) {
        return (size_t)src_w;
    }

    backbuf_addr = (uintptr_t)g_backbuf;
    src_addr = (uintptr_t)src;
    if (src_addr < backbuf_addr) {
        return (size_t)src_w;
    }

    frame_bytes = kernel_video_frame_bytes();
    offset = (size_t)(src_addr - backbuf_addr);
    if (frame_bytes == 0u || offset >= frame_bytes) {
        return (size_t)src_w;
    }

    start_x = offset % g_mode.pitch;
    start_y = offset / g_mode.pitch;
    if (start_x + (size_t)src_w > g_mode.pitch ||
        start_y + (size_t)src_h > g_mode.height) {
        return (size_t)src_w;
    }

    return (size_t)g_mode.pitch;
}

int kernel_video_pack_blit8_source(const uint8_t *src, int src_w, int src_h,
                                   uint8_t *dst, size_t dst_size) {
    size_t src_pitch;
    size_t row_bytes;
    size_t packed_bytes;

    if (src == 0 || dst == 0 || src_w <= 0 || src_h <= 0) {
        return -1;
    }

    row_bytes = (size_t)src_w;
    packed_bytes = row_bytes * (size_t)src_h;
    if (packed_bytes == 0u || dst_size < packed_bytes) {
        return -1;
    }

    src_pitch = kernel_video_blit8_source_pitch(src, src_w, src_h);
    if (src_pitch == 0u) {
        return -1;
    }

    if (src_pitch == row_bytes) {
        memcpy(dst, src, packed_bytes);
        return 0;
    }

    for (int y = 0; y < src_h; ++y) {
        memcpy(dst + ((size_t)y * row_bytes),
               src + ((size_t)y * src_pitch),
               row_bytes);
    }
    return 0;
}

static void kernel_video_copy_lfb_to_buffer(uint8_t *dst) {
    if (dst == 0 || g_fb == 0) {
        return;
    }

    for (uint32_t y = 0u; y < g_mode.height; ++y) {
        size_t row_off = (size_t)y * g_mode.pitch;

        kernel_video_copy_row_byte(dst + row_off, g_fb + row_off, g_mode.pitch);
    }
}

static void kernel_video_bind_graphics_mode(const struct video_mode *mode) {
    size_t frame_bytes;

    if (!kernel_video_mode_usable(mode)) {
        return;
    }

    g_mode = *mode;
    g_fb = (volatile uint8_t *)(uintptr_t)mode->fb_addr;
    frame_bytes = (size_t)mode->pitch * (size_t)mode->height;
    if (g_shadow_backbuf != 0 && g_shadow_backbuf_capacity >= frame_bytes) {
        g_backbuf = g_shadow_backbuf;
    } else {
        g_backbuf = (uint8_t *)(uintptr_t)mode->fb_addr;
    }
    g_graphics_enabled = 1;
    g_backbuf_alloc_failed = 0;
}

static int kernel_video_fast_backend_usable(const struct video_mode *mode) {
    size_t frame_bytes;
    size_t heap_free;

    if (!kernel_video_mode_usable(mode) || !kernel_video_heap_ready()) {
        return 0;
    }

    frame_bytes = (size_t)mode->pitch * (size_t)mode->height;
    heap_free = kernel_heap_free();
    if (frame_bytes == 0u ||
        frame_bytes > VIDEO_BACKBUFFER_MAX_BYTES ||
        heap_free <= frame_bytes ||
        (heap_free - frame_bytes) < VIDEO_BACKBUFFER_HEAP_RESERVE) {
        return 0;
    }

    return 1;
}

static int kernel_video_backend_prefers_safe_present_copy(void) {
    enum kernel_drm_backend_kind active_kind = kernel_drm_active_backend_kind();
    enum kernel_drm_backend_kind detected_kind = kernel_drm_detected_backend_kind();
    enum kernel_drm_backend_kind effective_kind =
        active_kind != KERNEL_DRM_BACKEND_NONE ? active_kind : detected_kind;

    return effective_kind == KERNEL_DRM_BACKEND_BGA ||
           effective_kind == KERNEL_DRM_BACKEND_NONE ||
           effective_kind == KERNEL_DRM_BACKEND_UNKNOWN;
}

static enum kernel_video_backend_kind kernel_video_choose_boot_backend(const struct video_mode *mode) {
    if (!kernel_video_fast_backend_usable(mode)) {
        return KERNEL_VIDEO_BACKEND_LEGACY_LFB;
    }
    return KERNEL_VIDEO_BACKEND_FAST_LFB;
}

static void kernel_video_log_state(const char *reason) {
    size_t frame_bytes;
    size_t backbuffer_bytes;

    frame_bytes = kernel_video_frame_bytes();
    backbuffer_bytes = (g_backbuf != 0 && !kernel_video_backbuffer_is_lfb()) ? frame_bytes : 0u;
    g_video_perf.frame_bytes = frame_bytes;
    g_video_perf.backbuffer_bytes = backbuffer_bytes;

    kernel_debug_printf("video: %s backend=%s mode=%dx%dx%d pitch=%d frame=%d backbuf=%d heap=%d pat=%d sse2=%d\n",
                        reason != 0 ? reason : "state",
                        kernel_video_backend_name(),
                        (int)g_mode.width,
                        (int)g_mode.height,
                        (int)g_mode.bpp,
                        (int)g_mode.pitch,
                        (int)frame_bytes,
                        (int)backbuffer_bytes,
                        (int)kernel_heap_free(),
                        (int)g_video_perf.cpu_has_pat,
                        (int)g_video_perf.cpu_has_sse2);
}

static const char *kernel_video_present_kind_name(enum kernel_video_present_kind kind) {
    switch (kind) {
    case KERNEL_VIDEO_PRESENT_REP_MOVSD:
        return "rep_movsd";
    case KERNEL_VIDEO_PRESENT_MOVNTDQ:
        return "movntdq";
    case KERNEL_VIDEO_PRESENT_BYTE_LOOP:
    default:
        return "byte_loop";
    }
}

static void kernel_video_log_handoff(const char *stage, const char *source) {
    const char *native_name = kernel_drm_active_backend_name();

    if (native_name == 0 || native_name[0] == '\0') {
        native_name = "none";
    }

    kernel_debug_printf("video: handoff stage=%s source=%s backend=%s native=%s present=%s mode=%dx%dx%d pitch=%d fb=%x\n",
                        stage != 0 ? stage : "unknown",
                        source != 0 ? source : "unknown",
                        kernel_video_backend_name(),
                        native_name,
                        kernel_video_present_kind_name(g_present_kind),
                        (int)g_mode.width,
                        (int)g_mode.height,
                        (int)g_mode.bpp,
                        (int)g_mode.pitch,
                        g_mode.fb_addr);
}

static int kernel_video_rect_sanitize(struct kernel_video_rect *rect) {
    int x1;
    int y1;

    if (rect == 0 || !kernel_video_has_graphics_mode() || rect->w <= 0 || rect->h <= 0) {
        return 0;
    }

    x1 = rect->x + rect->w;
    y1 = rect->y + rect->h;
    if (rect->x < 0) {
        rect->x = 0;
    }
    if (rect->y < 0) {
        rect->y = 0;
    }
    if (x1 > (int)g_mode.width) {
        x1 = (int)g_mode.width;
    }
    if (y1 > (int)g_mode.height) {
        y1 = (int)g_mode.height;
    }
    rect->w = x1 - rect->x;
    rect->h = y1 - rect->y;
    return rect->w > 0 && rect->h > 0;
}

static void kernel_video_reset_dirty_state(void) {
    g_dirty_rect.x = 0;
    g_dirty_rect.y = 0;
    g_dirty_rect.w = 0;
    g_dirty_rect.h = 0;
    g_dirty_pending = 0;
    g_dirty_full = 0;
}

static void kernel_video_mark_full_dirty(void) {
    if (!kernel_video_has_graphics_mode()) {
        return;
    }
    g_dirty_rect.x = 0;
    g_dirty_rect.y = 0;
    g_dirty_rect.w = (int)g_mode.width;
    g_dirty_rect.h = (int)g_mode.height;
    g_dirty_pending = 1;
    g_dirty_full = 1;
}

void kernel_video_mark_dirty(int x, int y, int w, int h) {
    struct kernel_video_rect rect;
    int x0;
    int y0;
    int x1;
    int y1;

    if (!kernel_video_has_graphics_mode()) {
        return;
    }

    if (w <= 0 || h <= 0) {
        return;
    }

    if (g_backend_kind == KERNEL_VIDEO_BACKEND_LEGACY_LFB || kernel_video_backbuffer_is_lfb()) {
        kernel_video_mark_full_dirty();
        return;
    }

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    if (!kernel_video_rect_sanitize(&rect)) {
        return;
    }

    if (!g_dirty_pending) {
        g_dirty_rect = rect;
        g_dirty_pending = 1;
        return;
    }

    x0 = g_dirty_rect.x < rect.x ? g_dirty_rect.x : rect.x;
    y0 = g_dirty_rect.y < rect.y ? g_dirty_rect.y : rect.y;
    x1 = (g_dirty_rect.x + g_dirty_rect.w) > (rect.x + rect.w)
             ? (g_dirty_rect.x + g_dirty_rect.w)
             : (rect.x + rect.w);
    y1 = (g_dirty_rect.y + g_dirty_rect.h) > (rect.y + rect.h)
             ? (g_dirty_rect.y + g_dirty_rect.h)
             : (rect.y + rect.h);

    g_dirty_rect.x = x0;
    g_dirty_rect.y = y0;
    g_dirty_rect.w = x1 - x0;
    g_dirty_rect.h = y1 - y0;
    if (g_dirty_rect.w >= (int)g_mode.width && g_dirty_rect.h >= (int)g_mode.height) {
        kernel_video_mark_full_dirty();
    }
}

static uint32_t kernel_video_measure_fill_time(void) {
    uint32_t start;
    uint32_t end;

    if (!kernel_video_has_graphics_mode() || g_backbuf == 0) {
        return 0u;
    }

    start = kernel_timer_get_ticks();
    for (uint32_t y = 0u; y < g_mode.height; ++y) {
        uint8_t *row = g_backbuf + ((size_t)y * g_mode.pitch);
        for (uint32_t x = 0u; x < g_mode.width; ++x) {
            row[x] = 0u;
        }
    }
    end = kernel_timer_get_ticks();
    return end - start;
}

static uint32_t kernel_video_measure_present_time(void) {
    uint32_t start;
    uint32_t end;

    if (!kernel_video_has_graphics_mode() || g_fb == 0 || kernel_video_backbuffer_is_lfb()) {
        return 0u;
    }

    start = kernel_timer_get_ticks();
    kernel_video_present_full_fast_lfb();
    end = kernel_timer_get_ticks();
    return end - start;
}

static uint32_t kernel_video_measure_frame_time(void) {
    uint32_t start;
    uint32_t end;

    start = kernel_timer_get_ticks();
    (void)kernel_video_measure_fill_time();
    (void)kernel_video_measure_present_time();
    end = kernel_timer_get_ticks();
    return end - start;
}

static uint32_t kernel_video_measure_fullscreen_direct_time(void) {
    uint32_t start;
    uint32_t end;

    if (!kernel_video_has_graphics_mode() ||
        g_fb == 0 ||
        g_backbuf == 0 ||
        kernel_video_backbuffer_is_lfb()) {
        return 0u;
    }

    start = kernel_timer_get_ticks();
    kernel_video_blit8_stretch_to_target(g_backbuf,
                                         (int)g_mode.width,
                                         (int)g_mode.height,
                                         0,
                                         0,
                                         (int)g_mode.width,
                                         (int)g_mode.height,
                                         (uint8_t *)(uintptr_t)g_fb,
                                         g_mode.pitch,
                                         g_mode.width,
                                         g_mode.height);
    end = kernel_timer_get_ticks();
    kernel_video_resync_shadow_backbuffer();
    return end - start;
}

static uint32_t kernel_video_measure_fullscreen_blit_present_time(void) {
    uint32_t start;
    uint32_t end;

    if (!kernel_video_has_graphics_mode() ||
        g_fb == 0 ||
        g_backbuf == 0 ||
        g_mode.width == 0u ||
        g_mode.height == 0u ||
        kernel_video_backbuffer_is_lfb()) {
        return 0u;
    }

    start = kernel_timer_get_ticks();
    kernel_gfx_blit8_present(g_backbuf,
                             (int)g_mode.width,
                             (int)g_mode.height,
                             0,
                             0,
                             1);
    end = kernel_timer_get_ticks();
    kernel_video_resync_shadow_backbuffer();
    return end - start;
}

static uint32_t kernel_video_measure_microkernel_frame_time(void) {
    uint32_t start;
    uint32_t end;

    if (!kernel_video_has_graphics_mode() ||
        g_backbuf == 0 ||
        g_mode.width == 0u ||
        g_mode.height == 0u) {
        return 0u;
    }

    start = kernel_timer_get_ticks();
    if (mk_video_service_blit8_present(g_backbuf,
                                       (int)g_mode.width,
                                       (int)g_mode.height,
                                       0,
                                       0,
                                       1) != 0) {
        return 0u;
    }
    end = kernel_timer_get_ticks();

    if (!kernel_video_backbuffer_is_lfb()) {
        kernel_video_resync_shadow_backbuffer();
    }
    return end - start;
}

static uint32_t kernel_video_measure_microkernel_flip_time(void) {
    uint32_t start;
    uint32_t end;

    if (!kernel_video_has_graphics_mode()) {
        return 0u;
    }

    start = kernel_timer_get_ticks();
    if (mk_video_service_flip_mode(VIDEO_PRESENT_FULL) != 0) {
        return 0u;
    }
    end = kernel_timer_get_ticks();
    return end - start;
}

static uint32_t kernel_video_measure_microkernel_blit_time(void) {
    uint32_t start;
    uint32_t end;
    int blit_w;
    int blit_h;

    if (!kernel_video_has_graphics_mode() ||
        g_backbuf == 0 ||
        g_mode.width == 0u ||
        g_mode.height == 0u) {
        return 0u;
    }

    blit_w = (int)(g_mode.width < 64u ? g_mode.width : 64u);
    blit_h = (int)(g_mode.height < 64u ? g_mode.height : 64u);
    if (blit_w <= 0 || blit_h <= 0) {
        return 0u;
    }

    start = kernel_timer_get_ticks();
    if (mk_video_service_blit8(g_backbuf, blit_w, blit_h, 0, 0, 1) != 0) {
        return 0u;
    }
    end = kernel_timer_get_ticks();
    return end - start;
}

static uint32_t kernel_video_measure_microkernel_stretch_time(void) {
    uint32_t start;
    uint32_t end;
    int blit_w;
    int blit_h;

    if (!kernel_video_has_graphics_mode() ||
        g_backbuf == 0 ||
        g_mode.width == 0u ||
        g_mode.height == 0u) {
        return 0u;
    }

    blit_w = (int)(g_mode.width < 64u ? g_mode.width : 64u);
    blit_h = (int)(g_mode.height < 64u ? g_mode.height : 64u);
    if (blit_w <= 0 || blit_h <= 0) {
        return 0u;
    }

    start = kernel_timer_get_ticks();
    if (mk_video_service_blit8_stretch(g_backbuf,
                                       blit_w,
                                       blit_h,
                                       0,
                                       0,
                                       blit_w,
                                       blit_h) != 0) {
        return 0u;
    }
    end = kernel_timer_get_ticks();
    return end - start;
}

static void kernel_video_try_alloc_backbuffer(void) {
    uint8_t *shadow;
    size_t frame_bytes;
    size_t heap_free;

    if (!kernel_video_has_graphics_mode() ||
        !kernel_video_backend_supports_shadow() ||
        !kernel_video_backbuffer_is_lfb() ||
        g_backbuf_alloc_failed ||
        !kernel_video_heap_ready()) {
        return;
    }

    frame_bytes = kernel_video_frame_bytes();
    if (frame_bytes == 0u) {
        return;
    }

    heap_free = kernel_heap_free();
    if (frame_bytes > VIDEO_BACKBUFFER_MAX_BYTES ||
        heap_free <= frame_bytes ||
        (heap_free - frame_bytes) < VIDEO_BACKBUFFER_HEAP_RESERVE) {
        return;
    }

    if (g_shadow_backbuf != 0 && g_shadow_backbuf_capacity >= frame_bytes) {
        kernel_video_copy_lfb_to_buffer(g_shadow_backbuf);
        g_backbuf = g_shadow_backbuf;
        kernel_video_mark_full_dirty();
        kernel_video_select_present_kind();
        return;
    }

    shadow = (uint8_t *)kernel_malloc(frame_bytes);
    if (shadow == 0) {
        g_backbuf_alloc_failed = 1;
        kernel_debug_puts("video: shadow backbuffer alloc failed, staying direct-lfb\n");
        return;
    }

    kernel_video_copy_lfb_to_buffer(shadow);
    g_shadow_backbuf = shadow;
    g_shadow_backbuf_capacity = frame_bytes;
    g_backbuf = shadow;
    kernel_video_mark_full_dirty();
    kernel_video_select_present_kind();
    kernel_debug_printf("video: shadow backbuffer enabled bytes=%d\n",
                        (int)frame_bytes);
}

static void kernel_video_program_palette(void) {
    if (!g_palette_ready || !kernel_video_has_graphics_mode()) {
        return;
    }

    outb(VGA_PEL_MASK, 0xFFu);
    outb(VGA_DAC_WRITE_INDEX, 0u);
    for (int i = 0; i < 256 * 3; ++i) {
        outb(VGA_DAC_DATA, (uint8_t)(g_palette[i] >> 2));
    }
}

static void kernel_video_load_default_palette(void) {
    static const uint8_t ega16[16][3] = {
        {0x00u, 0x00u, 0x00u},
        {0x00u, 0x00u, 0xAAu},
        {0x00u, 0xAAu, 0x00u},
        {0x00u, 0xAAu, 0xAAu},
        {0xAAu, 0x00u, 0x00u},
        {0xAAu, 0x00u, 0xAAu},
        {0xAAu, 0x55u, 0x00u},
        {0xAAu, 0xAAu, 0xAAu},
        {0x55u, 0x55u, 0x55u},
        {0x55u, 0x55u, 0xFFu},
        {0x55u, 0xFFu, 0x55u},
        {0x55u, 0xFFu, 0xFFu},
        {0xFFu, 0x55u, 0x55u},
        {0xFFu, 0x55u, 0xFFu},
        {0xFFu, 0xFFu, 0x55u},
        {0xFFu, 0xFFu, 0xFFu}
    };

    for (int i = 0; i < 16; ++i) {
        g_palette[i * 3 + 0] = ega16[i][0];
        g_palette[i * 3 + 1] = ega16[i][1];
        g_palette[i * 3 + 2] = ega16[i][2];
    }
    for (int i = 16; i < 256; ++i) {
        g_palette[i * 3 + 0] = (uint8_t)((((unsigned)i >> 5) & 0x07u) * 255u / 7u);
        g_palette[i * 3 + 1] = (uint8_t)((((unsigned)i >> 2) & 0x07u) * 255u / 7u);
        g_palette[i * 3 + 2] = (uint8_t)(((unsigned)i & 0x03u) * 255u / 3u);
    }
    g_palette_ready = 1;
    kernel_video_program_palette();
}

static void kernel_video_enter_graphics(void) {
    kernel_video_try_alloc_backbuffer();
}

static void kernel_video_bind_legacy_lfb(const struct video_mode *mode) {
    g_mode = *mode;
    g_fb = (volatile uint8_t *)(uintptr_t)mode->fb_addr;
    g_backbuf = (uint8_t *)(uintptr_t)mode->fb_addr;
    g_graphics_enabled = 1;
    g_backbuf_alloc_failed = 1;
    g_direct_fullscreen_present_dirty = 0;
    kernel_video_mark_full_dirty();
}

static void kernel_video_bind_fast_lfb(const struct video_mode *mode) {
    kernel_video_bind_graphics_mode(mode);
    g_direct_fullscreen_present_dirty = 0;
    kernel_video_mark_full_dirty();
}

static void kernel_video_present_rect_fast_lfb(const struct kernel_video_rect *rect) {
    struct kernel_video_rect clipped;

    if (rect == 0 || g_fb == 0 || g_backbuf == 0 || kernel_video_backbuffer_is_lfb()) {
        return;
    }

    clipped = *rect;
    if (!kernel_video_rect_sanitize(&clipped)) {
        return;
    }

    for (int y = clipped.y; y < (clipped.y + clipped.h); ++y) {
        size_t row_off = ((size_t)y * g_mode.pitch) + (size_t)clipped.x;

        if (kernel_video_backend_prefers_safe_present_copy()) {
            /*
             * Bochs/BGA in QEMU is more reliable with direct volatile writes
             * into the mapped framebuffer plus a readback to drain posting.
             */
            kernel_video_copy_row_safe(g_fb + row_off, g_backbuf + row_off, (size_t)clipped.w);
            continue;
        }
        kernel_video_copy_to_lfb(g_fb + row_off, g_backbuf + row_off, (size_t)clipped.w);
    }
    if (kernel_video_backend_prefers_safe_present_copy()) {
        kernel_video_flush_safe_present_write(&clipped);
    }
}

static void kernel_video_present_full_fast_lfb(void) {
    struct kernel_video_rect rect;

    if (g_fb == 0 || g_backbuf == 0 || kernel_video_backbuffer_is_lfb()) {
        return;
    }

    rect.x = 0;
    rect.y = 0;
    rect.w = (int)g_mode.width;
    rect.h = (int)g_mode.height;
    kernel_video_present_rect_fast_lfb(&rect);
}

static void kernel_video_present_full_legacy_lfb(void) {
    if (g_fb == 0 || g_backbuf == 0 || kernel_video_backbuffer_is_lfb()) {
        return;
    }
    size_t size = (size_t)g_mode.height * (size_t)g_mode.pitch;
    if (size > 0u) {
        memcpy((void *)g_fb, g_backbuf, size);
    }
}

static void kernel_video_present_rect_legacy_lfb(const struct kernel_video_rect *rect) {
    if (g_fb == 0 || g_backbuf == 0 || kernel_video_backbuffer_is_lfb() || rect == 0) {
        return;
    }
    int y0 = rect->y;
    int y1 = rect->y + rect->h;
    if (y0 < 0) y0 = 0;
    if (y1 > (int)g_mode.height) y1 = (int)g_mode.height;
    for (int y = y0; y < y1; y++) {
        memcpy((void *)&g_fb[y * g_mode.pitch],
               &g_backbuf[y * g_mode.pitch],
               (size_t)g_mode.pitch);
    }
}

static const struct kernel_video_backend_ops *kernel_video_backend_ops_for_kind(
    enum kernel_video_backend_kind kind) {
    switch (kind) {
    case KERNEL_VIDEO_BACKEND_LEGACY_LFB:
        return &g_backend_legacy_lfb_ops;
    case KERNEL_VIDEO_BACKEND_FAST_LFB:
        return &g_backend_fast_lfb_ops;
    default:
        return 0;
    }
}

static void kernel_video_activate_backend(enum kernel_video_backend_kind kind,
                                          const struct video_mode *mode) {
    const struct kernel_video_backend_ops *ops;

    if (!kernel_video_mode_usable(mode)) {
        return;
    }

    ops = kernel_video_backend_ops_for_kind(kind);
    if (ops == 0 || ops->activate == 0) {
        return;
    }

    g_backend_kind = kind;
    g_backend_ops = ops;
    kernel_video_reset_dirty_state();
    ops->activate(mode);
    if (kernel_video_has_graphics_mode()) {
        (void)paging_set_framebuffer_wc((uintptr_t)g_mode.fb_addr, kernel_video_frame_bytes());
    }
    kernel_video_select_present_kind();
}

static void kernel_video_present_rect_internal(const struct kernel_video_rect *rect) {
    if (g_backend_ops == 0 || g_backend_ops->present_rect == 0) {
        return;
    }
    g_backend_ops->present_rect(rect);
}

static void __attribute__((unused)) kernel_video_record_benchmarks(void) {
    const struct kernel_pci_device_info *active_pci = kernel_drm_active_pci_info();
    const struct kernel_pci_device_info *detected_pci = kernel_drm_detected_pci_info();

    g_video_perf.active_width = g_mode.width;
    g_video_perf.active_height = g_mode.height;
    g_video_perf.active_pitch = g_mode.pitch;
    g_video_perf.fill_ticks = kernel_video_measure_fill_time();
    g_video_perf.present_ticks = kernel_video_measure_present_time();
    g_video_perf.frame_ticks = kernel_video_measure_frame_time();
    g_video_perf.fullscreen_direct_ticks = kernel_video_measure_fullscreen_direct_time();
    g_video_perf.fullscreen_blit_present_ticks = kernel_video_measure_fullscreen_blit_present_time();
    g_video_perf.microkernel_frame_ticks = kernel_video_measure_microkernel_frame_time();
    g_video_perf.microkernel_flip_ticks = kernel_video_measure_microkernel_flip_time();
    g_video_perf.microkernel_blit_ticks = kernel_video_measure_microkernel_blit_time();
    g_video_perf.microkernel_stretch_ticks = kernel_video_measure_microkernel_stretch_time();
    g_video_perf.wc_enabled = paging_pat_wc_enabled() ? 1u : 0u;
    g_video_perf.backend_kind = (uint32_t)g_backend_kind;
    g_video_perf.present_copy_kind = (uint32_t)g_present_kind;
    g_video_perf.present_copy_override_kind = g_present_copy_override_kind;
    g_video_perf.native_backend_kind = (uint32_t)kernel_drm_active_backend_kind();
    g_video_perf.detected_native_backend_kind = (uint32_t)kernel_drm_detected_backend_kind();
    g_video_perf.gpu_vendor_id = active_pci != 0 ? active_pci->vendor_id : 0u;
    g_video_perf.gpu_device_id = active_pci != 0 ? active_pci->device_id : 0u;
    g_video_perf.gpu_revision = active_pci != 0 ? active_pci->revision : 0u;
    g_video_perf.detected_gpu_vendor_id = detected_pci != 0 ? detected_pci->vendor_id : 0u;
    g_video_perf.detected_gpu_device_id = detected_pci != 0 ? detected_pci->device_id : 0u;
    g_video_perf.detected_gpu_revision = detected_pci != 0 ? detected_pci->revision : 0u;
    kernel_debug_printf("video: bench fill=%d present=%d frame=%d fullscreen_direct=%d blit_present=%d mk_frame=%d mk_flip=%d mk_blit=%d mk_stretch=%d present_kind=%d\n",
                        (int)g_video_perf.fill_ticks,
                        (int)g_video_perf.present_ticks,
                        (int)g_video_perf.frame_ticks,
                        (int)g_video_perf.fullscreen_direct_ticks,
                        (int)g_video_perf.fullscreen_blit_present_ticks,
                        (int)g_video_perf.microkernel_frame_ticks,
                        (int)g_video_perf.microkernel_flip_ticks,
                        (int)g_video_perf.microkernel_blit_ticks,
                        (int)g_video_perf.microkernel_stretch_ticks,
                        (int)g_present_kind);
}

static void kernel_video_run_drm_recovery_selftest(void) {
    struct video_mode original_mode;
    struct video_mode test_mode = {0};
    struct video_mode restored_mode = {0};
    uint32_t original_width;
    uint32_t original_height;
    uint32_t test_width;
    uint32_t test_height;
    uint8_t original_mode_index;
    uint8_t test_mode_index;
    uint16_t original_bios_mode;
    uint16_t bios_mode;
    uint32_t irq_flags;
    enum kernel_video_backend_kind active_backend;

    if (VIDEO_DRM_TEST_FORCE_HANDOFF_FAIL == 0 || !kernel_video_has_graphics_mode()) {
        return;
    }
    if (kernel_video_boot_forces_legacy_video()) {
        kernel_debug_puts("video: drm recovery selftest skipped because boot forced legacy video\n");
        return;
    }

    original_mode = g_mode;
    original_width = g_mode.width;
    original_height = g_mode.height;
    if (kernel_video_find_alternate_catalog_mode(&test_width, &test_height) != 0) {
        kernel_debug_puts("video: drm recovery selftest skipped because no alternate catalog mode was found\n");
        return;
    }
    if (kernel_video_find_catalog_mode(original_width,
                                       original_height,
                                       &original_mode_index,
                                       &original_bios_mode) != 0 ||
        kernel_video_find_catalog_mode(test_width,
                                       test_height,
                                       &test_mode_index,
                                       &bios_mode) != 0) {
        kernel_debug_puts("video: drm recovery selftest skipped because the alternate mode is not addressable in the boot catalog\n");
        return;
    }

    kernel_debug_printf("video: drm recovery selftest begin from %dx%d to %dx%d\n",
                        (int)original_width,
                        (int)original_height,
                        (int)test_width,
                        (int)test_height);

    irq_flags = kernel_irq_save();
    kernel_keyboard_prepare_for_graphics();
    kernel_mouse_prepare_for_graphics();
    active_backend = g_backend_kind;

    if (kernel_drm_try_set_mode(test_width, test_height, bios_mode, &test_mode) != 0) {
        kernel_irq_restore(irq_flags);
        kernel_debug_printf("video: drm recovery selftest failed while switching to %dx%d\n",
                            (int)test_width,
                            (int)test_height);
        return;
    }

    if (((test_mode.width != (uint16_t)test_width ||
          test_mode.height != (uint16_t)test_height) &&
         vesa_init(&test_mode) != 0) ||
        !kernel_video_mode_usable(&test_mode)) {
        (void)kernel_drm_revert_last_modeset();
        kernel_irq_restore(irq_flags);
        kernel_debug_printf("video: drm recovery selftest failed while reading back %dx%d\n",
                            (int)test_width,
                            (int)test_height);
        return;
    }

    kernel_debug_printf("video: forcing native handoff failure test for %dx%d after modeset success\n",
                        (int)test_width,
                        (int)test_height);

    if (kernel_drm_revert_last_modeset() != 0) {
        kernel_irq_restore(irq_flags);
        kernel_debug_printf("video: drm recovery selftest failed while reverting %dx%d\n",
                            (int)test_width,
                            (int)test_height);
        return;
    }

    kernel_debug_printf("video: reverted native modeset after handoff failure for %dx%d\n",
                        (int)test_width,
                        (int)test_height);

    if (vesa_init(&restored_mode) != 0 ||
        !kernel_video_mode_usable(&restored_mode) ||
        restored_mode.width != original_mode.width ||
        restored_mode.height != original_mode.height ||
        restored_mode.pitch != original_mode.pitch ||
        restored_mode.bpp != original_mode.bpp ||
        restored_mode.fb_addr != original_mode.fb_addr) {
        kernel_irq_restore(irq_flags);
        kernel_debug_printf("video: drm recovery selftest failed while restoring %dx%d\n",
                            (int)original_width,
                            (int)original_height);
        return;
    }

    (void)original_bios_mode;
    (void)test_mode_index;
    kernel_video_mark_catalog_active(original_mode_index);
    kernel_video_activate_backend(active_backend, &restored_mode);
    kernel_video_load_default_palette();
    kernel_video_present_full();
    kernel_mouse_sync_to_video();
    kernel_irq_restore(irq_flags);
    kernel_debug_printf("video: drm recovery selftest restored %dx%d\n",
                        (int)g_mode.width,
                        (int)g_mode.height);
}

void kernel_video_init(void) {
    struct video_mode boot_mode;
    enum kernel_video_backend_kind boot_backend;

    if (g_video_initialized) {
        return;
    }

    kernel_video_detect_cpu_features();
    kernel_drm_log_candidates();
    if (vesa_init(&boot_mode) == 0 && kernel_video_mode_usable(&boot_mode)) {
        g_video_perf.heap_free_before = kernel_heap_free();
        boot_backend = kernel_video_choose_boot_backend(&boot_mode);
        kernel_video_activate_backend(boot_backend, &boot_mode);
        kernel_video_load_default_palette();
        kernel_video_clear(0u);
        kernel_video_flip();
        g_video_perf.heap_free_after = kernel_heap_free();
        kernel_video_log_state("boot init");
        kernel_video_log_handoff("boot", "boot");
        kernel_video_run_drm_recovery_selftest();
        g_video_initialized = 1;
        register_driver("video", "display", (void (*)(void))0);
        return;
    }

    /* stay in BIOS text mode until a graphical syscall is used */
    g_mode.fb_addr = 0xB8000;
    g_mode.width = 80;
    g_mode.height = 25;
    g_mode.pitch = 160; /* 80 cols * 2 bytes */
    g_mode.bpp = 16;
    g_fb = (volatile uint8_t *)g_mode.fb_addr;
    g_backbuf = 0;
    g_backend_kind = KERNEL_VIDEO_BACKEND_NONE;
    g_video_initialized = 1;
    kernel_debug_puts("video: VESA 640x480x8 unavailable, staying in text mode\n");
    register_driver("video", "display", (void (*)(void))0);
}

int kernel_video_ioctl(int cmd, void *arg, size_t arg_size) {
    (void)arg_size;

    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (st == 0) return -1;
        st->present = kernel_video_has_graphics_mode() ? 1u : 0u;
        st->active = g_graphics_enabled ? 1u : 0u;
        st->error = g_backbuf_alloc_failed ? 1u : 0u;
        st->link_up = 1u;
        st->mtu = 0u;
        return 0;
    }
    case VID_IOCTL_GET_MODE: {
        struct vid_mode_info *info = (struct vid_mode_info *)arg;
        if (info == 0) return -1;
        info->width = g_mode.width;
        info->height = g_mode.height;
        info->pitch = g_mode.pitch;
        info->bpp = g_mode.bpp;
        info->fb_addr = g_mode.fb_addr;
        return 0;
    }
    case VID_IOCTL_GET_BACKEND: {
        struct vid_backend_info *info = (struct vid_backend_info *)arg;
        const char *name = kernel_video_backend_name();
        const char *drm_name = kernel_drm_active_backend_name();
        if (info == 0) return -1;
        info->active = g_graphics_enabled ? 1u : 0u;
        info->present = kernel_video_has_graphics_mode() ? 1u : 0u;
        info->drm_active = kernel_drm_active_backend_kind() != KERNEL_DRM_BACKEND_NONE ? 1u : 0u;
        if (name != 0) {
            size_t len = strlen(name);
            if (len >= sizeof(info->name)) len = sizeof(info->name) - 1u;
            memcpy(info->name, name, len);
            info->name[len] = '\0';
        } else {
            info->name[0] = '\0';
        }
        if (drm_name != 0) {
            size_t len = strlen(drm_name);
            if (len >= sizeof(info->drm_backend)) len = sizeof(info->drm_backend) - 1u;
            memcpy(info->drm_backend, drm_name, len);
            info->drm_backend[len] = '\0';
        } else {
            info->drm_backend[0] = '\0';
        }
        return 0;
    }
    case VID_IOCTL_GET_FRAMEBUFFER: {
        struct vid_framebuffer_info *info = (struct vid_framebuffer_info *)arg;
        if (info == 0) return -1;
        info->fb_addr = g_mode.fb_addr;
        info->pitch = g_mode.pitch;
        info->width = g_mode.width;
        info->height = g_mode.height;
        info->bpp = g_mode.bpp;
        info->fb_size = kernel_video_frame_bytes();
        return 0;
    }
    default:
        return -1;
    }
}

struct video_mode *kernel_video_get_mode(void) {
    return &g_mode;
}

volatile uint8_t *kernel_video_get_fb(void) {
    return g_fb;
}

void kernel_video_set_fb_window(uintptr_t base, size_t bytes) {
    g_fb_window_base = base;
    g_fb_window_bytes = bytes;
}

size_t kernel_video_fb_window_bytes(void) {
    return g_fb_window_bytes;
}

/*
 * Real byte capacity of the buffer a caller writing through
 * kernel_video_get_backbuffer()/get_fb() may touch:
 *   - shadow backbuffer: its malloc'd size;
 *   - direct LFB: only the portion of the RESERVED aperture from the
 *     target onward (never the full pitch*height frame);
 *   - anything else (unexpected base): 0 — callers must refuse to draw
 *     rather than guess. 0 also results when the window was never set.
 */
size_t kernel_video_target_capacity(const void *target) {
    uintptr_t t = (uintptr_t)target;

    if (t == 0u) {
        return 0u;
    }
    if (g_shadow_backbuf != 0 && t == (uintptr_t)g_shadow_backbuf) {
        return g_shadow_backbuf_capacity;
    }
    if (g_fb_window_bytes != 0u &&
        t >= g_fb_window_base &&
        t < g_fb_window_base + g_fb_window_bytes) {
        return g_fb_window_bytes - (t - g_fb_window_base);
    }
    /* Direct fb without a recorded window: cannot vouch for capacity. */
    return 0u;
}

uint8_t *kernel_video_get_backbuffer(void) {
    kernel_video_try_alloc_backbuffer();
    return g_backbuf;
}

size_t kernel_video_get_pixel_count(void) {
    if (!kernel_video_has_graphics_mode()) {
        return 0u;
    }
    return (size_t)g_mode.width * (size_t)g_mode.height;
}

void kernel_video_clear(uint8_t color) {
    kernel_video_try_alloc_backbuffer();
    if (!kernel_video_has_graphics_mode()) {
        (void)color;
        return;
    }

    for (uint32_t y = 0u; y < g_mode.height; ++y) {
        uint8_t *row = g_backbuf + ((size_t)y * g_mode.pitch);

        for (uint32_t x = 0u; x < g_mode.width; ++x) {
            row[x] = color;
        }
    }
    kernel_video_mark_full_dirty();
}

void kernel_video_flip(void) {
    kernel_video_flip_mode(VIDEO_PRESENT_AUTO);
}

void kernel_video_set_present_policy(uint32_t policy) {
    enum kernel_video_auto_present_policy old_policy = g_auto_present_policy;

    switch (policy) {
    case VIDEO_PRESENT_POLICY_FULLSCREEN:
        g_auto_present_policy = KERNEL_VIDEO_AUTO_PRESENT_FULLSCREEN;
        break;
    case VIDEO_PRESENT_POLICY_DEFAULT:
    case VIDEO_PRESENT_POLICY_DESKTOP:
    default:
        g_auto_present_policy = KERNEL_VIDEO_AUTO_PRESENT_DIRTY;
        break;
    }

    if (old_policy == KERNEL_VIDEO_AUTO_PRESENT_FULLSCREEN &&
        g_auto_present_policy == KERNEL_VIDEO_AUTO_PRESENT_DIRTY &&
        g_direct_fullscreen_present_dirty) {
        kernel_video_resync_shadow_backbuffer();
        g_direct_fullscreen_present_dirty = 0;
    }
}

void kernel_video_set_present_copy_override(uint32_t kind) {
    switch (kind) {
    case VIDEO_PRESENT_COPY_OVERRIDE_AUTO:
    case VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP:
    case VIDEO_PRESENT_COPY_OVERRIDE_REP_MOVSD:
    case VIDEO_PRESENT_COPY_OVERRIDE_MOVNTDQ:
        g_present_copy_override_kind = kind;
        break;
    default:
        g_present_copy_override_kind = VIDEO_PRESENT_COPY_OVERRIDE_AUTO;
        break;
    }
    kernel_video_select_present_kind();
}

void kernel_video_flip_mode(uint32_t mode) {
    switch (mode) {
    case VIDEO_PRESENT_FULL:
        kernel_video_present_full();
        break;
    case VIDEO_PRESENT_DIRTY:
        kernel_video_present_dirty();
        break;
    case VIDEO_PRESENT_AUTO:
    default:
        kernel_video_present_auto();
        break;
    }
}

void kernel_video_present_full(void) {
    kernel_video_try_alloc_backbuffer();
    if (!kernel_video_has_graphics_mode() || g_backend_ops == 0 || g_backend_ops->present_full == 0) {
        return;
    }

    g_backend_ops->present_full();
    kernel_video_reset_dirty_state();
}

static void kernel_video_present_auto(void) {
    kernel_video_try_alloc_backbuffer();
    if (!kernel_video_has_graphics_mode() || !g_dirty_pending) {
        return;
    }

    if (g_dirty_full || g_auto_present_policy == KERNEL_VIDEO_AUTO_PRESENT_FULLSCREEN) {
        kernel_video_present_full();
        return;
    }

    kernel_video_present_dirty();
}

static void kernel_video_resync_shadow_backbuffer(void) {
    if (!kernel_video_has_graphics_mode() ||
        g_shadow_backbuf == 0 ||
        g_backbuf == 0 ||
        kernel_video_backbuffer_is_lfb()) {
        return;
    }

    kernel_video_copy_lfb_to_buffer(g_shadow_backbuf);
}

void kernel_video_present_dirty(void) {
    struct kernel_video_rect rect;

    kernel_video_try_alloc_backbuffer();
    if (!kernel_video_has_graphics_mode()) {
        return;
    }

    if (!g_dirty_pending) {
        return;
    }

    if (g_dirty_full) {
        kernel_video_present_full();
        return;
    }

    rect = g_dirty_rect;
    kernel_video_present_rect_internal(&rect);
    kernel_video_reset_dirty_state();
}

void kernel_video_leave_graphics(void) {
    if (kernel_video_has_graphics_mode()) {
        kernel_video_clear(0u);
        kernel_video_flip();
        kernel_text_init();
    }
}

int kernel_video_set_mode(uint32_t width, uint32_t height) {
    struct video_mode new_mode = {0};
    uint8_t mode_index;
    uint16_t bios_mode;
    uint32_t irq_flags;
    int switch_rc;
    int used_native_modeset;
    int used_bios_recovery;
    enum kernel_video_backend_kind active_backend;
    enum kernel_video_backend_kind target_backend;

    if (!kernel_video_has_graphics_mode()) {
        return -1;
    }

    if (width == g_mode.width && height == g_mode.height) {
        kernel_video_load_default_palette();
        return 0;
    }

    if (kernel_video_find_catalog_mode(width, height, &mode_index, &bios_mode) != 0) {
        return -1;
    }

    irq_flags = kernel_irq_save();
    kernel_keyboard_prepare_for_graphics();
    kernel_mouse_prepare_for_graphics();
    active_backend = g_backend_kind;
    used_native_modeset = 0;
    used_bios_recovery = 0;
    if (kernel_video_boot_forces_legacy_video()) {
        kernel_debug_printf("video: drm modeset skipped by boot flag for %dx%d, trying BIOS fallback mode=%x\n",
                            (int)width,
                            (int)height,
                            bios_mode);
        switch_rc = kernel_video_bios_set_mode(bios_mode);
    } else {
        switch_rc = kernel_drm_try_set_mode(width, height, bios_mode, &new_mode);
        if (switch_rc == 0) {
            used_native_modeset = 1;
        } else {
            kernel_debug_printf("video: drm modeset unavailable for %dx%d, trying BIOS fallback mode=%x\n",
                                (int)width,
                                (int)height,
                                bios_mode);
            kernel_drm_prepare_for_bios_modeset();
            switch_rc = kernel_video_bios_set_mode(bios_mode);
        }
    }
try_activate_mode:
    if (switch_rc == 0 &&
        ((new_mode.width == (uint16_t)width && new_mode.height == (uint16_t)height) ||
         vesa_init(&new_mode) == 0) &&
        kernel_video_mode_usable(&new_mode)) {
        target_backend = kernel_video_choose_boot_backend(&new_mode);
        if (used_bios_recovery) {
            kernel_debug_printf("video: BIOS recovery after native revert succeeded for %dx%d\n",
                                (int)width,
                                (int)height);
        }
        kernel_debug_printf("video: runtime modeset source=%s fb=%x pitch=%d %dx%d\n",
                            used_native_modeset ? "drm" :
                            (used_bios_recovery ? "bios-after-drm-revert" : "bios"),
                            new_mode.fb_addr,
                            (int)new_mode.pitch,
                            (int)new_mode.width,
                            (int)new_mode.height);
        kernel_video_mark_catalog_active(mode_index);
        if (target_backend != active_backend) {
            kernel_debug_printf("video: runtime backend switch %s -> %s for %dx%d frame=%d\n",
                                active_backend == KERNEL_VIDEO_BACKEND_FAST_LFB ? "fast_lfb" : "legacy_lfb",
                                target_backend == KERNEL_VIDEO_BACKEND_FAST_LFB ? "fast_lfb" : "legacy_lfb",
                                (int)new_mode.width,
                                (int)new_mode.height,
                                (int)((size_t)new_mode.pitch * (size_t)new_mode.height));
        }
        kernel_video_activate_backend(target_backend, &new_mode);
        kernel_video_load_default_palette();
        kernel_video_clear(0u);
        kernel_video_present_full();
        kernel_mouse_sync_to_video();
        kernel_irq_restore(irq_flags);
        if (used_native_modeset) {
            kernel_drm_forget_last_modeset();
        }
        kernel_video_log_state("runtime mode");
        kernel_video_log_handoff("runtime",
                                 used_native_modeset ? "drm" :
                                 (used_bios_recovery ? "bios-after-drm-revert" : "bios"));
        return 0;
    }
    if (used_native_modeset) {
        if (kernel_drm_revert_last_modeset() == 0) {
            kernel_debug_printf("video: reverted native modeset after handoff failure for %dx%d\n",
                                (int)width,
                                (int)height);
            kernel_debug_printf("video: trying BIOS fallback after native handoff failure for %dx%d mode=%x\n",
                                (int)width,
                                (int)height,
                                bios_mode);
            kernel_drm_prepare_for_bios_modeset();
            new_mode = (struct video_mode){0};
            switch_rc = kernel_video_bios_set_mode(bios_mode);
            if (switch_rc != 0) {
                kernel_debug_printf("video: BIOS recovery set_mode failed after native revert for %dx%d mode=%x\n",
                                    (int)width,
                                    (int)height,
                                    bios_mode);
            }
            used_native_modeset = 0;
            used_bios_recovery = 1;
            goto try_activate_mode;
        } else {
            kernel_debug_printf("video: native modeset revert failed after handoff failure for %dx%d\n",
                                (int)width,
                                (int)height);
        }
    }
    kernel_irq_restore(irq_flags);
    kernel_debug_printf("video: runtime mode switch failed for %dx%d source=%s\n",
                        (int)width,
                        (int)height,
                        used_native_modeset ? "drm" :
                        (used_bios_recovery ? "bios-after-drm-revert" : "bios"));
    kernel_debug_printf("video: handoff stage=runtime-fail source=%s backend=%s native=%s present=%s mode=%dx%dx%d pitch=%d fb=%x\n",
                        used_native_modeset ? "drm" :
                        (used_bios_recovery ? "bios-after-drm-revert" : "bios"),
                        kernel_video_backend_name(),
                        kernel_drm_active_backend_name() != 0 ? kernel_drm_active_backend_name() : "none",
                        kernel_video_present_kind_name(g_present_kind),
                        (int)g_mode.width,
                        (int)g_mode.height,
                        (int)g_mode.bpp,
                        (int)g_mode.pitch,
                        g_mode.fb_addr);
    return -1;
}

void kernel_video_get_capabilities(struct video_capabilities *caps) {
    if (caps == 0) {
        return;
    }

    caps->flags = 0u;
    caps->supported_modes = 0u;
    caps->active_width = g_mode.width;
    caps->active_height = g_mode.height;
    caps->active_bpp = g_mode.bpp;
    caps->mode_count = 0u;
    for (uint32_t i = 0u; i < VIDEO_MODE_LIST_MAX; ++i) {
        caps->mode_width[i] = 0u;
        caps->mode_height[i] = 0u;
    }

    if (kernel_video_has_graphics_mode()) {
        caps->flags |= VIDEO_CAPS_BOOT_LFB;
        kernel_video_load_boot_catalog(caps);
        if (caps->mode_count != 0u) {
            caps->flags |= VIDEO_CAPS_CAN_SET_MODE;
        }
        kernel_video_add_mode_cap(caps, (uint16_t)g_mode.width, (uint16_t)g_mode.height);
    } else {
        caps->flags |= VIDEO_CAPS_TEXT_ONLY;
        caps->supported_modes = 0u;
    }
}

void kernel_video_get_benchmarks(struct video_bench_info *bench) {
    size_t frame_bytes;
    size_t backbuffer_bytes;

    if (bench == 0) {
        return;
    }

    frame_bytes = kernel_video_frame_bytes();
    backbuffer_bytes = (g_backbuf != 0 && !kernel_video_backbuffer_is_lfb()) ? frame_bytes : 0u;
    g_video_perf.active_width = g_mode.width;
    g_video_perf.active_height = g_mode.height;
    g_video_perf.active_pitch = g_mode.pitch;
    g_video_perf.frame_bytes = frame_bytes;
    g_video_perf.backbuffer_bytes = backbuffer_bytes;
    g_video_perf.heap_free_after = kernel_heap_free();
    g_video_perf.wc_enabled = paging_pat_wc_enabled() ? 1u : 0u;
    g_video_perf.backend_kind = (uint32_t)g_backend_kind;
    g_video_perf.present_copy_kind = (uint32_t)g_present_kind;
    g_video_perf.present_copy_override_kind = g_present_copy_override_kind;
    g_video_perf.native_backend_kind = (uint32_t)kernel_drm_active_backend_kind();
    g_video_perf.detected_native_backend_kind = (uint32_t)kernel_drm_detected_backend_kind();

    bench->active_width = g_video_perf.active_width;
    bench->active_height = g_video_perf.active_height;
    bench->active_pitch = g_video_perf.active_pitch;
    bench->gpu_vendor_id = g_video_perf.gpu_vendor_id;
    bench->gpu_device_id = g_video_perf.gpu_device_id;
    bench->gpu_revision = g_video_perf.gpu_revision;
    bench->detected_gpu_vendor_id = g_video_perf.detected_gpu_vendor_id;
    bench->detected_gpu_device_id = g_video_perf.detected_gpu_device_id;
    bench->detected_gpu_revision = g_video_perf.detected_gpu_revision;
    bench->cpu_family = g_video_perf.cpu_family;
    bench->cpu_model = g_video_perf.cpu_model;
    bench->cpu_stepping = g_video_perf.cpu_stepping;
    bench->fill_ticks = g_video_perf.fill_ticks;
    bench->present_ticks = g_video_perf.present_ticks;
    bench->frame_ticks = g_video_perf.frame_ticks;
    bench->fullscreen_direct_ticks = g_video_perf.fullscreen_direct_ticks;
    bench->fullscreen_blit_present_ticks = g_video_perf.fullscreen_blit_present_ticks;
    bench->microkernel_frame_ticks = g_video_perf.microkernel_frame_ticks;
    bench->microkernel_flip_ticks = g_video_perf.microkernel_flip_ticks;
    bench->microkernel_blit_ticks = g_video_perf.microkernel_blit_ticks;
    bench->microkernel_stretch_ticks = g_video_perf.microkernel_stretch_ticks;
    bench->frame_bytes = (uint32_t)g_video_perf.frame_bytes;
    bench->backbuffer_bytes = (uint32_t)g_video_perf.backbuffer_bytes;
    bench->heap_free_before = (uint32_t)g_video_perf.heap_free_before;
    bench->heap_free_after = (uint32_t)g_video_perf.heap_free_after;
    bench->cpu_has_pat = g_video_perf.cpu_has_pat;
    bench->cpu_has_sse2 = g_video_perf.cpu_has_sse2;
    bench->wc_enabled = g_video_perf.wc_enabled;
    bench->backend_kind = g_video_perf.backend_kind;
    bench->present_copy_kind = g_video_perf.present_copy_kind;
    bench->present_copy_override_kind = g_video_perf.present_copy_override_kind;
    bench->native_backend_kind = g_video_perf.native_backend_kind;
    bench->detected_native_backend_kind = g_video_perf.detected_native_backend_kind;
    memcpy(bench->cpu_vendor, g_video_perf.cpu_vendor, sizeof(bench->cpu_vendor));
    bench->cpu_vendor[sizeof(bench->cpu_vendor) - 1u] = '\0';
}

/* graphics helper internal font & routines copied from stage2 */

static char uppercase_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static uint8_t font_row_bits(char c, int row) {
    c = uppercase_char(c);
    /* the big switch from stage2/graphics.c */
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

void kernel_gfx_putpixel(int x, int y, uint8_t color) {
    kernel_video_enter_graphics();
    struct video_mode *mode = kernel_video_get_mode();
    uint8_t *bb = kernel_video_get_backbuffer();

    if (!bb) return;
    if (x < 0 || y < 0 || x >= (int)mode->width || y >= (int)mode->height) return;
    bb[(y * mode->pitch) + x] = color;
    kernel_video_mark_dirty(x, y, 1, 1);
}

void kernel_gfx_rect(int x, int y, int w, int h, uint8_t color) {
    kernel_video_enter_graphics();
    struct video_mode *mode = kernel_video_get_mode();
    uint8_t *bb = kernel_video_get_backbuffer();
    if (!bb || w <= 0 || h <= 0) return;
    int x0 = x<0?0:x;
    int y0 = y<0?0:y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > (int)mode->width) x1 = (int)mode->width;
    if (y1 > (int)mode->height) y1 = (int)mode->height;
    if (x0 >= x1 || y0 >= y1) return;
    for (int py=y0; py<y1; ++py)
        for (int px=x0; px<x1; ++px)
            bb[(py * mode->pitch) + px] = color;
    kernel_video_mark_dirty(x0, y0, x1 - x0, y1 - y0);
}

void kernel_gfx_clear(uint8_t color) {
    kernel_video_enter_graphics();
    kernel_video_clear(color);
}

void kernel_gfx_draw_text(int x, int y, const char *text, uint8_t color) {
    kernel_video_enter_graphics();
    int cx = x, cy = y;
    while (*text) {
        char c = *text++;
        if (c=='\n') { cx = x; cy += 8; continue; }
        for (int row=0; row<7; ++row) {
            uint8_t bits = font_row_bits(c,row);
            for (int col=0; col<5; ++col) {
                if (!(bits & (1u << (4-col)))) continue;
                int px = cx + col;
                int py = cy + row;
                if (px<0||py<0||px>=(int)g_mode.width||py>=(int)g_mode.height) continue;
                g_backbuf[(py * g_mode.pitch) + px] = color;
            }
        }
        cx += 6;
    }
    kernel_video_mark_dirty(x, y, cx - x, cy - y + 7);
}

void kernel_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct video_mode *mode;
    uint8_t *bb;
    size_t src_pitch;

    kernel_video_enter_graphics();
    if (src == NULL || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return;
    }

    mode = kernel_video_get_mode();
    bb = kernel_video_get_backbuffer();
    if (mode == NULL || bb == NULL) {
        return;
    }

    src_pitch = kernel_video_blit8_source_pitch(src, src_w, src_h);
    if (src_pitch == 0u) {
        return;
    }

    for (int sy = 0; sy < src_h; ++sy) {
        int py0 = dst_y + (sy * scale);
        for (int sx = 0; sx < src_w; ++sx) {
            int px0 = dst_x + (sx * scale);
            uint8_t color = src[((size_t)sy * src_pitch) + (size_t)sx];

            for (int oy = 0; oy < scale; ++oy) {
                int py = py0 + oy;
                if (py < 0 || py >= (int)mode->height) {
                    continue;
                }
                for (int ox = 0; ox < scale; ++ox) {
                    int px = px0 + ox;
                    if (px < 0 || px >= (int)mode->width) {
                        continue;
                    }
                    bb[(py * mode->pitch) + px] = color;
                }
            }
        }
    }
    kernel_video_mark_dirty(dst_x, dst_y, src_w * scale, src_h * scale);
}

void kernel_gfx_blit8_present(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    size_t src_pitch;

    kernel_video_enter_graphics();
    if (src == NULL || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return;
    }

    if (kernel_video_has_graphics_mode() &&
        g_fb != 0 &&
        !kernel_video_backbuffer_is_lfb() &&
        scale == 1 &&
        dst_x == 0 &&
        dst_y == 0 &&
        src_w == (int)g_mode.width &&
        src_h == (int)g_mode.height) {
        uint8_t *fb = (uint8_t *)(uintptr_t)g_fb;

        src_pitch = kernel_video_blit8_source_pitch(src, src_w, src_h);
        if (src_pitch == 0u) {
            return;
        }

        for (int y = 0; y < src_h; ++y) {
            memcpy(fb + ((size_t)y * g_mode.pitch),
                   src + ((size_t)y * src_pitch),
                   (size_t)src_w);
        }
        g_direct_fullscreen_present_dirty = 1;
        kernel_video_reset_dirty_state();
        return;
    }

    kernel_gfx_blit8(src, src_w, src_h, dst_x, dst_y, scale);
    kernel_video_present_full();
}

static void kernel_video_blit8_stretch_to_target(const uint8_t *src, int src_w, int src_h,
                                                 int dst_x, int dst_y, int dst_w, int dst_h,
                                                 uint8_t *dst, uint32_t pitch, uint32_t width,
                                                 uint32_t height) {
    size_t src_pitch;

    src_pitch = kernel_video_blit8_source_pitch(src, src_w, src_h);
    if (src_pitch == 0u) {
        return;
    }

    for (int dy = 0; dy < dst_h; ++dy) {
        int py = dst_y + dy;
        int sy;

        if (py < 0 || py >= (int)height) {
            continue;
        }
        sy = (dy * src_h) / dst_h;
        if (sy < 0) {
            sy = 0;
        } else if (sy >= src_h) {
            sy = src_h - 1;
        }

        for (int dx = 0; dx < dst_w; ++dx) {
            int px = dst_x + dx;
            int sx;

            if (px < 0 || px >= (int)width) {
                continue;
            }
            sx = (dx * src_w) / dst_w;
            if (sx < 0) {
                sx = 0;
            } else if (sx >= src_w) {
                sx = src_w - 1;
            }
            dst[(py * pitch) + px] = src[((size_t)sy * src_pitch) + (size_t)sx];
        }
    }
}

static void kernel_video_blit8_stretch_to_backbuffer(const uint8_t *src, int src_w, int src_h,
                                                     int dst_x, int dst_y, int dst_w, int dst_h,
                                                     int mark_dirty) {
    struct video_mode *mode;
    uint8_t *bb;

    kernel_video_enter_graphics();
    if (src == NULL || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    mode = kernel_video_get_mode();
    bb = kernel_video_get_backbuffer();
    if (mode == NULL || bb == NULL) {
        return;
    }

    kernel_video_blit8_stretch_to_target(src, src_w, src_h, dst_x, dst_y, dst_w, dst_h,
                                         bb, mode->pitch, mode->width, mode->height);
    if (mark_dirty) {
        kernel_video_mark_dirty(dst_x, dst_y, dst_w, dst_h);
    }
}

void kernel_gfx_blit8_stretch(const uint8_t *src, int src_w, int src_h,
                              int dst_x, int dst_y, int dst_w, int dst_h) {
    kernel_video_blit8_stretch_to_backbuffer(src, src_w, src_h, dst_x, dst_y, dst_w, dst_h, 1);
}

void kernel_gfx_blit8_stretch_present(const uint8_t *src, int src_w, int src_h,
                                      int dst_x, int dst_y, int dst_w, int dst_h) {
    kernel_video_enter_graphics();
    if (src == NULL || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    if (kernel_video_has_graphics_mode() &&
        g_fb != 0 &&
        !kernel_video_backbuffer_is_lfb() &&
        dst_x == 0 &&
        dst_y == 0 &&
        dst_w == (int)g_mode.width &&
        dst_h == (int)g_mode.height) {
        kernel_video_blit8_stretch_to_target(src, src_w, src_h, dst_x, dst_y, dst_w, dst_h,
                                             (uint8_t *)(uintptr_t)g_fb,
                                             g_mode.pitch,
                                             g_mode.width,
                                             g_mode.height);
        g_direct_fullscreen_present_dirty = 1;
        kernel_video_reset_dirty_state();
        return;
    }

    kernel_video_blit8_stretch_to_backbuffer(src, src_w, src_h, dst_x, dst_y, dst_w, dst_h, 0);
    kernel_video_present_full();
}

int kernel_video_set_palette(const uint8_t *rgb_triplets) {
    if (rgb_triplets == NULL || !kernel_video_has_graphics_mode()) {
        return -1;
    }

    for (int i = 0; i < 256 * 3; ++i) {
        g_palette[i] = rgb_triplets[i];
    }
    g_palette_ready = 1;
    kernel_video_program_palette();
    return 0;
}

int kernel_video_get_palette(uint8_t *rgb_triplets) {
    if (rgb_triplets == NULL || !kernel_video_has_graphics_mode()) {
        return -1;
    }

    if (!g_palette_ready) {
        kernel_video_load_default_palette();
    }
    for (int i = 0; i < 256 * 3; ++i) {
        rgb_triplets[i] = g_palette[i];
    }
    return 0;
}

struct kernel_video_boundary_desc kernel_video_boundary(void) {
    struct kernel_video_boundary_desc b;
    memset(&b, 0, sizeof(b));
    b.privileged_mmio = 1;
    b.privileged_modeset = 1;
    b.privileged_irq = 1;
    b.privileged_palette_dac = 1;
    b.privileged_fb_map = 1;
    b.privileged_backend_select = 1;
    b.userland_present_queue = 1;
    b.userland_control_plane = 1;
    b.userland_policy = 1;
    b.userland_compositor = 1;
    return b;
}
