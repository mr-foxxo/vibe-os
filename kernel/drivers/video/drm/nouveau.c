#include <kernel/drivers/video/drm/drm.h>
#include <kernel/drivers/video/drm/nouveau.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <kernel/bootinfo.h>

#define PCI_VENDOR_NVIDIA 0x10DEu
#define GRAPHICS_BPP 8u
#define GRAPHICS_MIN_FB_ADDR 0x00100000u
#define GRAPHICS_MIN_WIDTH 320u
#define GRAPHICS_MIN_HEIGHT 200u
#define GRAPHICS_MAX_WIDTH 1920u
#define GRAPHICS_MAX_HEIGHT 1080u

struct kernel_drm_nouveau_probe_info {
    uint16_t command;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;
    uint32_t rom_bar;
};

struct kernel_drm_nouveau_mmio_snapshot {
    uint32_t reg00;
    uint32_t reg04;
    uint32_t reg08;
    uint32_t reg0c;
};

struct kernel_drm_nouveau_vga_snapshot {
    uint8_t misc_output;
    uint8_t crtc_htotal;
    uint8_t crtc_hdisplay_end;
    uint8_t crtc_vtotal;
    uint8_t crtc_overflow;
    uint8_t crtc_vdisplay_end;
    uint8_t crtc_mode_control;
};

struct kernel_drm_nouveau_display_snapshot {
    uint32_t crtc_config;
    uint32_t crtc_horiz;
    uint32_t crtc_vert;
    uint32_t crtc_hsync;
    uint32_t crtc_vsync;
    uint32_t crtc_start;
    uint32_t crtc_pitch;
    uint32_t ramdac_dacclk;
    uint32_t ramdac_daccontrol;
};

struct kernel_drm_nouveau_timing {
    uint32_t width;
    uint32_t height;
    uint32_t htotal;
    uint32_t hdisplay_end;
    uint32_t hsync_start;
    uint32_t hsync_end;
    uint32_t vtotal;
    uint32_t vdisplay_end;
    uint32_t vsync_start;
    uint32_t vsync_end;
};

struct kernel_drm_nouveau_saved_state {
    int valid;
    struct kernel_drm_candidate candidate;
    struct kernel_drm_nouveau_display_snapshot before;
    struct kernel_drm_nouveau_display_snapshot after;
    struct bootinfo_vesa boot_vesa;
    uint32_t boot_flags;
};

#define VGA_MISC_OUTPUT_READ 0x3CCu
#define VGA_CRTC_COLOR_INDEX 0x3D4u
#define VGA_CRTC_COLOR_DATA 0x3D5u
#define VGA_CRTC_MONO_INDEX 0x3B4u
#define VGA_CRTC_MONO_DATA 0x3B5u

static struct kernel_drm_nouveau_saved_state g_nouveau_saved;
static int g_nouveau_modeset_enabled = 1;

void kernel_drm_nouveau_enable_modeset(void) {
    g_nouveau_modeset_enabled = 1;
    kernel_debug_puts("nouveau: modeset enabled\n");
}

static uint16_t kernel_drm_nouveau_vga_crtc_index_port(uint8_t misc_output) {
    return (misc_output & 0x01u) != 0u ? VGA_CRTC_COLOR_INDEX : VGA_CRTC_MONO_INDEX;
}

static uint16_t kernel_drm_nouveau_vga_crtc_data_port(uint8_t misc_output) {
    return (misc_output & 0x01u) != 0u ? VGA_CRTC_COLOR_DATA : VGA_CRTC_MONO_DATA;
}

static uint8_t kernel_drm_nouveau_vga_read_crtc(uint8_t misc_output, uint8_t index) {
    outb(kernel_drm_nouveau_vga_crtc_index_port(misc_output), index);
    return inb(kernel_drm_nouveau_vga_crtc_data_port(misc_output));
}

static uintptr_t kernel_drm_nouveau_mmio_ptr(const struct kernel_drm_candidate *candidate,
                                             uint32_t reg) {
    if (candidate == 0 || candidate->mmio_base == (uintptr_t)0) {
        return (uintptr_t)0;
    }
    if ((size_t)reg + sizeof(uint32_t) > candidate->mmio_size) {
        return (uintptr_t)0;
    }
    return candidate->mmio_base + (uintptr_t)reg;
}

static uint32_t kernel_drm_nouveau_mmio_read32(const struct kernel_drm_candidate *candidate,
                                               uint32_t reg) {
    volatile uint32_t *ptr =
        (volatile uint32_t *)(uintptr_t)kernel_drm_nouveau_mmio_ptr(candidate, reg);

    if (ptr == 0) {
        return 0u;
    }
    return *ptr;
}

static void kernel_drm_nouveau_mmio_write32(const struct kernel_drm_candidate *candidate,
                                            uint32_t reg,
                                            uint32_t value) {
    volatile uint32_t *ptr =
        (volatile uint32_t *)(uintptr_t)kernel_drm_nouveau_mmio_ptr(candidate, reg);

    if (ptr == 0) {
        return;
    }
    *ptr = value;
}

static int kernel_drm_nouveau_snapshot_mmio(const struct kernel_drm_candidate *candidate,
                                            struct kernel_drm_nouveau_mmio_snapshot *snapshot_out) {
    if (candidate == 0 || snapshot_out == 0 || candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }
    if (candidate->mmio_size < 0x10u) {
        return -1;
    }

    snapshot_out->reg00 = kernel_drm_nouveau_mmio_read32(candidate, 0x00u);
    snapshot_out->reg04 = kernel_drm_nouveau_mmio_read32(candidate, 0x04u);
    snapshot_out->reg08 = kernel_drm_nouveau_mmio_read32(candidate, 0x08u);
    snapshot_out->reg0c = kernel_drm_nouveau_mmio_read32(candidate, 0x0Cu);
    return 0;
}

static int kernel_drm_nouveau_snapshot_vga(struct kernel_drm_nouveau_vga_snapshot *snapshot_out) {
    uint8_t misc_output;

    if (snapshot_out == 0) {
        return -1;
    }

    misc_output = inb(VGA_MISC_OUTPUT_READ);
    snapshot_out->misc_output = misc_output;
    snapshot_out->crtc_htotal = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x00u);
    snapshot_out->crtc_hdisplay_end = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x01u);
    snapshot_out->crtc_vtotal = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x06u);
    snapshot_out->crtc_overflow = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x07u);
    snapshot_out->crtc_vdisplay_end = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x12u);
    snapshot_out->crtc_mode_control = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x17u);
    return 0;
}

static int kernel_drm_nouveau_read_probe_info(const struct kernel_drm_candidate *candidate,
                                              struct kernel_drm_nouveau_probe_info *info_out) {
    if (candidate == 0 || info_out == 0) {
        return -1;
    }
    if (candidate->pci.vendor_id != PCI_VENDOR_NVIDIA) {
        return -1;
    }

    info_out->command = kernel_pci_config_read_u16(candidate->pci.bus,
                                                   candidate->pci.slot,
                                                   candidate->pci.function,
                                                   0x04u);
    info_out->subsystem_vendor = kernel_pci_config_read_u16(candidate->pci.bus,
                                                            candidate->pci.slot,
                                                            candidate->pci.function,
                                                            0x2Cu);
    info_out->subsystem_device = kernel_pci_config_read_u16(candidate->pci.bus,
                                                            candidate->pci.slot,
                                                            candidate->pci.function,
                                                            0x2Eu);
    info_out->rom_bar = kernel_pci_config_read_u32(candidate->pci.bus,
                                                   candidate->pci.slot,
                                                   candidate->pci.function,
                                                   0x30u);
    return 0;
}

static int kernel_drm_nouveau_snapshot_display(const struct kernel_drm_candidate *candidate,
                                               struct kernel_drm_nouveau_display_snapshot *snap) {
    uint32_t crtc_base;
    uint32_t ramdac_base;

    if (candidate == 0 || snap == 0 || candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }

    crtc_base = NV_PCRTC0_START;
    ramdac_base = NV_PRAMDAC0;

    if (candidate->mmio_size < ramdac_base + NV_PRAMDAC_DACCONTROL + sizeof(uint32_t)) {
        return -1;
    }

    snap->crtc_config = kernel_drm_nouveau_mmio_read32(candidate, crtc_base + NV_PCRTC_CONFIG);
    snap->crtc_horiz = kernel_drm_nouveau_mmio_read32(candidate, crtc_base + NV_PCRTC_HORIZ);
    snap->crtc_vert = kernel_drm_nouveau_mmio_read32(candidate, crtc_base + NV_PCRTC_VERT);
    snap->crtc_hsync = kernel_drm_nouveau_mmio_read32(candidate, crtc_base + NV_PCRTC_HSYNC);
    snap->crtc_vsync = kernel_drm_nouveau_mmio_read32(candidate, crtc_base + NV_PCRTC_VSYNC);
    snap->crtc_start = kernel_drm_nouveau_mmio_read32(candidate, crtc_base + NV_PCRTC_START);
    snap->crtc_pitch = kernel_drm_nouveau_mmio_read32(candidate, crtc_base + NV_PCRTC_PITCH);
    snap->ramdac_dacclk = kernel_drm_nouveau_mmio_read32(candidate, ramdac_base + NV_PRAMDAC_DACCLK);
    snap->ramdac_daccontrol = kernel_drm_nouveau_mmio_read32(candidate, ramdac_base + NV_PRAMDAC_DACCONTROL);
    return 0;
}

static void kernel_drm_nouveau_restore_display(const struct kernel_drm_candidate *candidate,
                                               const struct kernel_drm_nouveau_display_snapshot *snap) {
    uint32_t crtc_base;
    uint32_t ramdac_base;

    if (candidate == 0 || snap == 0) {
        return;
    }

    crtc_base = NV_PCRTC0_START;
    ramdac_base = NV_PRAMDAC0;

    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_CONFIG, snap->crtc_config);
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_HORIZ, snap->crtc_horiz);
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_VERT, snap->crtc_vert);
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_HSYNC, snap->crtc_hsync);
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_VSYNC, snap->crtc_vsync);
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_START, snap->crtc_start);
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_PITCH, snap->crtc_pitch);
    kernel_drm_nouveau_mmio_write32(candidate, ramdac_base + NV_PRAMDAC_DACCLK, snap->ramdac_dacclk);
    kernel_drm_nouveau_mmio_write32(candidate, ramdac_base + NV_PRAMDAC_DACCONTROL, snap->ramdac_daccontrol);
}

static int kernel_drm_nouveau_fill_timing(uint32_t width,
                                          uint32_t height,
                                          struct kernel_drm_nouveau_timing *timing_out) {
    if (timing_out == 0) {
        return -1;
    }

    if (width < GRAPHICS_MIN_WIDTH || width > GRAPHICS_MAX_WIDTH ||
        height < GRAPHICS_MIN_HEIGHT || height > GRAPHICS_MAX_HEIGHT) {
        return -1;
    }

    timing_out->width = width;
    timing_out->height = height;

    if (width == 800u && height == 600u) {
        timing_out->htotal = 1056u;
        timing_out->hdisplay_end = 800u;
        timing_out->hsync_start = 840u;
        timing_out->hsync_end = 968u;
        timing_out->vtotal = 628u;
        timing_out->vdisplay_end = 600u;
        timing_out->vsync_start = 601u;
        timing_out->vsync_end = 605u;
        return 0;
    }
    if (width == 1024u && height == 768u) {
        timing_out->htotal = 1344u;
        timing_out->hdisplay_end = 1024u;
        timing_out->hsync_start = 1048u;
        timing_out->hsync_end = 1184u;
        timing_out->vtotal = 806u;
        timing_out->vdisplay_end = 768u;
        timing_out->vsync_start = 771u;
        timing_out->vsync_end = 777u;
        return 0;
    }
    if (width == 640u && height == 480u) {
        timing_out->htotal = 800u;
        timing_out->hdisplay_end = 640u;
        timing_out->hsync_start = 656u;
        timing_out->hsync_end = 752u;
        timing_out->vtotal = 525u;
        timing_out->vdisplay_end = 480u;
        timing_out->vsync_start = 490u;
        timing_out->vsync_end = 492u;
        return 0;
    }
    if (width == 1360u && height == 768u) {
        timing_out->htotal = 1792u;
        timing_out->hdisplay_end = 1360u;
        timing_out->hsync_start = 1384u;
        timing_out->hsync_end = 1552u;
        timing_out->vtotal = 798u;
        timing_out->vdisplay_end = 768u;
        timing_out->vsync_start = 771u;
        timing_out->vsync_end = 777u;
        return 0;
    }
    if (width == 1920u && height == 1080u) {
        timing_out->htotal = 2200u;
        timing_out->hdisplay_end = 1920u;
        timing_out->hsync_start = 1968u;
        timing_out->hsync_end = 2000u;
        timing_out->vtotal = 1125u;
        timing_out->vdisplay_end = 1080u;
        timing_out->vsync_start = 1083u;
        timing_out->vsync_end = 1089u;
        return 0;
    }
    return -1;
}

static uint32_t kernel_drm_nouveau_align_pitch(uint32_t width) {
    return (width + 63u) & ~63u;
}

static int kernel_drm_nouveau_snapshot_boot_mode(struct bootinfo_vesa *vesa_out,
                                                 uint32_t *flags_out) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;

    if (vesa_out == 0 || flags_out == 0) {
        return -1;
    }
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    *vesa_out = bootinfo->vesa;
    *flags_out = bootinfo->flags;
    return 0;
}

static int kernel_drm_nouveau_restore_boot_mode(const struct bootinfo_vesa *vesa,
                                                uint32_t flags) {
    volatile struct bootinfo *bootinfo =
        (volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;

    if (vesa == 0) {
        return -1;
    }
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    bootinfo->vesa = *vesa;
    bootinfo->flags = (bootinfo->flags & ~BOOTINFO_FLAG_VESA_VALID) |
                      (flags & BOOTINFO_FLAG_VESA_VALID);
    return 0;
}

static int kernel_drm_nouveau_publish_boot_mode(uint16_t mode_id,
                                                const struct video_mode *mode) {
    volatile struct bootinfo *bootinfo;

    if (mode == 0 ||
        mode->fb_addr < GRAPHICS_MIN_FB_ADDR ||
        mode->width > 0xFFFFu ||
        mode->height > 0xFFFFu ||
        mode->pitch > 0xFFFFu ||
        mode->bpp != GRAPHICS_BPP) {
        return -1;
    }

    bootinfo = (volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    bootinfo->flags |= BOOTINFO_FLAG_VESA_VALID;
    bootinfo->vesa.mode = mode_id;
    bootinfo->vesa.fb_addr = mode->fb_addr;
    bootinfo->vesa.pitch = (uint16_t)mode->pitch;
    bootinfo->vesa.width = (uint16_t)mode->width;
    bootinfo->vesa.height = (uint16_t)mode->height;
    bootinfo->vesa.bpp = mode->bpp;
    return 0;
}

static void kernel_drm_nouveau_remember_saved_state(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_nouveau_display_snapshot *before,
    const struct kernel_drm_nouveau_display_snapshot *after,
    const struct bootinfo_vesa *boot_vesa,
    uint32_t boot_flags) {
    if (candidate == 0 || before == 0 || after == 0 || boot_vesa == 0) {
        g_nouveau_saved.valid = 0;
        return;
    }

    g_nouveau_saved.candidate = *candidate;
    g_nouveau_saved.before = *before;
    g_nouveau_saved.after = *after;
    g_nouveau_saved.boot_vesa = *boot_vesa;
    g_nouveau_saved.boot_flags = boot_flags;
    g_nouveau_saved.valid = 1;
}

static void kernel_drm_nouveau_clear_saved_state(void) {
    g_nouveau_saved.valid = 0;
    g_nouveau_saved.candidate = (struct kernel_drm_candidate){0};
    g_nouveau_saved.before = (struct kernel_drm_nouveau_display_snapshot){0};
    g_nouveau_saved.after = (struct kernel_drm_nouveau_display_snapshot){0};
    g_nouveau_saved.boot_vesa = (struct bootinfo_vesa){0};
    g_nouveau_saved.boot_flags = 0u;
}

static int kernel_drm_nouveau_probe(const struct kernel_drm_candidate *candidate) {
    struct kernel_drm_nouveau_probe_info probe;
    struct kernel_drm_nouveau_mmio_snapshot mmio;
    struct kernel_drm_nouveau_vga_snapshot vga;
    struct kernel_drm_nouveau_display_snapshot display;
    int have_mmio_snapshot = 0;
    int have_vga_snapshot = 0;
    int have_display_snapshot = 0;

    if (kernel_drm_nouveau_read_probe_info(candidate, &probe) != 0) {
        return -1;
    }
    if ((probe.command & PCI_COMMAND_MEMORY_SPACE) == 0u) {
        kernel_debug_printf("nouveau: memory decode disabled dev=%x rev=%x mmio=%x fb=%x\n",
                            candidate->pci.device_id,
                            candidate->pci.revision,
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->fb_base);
        return -1;
    }
    if (candidate->mmio_base == (uintptr_t)0 || candidate->mmio_size == 0u) {
        kernel_debug_printf("nouveau: missing MMIO BAR dev=%x bar0=%x mmio=%x size=%x\n",
                            candidate->pci.device_id,
                            candidate->pci.bars[0],
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->mmio_size);
        return -1;
    }
    if (kernel_drm_nouveau_snapshot_mmio(candidate, &mmio) == 0) {
        have_mmio_snapshot = 1;
    }
    if (kernel_drm_nouveau_snapshot_vga(&vga) == 0) {
        have_vga_snapshot = 1;
    }
    if (kernel_drm_nouveau_snapshot_display(candidate, &display) == 0) {
        have_display_snapshot = 1;
    }

    kernel_debug_printf("nouveau: probe dev=%x rev=%x class=%x subclass=%x prog_if=%x command=%x mmio=%x mmio_size=%x fb=%x fb_size=%x subsys=%x:%x irq=%x rom=%x\n",
                        candidate->pci.device_id,
                        candidate->pci.revision,
                        candidate->pci.class_code,
                        candidate->pci.subclass,
                        candidate->pci.prog_if,
                        probe.command,
                        (uint32_t)candidate->mmio_base,
                        (uint32_t)candidate->mmio_size,
                        (uint32_t)candidate->fb_base,
                        (uint32_t)candidate->fb_size,
                        probe.subsystem_vendor,
                        probe.subsystem_device,
                        candidate->pci.irq_line,
                        probe.rom_bar);
    if (have_mmio_snapshot) {
        kernel_debug_printf("nouveau: mmio reg00=%x reg04=%x reg08=%x reg0c=%x\n",
                            mmio.reg00,
                            mmio.reg04,
                            mmio.reg08,
                            mmio.reg0c);
    }
    if (have_vga_snapshot) {
        kernel_debug_printf("nouveau: vga misc=%x htotal=%x hdisp_end=%x vtotal=%x overflow=%x vdisp_end=%x mode_ctl=%x\n",
                            vga.misc_output,
                            vga.crtc_htotal,
                            vga.crtc_hdisplay_end,
                            vga.crtc_vtotal,
                            vga.crtc_overflow,
                            vga.crtc_vdisplay_end,
                            vga.crtc_mode_control);
    }
    if (have_display_snapshot) {
        kernel_debug_printf("nouveau: display cfg=%x horiz=%x vert=%x hsync=%x vsync=%x start=%x pitch=%x dacclk=%x dacctl=%x\n",
                            display.crtc_config,
                            display.crtc_horiz,
                            display.crtc_vert,
                            display.crtc_hsync,
                            display.crtc_vsync,
                            display.crtc_start,
                            display.crtc_pitch,
                            display.ramdac_dacclk,
                            display.ramdac_daccontrol);
    }
    kernel_drm_nouveau_enable_modeset();
    return 0;
}

static int kernel_drm_nouveau_set_mode(const struct kernel_drm_candidate *candidate,
                                       uint32_t width,
                                       uint32_t height,
                                       uint16_t mode_id,
                                       struct video_mode *mode_out) {
    struct kernel_drm_nouveau_timing timing;
    struct kernel_drm_nouveau_display_snapshot before;
    struct kernel_drm_nouveau_display_snapshot after;
    struct bootinfo_vesa previous_boot_vesa;
    uint32_t previous_boot_flags;
    uint32_t crtc_base;
    uint32_t pitch;
    uint32_t fb_addr;
    uint32_t vram_offset;
    int have_previous_boot = 0;

    if (mode_out != 0) {
        *mode_out = (struct video_mode){0};
    }
    kernel_drm_nouveau_clear_saved_state();

    if (candidate == 0) {
        kernel_debug_puts("nouveau: set_mode blocked: no candidate\n");
        return -1;
    }
    if (!g_nouveau_modeset_enabled) {
        kernel_debug_printf("nouveau: modeset gated dev=%x requested=%dx%d mode=%x mmio=%x fb=%x\n",
                            candidate->pci.device_id,
                            (int)width,
                            (int)height,
                            mode_id,
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->fb_base);
        return -1;
    }
    if (candidate->mmio_base == (uintptr_t)0 || candidate->mmio_size < 0x00690000u) {
        kernel_debug_printf("nouveau: MMIO range too small for NV40 display dev=%x mmio=%x size=%x\n",
                            candidate->pci.device_id,
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->mmio_size);
        return -1;
    }
    if (kernel_drm_nouveau_fill_timing(width, height, &timing) != 0) {
        kernel_debug_printf("nouveau: unsupported resolution %dx%d dev=%x\n",
                            (int)width,
                            (int)height,
                            candidate->pci.device_id);
        return -1;
    }
    if (kernel_drm_nouveau_snapshot_display(candidate, &before) != 0) {
        kernel_debug_printf("nouveau: pre-modeset snapshot failed dev=%x\n",
                            candidate->pci.device_id);
        return -1;
    }

    pitch = kernel_drm_nouveau_align_pitch(width);

    if (kernel_drm_nouveau_snapshot_boot_mode(&previous_boot_vesa, &previous_boot_flags) == 0) {
        have_previous_boot = 1;
    } else {
        previous_boot_vesa = (struct bootinfo_vesa){0};
        previous_boot_flags = 0u;
    }

    if (have_previous_boot &&
        previous_boot_vesa.fb_addr >= GRAPHICS_MIN_FB_ADDR &&
        (uint32_t)candidate->fb_base != 0u &&
        previous_boot_vesa.fb_addr >= (uint32_t)candidate->fb_base) {
        fb_addr = previous_boot_vesa.fb_addr;
        vram_offset = fb_addr - (uint32_t)candidate->fb_base;
    } else {
        kernel_debug_printf("nouveau: no valid boot framebuffer for handoff dev=%x boot_fb=%x aperture=%x+%x\n",
                            candidate->pci.device_id,
                            have_previous_boot ? previous_boot_vesa.fb_addr : 0u,
                            (uint32_t)candidate->fb_base,
                            (uint32_t)candidate->fb_size);
        return -1;
    }

    crtc_base = NV_PCRTC0_START;

    kernel_debug_printf("nouveau: modeset dev=%x %dx%d vram_offset=%x pitch=%x dacclk=%x crtc_base=%x\n",
                        candidate->pci.device_id,
                        (int)width,
                        (int)height,
                        vram_offset,
                        pitch,
                        before.ramdac_dacclk,
                        crtc_base);

    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_HORIZ,
                                    NV_ENCODE_HORIZ(timing.htotal, timing.hdisplay_end));
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_VERT,
                                    NV_ENCODE_VERT(timing.vtotal, timing.vdisplay_end));
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_HSYNC,
                                    NV_ENCODE_HSYNC(timing.hsync_start, timing.hsync_end));
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_VSYNC,
                                    NV_ENCODE_VSYNC(timing.vsync_start, timing.vsync_end));
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_START, vram_offset);
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_PITCH, pitch);
    kernel_drm_nouveau_mmio_write32(candidate, crtc_base + NV_PCRTC_CONFIG,
                                    NV_PCRTC_CONFIG_ENABLE);

    if (kernel_drm_nouveau_snapshot_display(candidate, &after) != 0) {
        kernel_drm_nouveau_restore_display(candidate, &before);
        kernel_debug_printf("nouveau: post-modeset snapshot failed, rolled back dev=%x\n",
                            candidate->pci.device_id);
        return -1;
    }

    kernel_drm_nouveau_publish_boot_mode(mode_id, &(struct video_mode){
        .fb_addr = fb_addr,
        .width = width,
        .height = height,
        .pitch = pitch,
        .bpp = GRAPHICS_BPP
    });

    kernel_drm_nouveau_remember_saved_state(candidate, &before, &after,
                                            &previous_boot_vesa, previous_boot_flags);

    if (mode_out != 0) {
        mode_out->fb_addr = fb_addr;
        mode_out->width = width;
        mode_out->height = height;
        mode_out->pitch = pitch;
        mode_out->bpp = GRAPHICS_BPP;
    }

    kernel_debug_printf("nouveau: modeset ok dev=%x %dx%d fb=%x pitch=%x mode=%x\n",
                        candidate->pci.device_id,
                        (int)width,
                        (int)height,
                        fb_addr,
                        pitch,
                        mode_id);
    return 0;
}

static int kernel_drm_nouveau_revert_last_modeset(void) {
    struct kernel_drm_candidate candidate;
    struct kernel_drm_nouveau_display_snapshot before;

    if (!g_nouveau_saved.valid) {
        kernel_debug_puts("nouveau: revert requested but no saved state\n");
        return -1;
    }

    candidate = g_nouveau_saved.candidate;
    before = g_nouveau_saved.before;
    kernel_drm_nouveau_restore_display(&candidate, &before);
    kernel_drm_nouveau_restore_boot_mode(&g_nouveau_saved.boot_vesa,
                                         g_nouveau_saved.boot_flags);
    kernel_drm_nouveau_clear_saved_state();

    kernel_debug_printf("nouveau: revert ok dev=%x\n",
                        candidate.pci.device_id);
    return 0;
}

static void kernel_drm_nouveau_forget_last_modeset(void) {
    kernel_drm_nouveau_clear_saved_state();
    kernel_debug_puts("nouveau: saved state cleared\n");
}

static void kernel_drm_nouveau_prepare_for_bios_modeset(void) {
    uint32_t crtc_base;
    volatile uint32_t *cfg_ptr;
    volatile uint32_t *raster_ptr;
    volatile uint32_t *dacctl_ptr;
    uint32_t raster;
    uint32_t vdisplay_end;
    uint32_t vert_reg;
    uint32_t tries;

    if (!g_nouveau_saved.valid) {
        return;
    }

    crtc_base = NV_PCRTC0_START;

    cfg_ptr = (volatile uint32_t *)(uintptr_t)
        kernel_drm_nouveau_mmio_ptr(&g_nouveau_saved.candidate, crtc_base + NV_PCRTC_CONFIG);
    raster_ptr = (volatile uint32_t *)(uintptr_t)
        kernel_drm_nouveau_mmio_ptr(&g_nouveau_saved.candidate, crtc_base + NV_PCRTC_RASTER);

    if (cfg_ptr == 0 || raster_ptr == 0) {
        return;
    }

    *cfg_ptr = 0u;

    vert_reg = kernel_drm_nouveau_mmio_read32(&g_nouveau_saved.candidate,
                                              crtc_base + NV_PCRTC_VERT);
    vdisplay_end = (vert_reg >> 16) & 0x0FFFu;

    for (tries = 0u; tries < 10000000u; ++tries) {
        raster = *raster_ptr & 0x0FFFu;
        if (raster >= vdisplay_end) {
            break;
        }
    }

    dacctl_ptr = (volatile uint32_t *)(uintptr_t)
        kernel_drm_nouveau_mmio_ptr(&g_nouveau_saved.candidate,
                                    NV_PRAMDAC0 + NV_PRAMDAC_DACCONTROL);
    if (dacctl_ptr != 0) {
        *dacctl_ptr = 0u;
    }

    kernel_debug_printf("nouveau: prepare_for_bios CRTC disabled dev=%x\n",
                        g_nouveau_saved.candidate.pci.device_id);
}

const struct kernel_drm_backend_ops g_kernel_drm_nouveau_ops = {
    KERNEL_DRM_BACKEND_NOUVEAU,
    "native_gpu_nouveau",
    kernel_drm_nouveau_probe,
    kernel_drm_nouveau_set_mode,
    kernel_drm_nouveau_revert_last_modeset,
    kernel_drm_nouveau_forget_last_modeset,
    kernel_drm_nouveau_prepare_for_bios_modeset
};
