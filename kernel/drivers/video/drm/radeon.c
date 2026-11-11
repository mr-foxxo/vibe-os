#include <kernel/drivers/video/drm/drm.h>
#include <kernel/bootinfo.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>

#define PCI_VENDOR_AMD 0x1002u
#define GRAPHICS_BPP 8u
#define GRAPHICS_MIN_FB_ADDR 0x00100000u
#define GRAPHICS_MIN_WIDTH 320u
#define GRAPHICS_MIN_HEIGHT 200u
#define GRAPHICS_MAX_WIDTH 1920u
#define GRAPHICS_MAX_HEIGHT 1080u
#define AVIVO_D1CRTC_H_TOTAL 0x6000u
#define AVIVO_D1CRTC_H_BLANK_START_END 0x6004u
#define AVIVO_D1CRTC_H_SYNC_A 0x6008u
#define AVIVO_D1CRTC_V_TOTAL 0x6020u
#define AVIVO_D1CRTC_V_BLANK_START_END 0x6024u
#define AVIVO_D1CRTC_V_SYNC_A 0x6028u
#define AVIVO_D1CRTC_CONTROL 0x6080u
#define AVIVO_D1CRTC_BLANK_CONTROL 0x6084u
#define AVIVO_D1CRTC_STATUS 0x609Cu
#define AVIVO_D1CRTC_UPDATE_LOCK 0x60E8u
#define AVIVO_D1GRPH_ENABLE 0x6100u
#define AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS 0x6110u
#define AVIVO_D1GRPH_SECONDARY_SURFACE_ADDRESS 0x6118u
#define AVIVO_D1GRPH_PITCH 0x6120u
#define AVIVO_D1GRPH_X_START 0x612Cu
#define AVIVO_D1GRPH_Y_START 0x6130u
#define AVIVO_D1GRPH_X_END 0x6134u
#define AVIVO_D1GRPH_Y_END 0x6138u
#define AVIVO_D1GRPH_UPDATE 0x6144u
#define AVIVO_D1GRPH_UPDATE_LOCK (1u << 16)
#define AVIVO_D1CUR_CONTROL 0x6400u
#define AVIVO_D1CUR_SIZE 0x6410u
#define AVIVO_D1MODE_VLINE_STATUS 0x653Cu
#define AVIVO_D1MODE_VIEWPORT_START 0x6580u
#define AVIVO_D1MODE_VIEWPORT_SIZE 0x6584u
#define AVIVO_D1MODE_MASTER_UPDATE_LOCK 0x60E0u
#define AVIVO_D1MODE_MASTER_UPDATE_MODE 0x60E4u
#define AVIVO_D1GRPH_CONTROL 0x6104u
#define AVIVO_D1GRPH_CONTROL_DEPTH_8BPP (0u << 0)
#define AVIVO_D1GRPH_CONTROL_8BPP_INDEXED (0u << 8)
#define AVIVO_D2CRTC_H_TOTAL 0x6800u
#define AVIVO_D2CRTC_H_BLANK_START_END 0x6804u
#define AVIVO_D2CRTC_H_SYNC_A 0x6808u
#define AVIVO_D2CRTC_V_TOTAL 0x6820u
#define AVIVO_D2CRTC_V_BLANK_START_END 0x6824u
#define AVIVO_D2CRTC_V_SYNC_A 0x6828u
#define AVIVO_D2CRTC_CONTROL 0x6880u
#define AVIVO_D2CRTC_BLANK_CONTROL 0x6884u
#define AVIVO_D2CRTC_STATUS 0x689Cu
#define AVIVO_D2GRPH_ENABLE 0x6900u
#define AVIVO_D2GRPH_PRIMARY_SURFACE_ADDRESS 0x6910u
#define AVIVO_D2MODE_VLINE_STATUS 0x6D3Cu
#define AVIVO_D2MODE_VIEWPORT_START 0x6D80u
#define AVIVO_D2MODE_VIEWPORT_SIZE 0x6D84u
#define AVIVO_CRTC_EN (1u << 0)
#define AVIVO_CRTC_DISP_READ_REQUEST_DISABLE (1u << 24)
#define AVIVO_D1CRTC_V_BLANK (1u << 0)
#define AVIVO_PCLK_CRTC1_CNTL 0x480u
#define AVIVO_PCLK_CRTC2_CNTL 0x484u

#define RADEON_P1PLL_REF_DIV 0x44Cu
#define RADEON_P1PLL_FB_DIV 0x450u
#define RADEON_P2PLL_REF_DIV 0x48Cu
#define RADEON_P2PLL_FB_DIV 0x490u
#define RADEON_P1PLL_REF_DIV_MASK 0x000003FFu
#define RADEON_P2PLL_REF_DIV_MASK 0x000003FFu
#define RADEON_P1PLL_FB_DIV_MASK 0x000007FFu
#define RADEON_P1PLL_POST_DIV_MASK 0x00070000u
#define RADEON_P1PLL_POST_DIV_SHIFT 16
#define RADEON_P2PLL_FB_DIV_MASK 0x000007FFu
#define RADEON_P2PLL_POST_DIV_MASK 0x00070000u
#define RADEON_P2PLL_POST_DIV_SHIFT 16

static int g_radeon_modeset_enabled = 1;

void kernel_drm_radeon_enable_modeset(void) {
    g_radeon_modeset_enabled = 1;
}

struct kernel_drm_radeon_probe_info {
    uint16_t command;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;
    uint32_t rom_bar;
    const char *family_name;
    const char *asic_name;
    int modeset_candidate;
};

struct kernel_drm_radeon_mmio_snapshot {
    uint32_t reg00;
    uint32_t reg04;
    uint32_t reg08;
    uint32_t reg0c;
};

struct kernel_drm_radeon_vga_snapshot {
    uint8_t misc_output;
    uint8_t crtc_htotal;
    uint8_t crtc_hdisplay_end;
    uint8_t crtc_vtotal;
    uint8_t crtc_overflow;
    uint8_t crtc_vdisplay_end;
    uint8_t crtc_mode_control;
};

struct kernel_drm_radeon_r6xx_display_snapshot {
    uint32_t d1_crtc_h_total;
    uint32_t d1_crtc_h_blank_start_end;
    uint32_t d1_crtc_control;
    uint32_t d1_crtc_blank_control;
    uint32_t d1_grph_enable;
    uint32_t d1_grph_primary_surface_address;
    uint32_t d1_cur_control;
    uint32_t d1_cur_size;
    uint32_t d1_mode_vline_status;
    uint32_t d1_mode_viewport_start;
    uint32_t d1_mode_viewport_size;
    uint32_t d2_crtc_h_total;
    uint32_t d2_crtc_h_blank_start_end;
    uint32_t d2_crtc_control;
    uint32_t d2_crtc_blank_control;
    uint32_t d2_grph_enable;
    uint32_t d2_grph_primary_surface_address;
    uint32_t d2_mode_vline_status;
    uint32_t d2_mode_viewport_start;
    uint32_t d2_mode_viewport_size;
};

struct kernel_drm_radeon_r6xx_display_analysis {
    unsigned int live_registers;
    int display_block_responding;
    int crtc0_programmed;
    int crtc1_programmed;
    int grph0_programmed;
    int grph1_programmed;
    int viewport0_programmed;
    int viewport1_programmed;
    int cursor0_programmed;
};

struct kernel_drm_radeon_mode_plan {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t frame_bytes;
    uint8_t bpp;
    uint32_t h_total;
    uint32_t h_blank_start_end;
    uint32_t h_sync_a;
    uint32_t v_total;
    uint32_t v_blank_start_end;
    uint32_t v_sync_a;
    uint32_t pixel_clock;
};

struct kernel_drm_radeon_d1_stage_snapshot {
    uint32_t master_update_lock;
    uint32_t master_update_mode;
    uint32_t crtc_update_lock;
    uint32_t crtc_h_total;
    uint32_t crtc_h_blank_start_end;
    uint32_t crtc_h_sync_a;
    uint32_t crtc_v_total;
    uint32_t crtc_v_blank_start_end;
    uint32_t crtc_v_sync_a;
    uint32_t crtc_control;
    uint32_t crtc_blank_control;
    uint32_t grph_update;
    uint32_t grph_enable;
    uint32_t grph_control;
    uint32_t grph_primary_surface_address;
    uint32_t grph_secondary_surface_address;
    uint32_t grph_pitch;
    uint32_t grph_x_start;
    uint32_t grph_y_start;
    uint32_t grph_x_end;
    uint32_t grph_y_end;
    uint32_t cur_control;
    uint32_t cur_size;
    uint32_t viewport_start;
    uint32_t viewport_size;
};

struct kernel_drm_radeon_stage_result {
    struct kernel_drm_radeon_d1_stage_snapshot before;
    int stage_status;
    int restore_status;
    struct kernel_drm_radeon_d1_stage_snapshot staged;
    struct kernel_drm_radeon_d1_stage_snapshot committed;
    struct kernel_drm_radeon_d1_stage_snapshot restored;
};

#define VGA_MISC_OUTPUT_READ 0x3CCu
#define VGA_CRTC_COLOR_INDEX 0x3D4u
#define VGA_CRTC_COLOR_DATA 0x3D5u
#define VGA_CRTC_MONO_INDEX 0x3B4u
#define VGA_CRTC_MONO_DATA 0x3B5u

static int g_kernel_drm_radeon_last_commit_valid = 0;
static struct kernel_drm_candidate g_kernel_drm_radeon_last_commit_candidate;
static struct kernel_drm_radeon_stage_result g_kernel_drm_radeon_last_commit_stage;
static struct bootinfo_vesa g_kernel_drm_radeon_last_commit_boot_vesa;
static uint32_t g_kernel_drm_radeon_last_commit_boot_flags = 0u;

static const char *kernel_drm_radeon_family_name(uint16_t device_id) {
    if (device_id >= 0x4144u && device_id <= 0x4179u) {
        return "R300/R350";
    }
    if (device_id >= 0x5548u && device_id <= 0x5557u) {
        return "R423/R430";
    }
    if (device_id >= 0x5B60u && device_id <= 0x5B7Fu) {
        return "RV370/RV380";
    }
    if (device_id >= 0x5D48u && device_id <= 0x5D57u) {
        return "RV410";
    }
    if (device_id >= 0x7100u && device_id <= 0x71FFu) {
        return "R520/RV530/RV560/R580";
    }
    if (device_id >= 0x7910u && device_id <= 0x791Fu) {
        return "RS690/RS740";
    }
    if (device_id >= 0x7930u && device_id <= 0x793Fu) {
        return "RS600/RS690";
    }
    if (device_id >= 0x94C0u && device_id <= 0x94DFu) {
        return "RV610";
    }
    if (device_id >= 0x9580u && device_id <= 0x9599u) {
        return "RV630";
    }
    if (device_id >= 0x9500u && device_id <= 0x9519u) {
        return "RV670";
    }
    if (device_id >= 0x9610u && device_id <= 0x9616u) {
        return "RS780/RS880";
    }
    if (device_id >= 0x9540u && device_id <= 0x954Fu) {
        return "RV710";
    }
    if (device_id >= 0x9490u && device_id <= 0x949Fu) {
        return "RV730";
    }
    if (device_id >= 0x9440u && device_id <= 0x9462u) {
        return "RV770";
    }
    if (device_id >= 0x68B8u && device_id <= 0x68DFu) {
        return "Juniper";
    }
    if (device_id >= 0x68E0u && device_id <= 0x68FFu) {
        return "Cedar";
    }
    if (device_id >= 0x68A0u && device_id <= 0x68BFu) {
        return "Redwood";
    }
    if (device_id >= 0x6898u && device_id <= 0x689Fu) {
        return "Cypress";
    }
    return "unknown";
}

static const char *kernel_drm_radeon_asic_name(uint16_t device_id) {
    if (device_id >= 0x4144u && device_id <= 0x4179u) {
        return "r3xx";
    }
    if (device_id >= 0x5548u && device_id <= 0x5557u) {
        return "r4xx";
    }
    if (device_id >= 0x7100u && device_id <= 0x71FFu) {
        return "r5xx";
    }
    if ((device_id >= 0x7910u && device_id <= 0x791Fu) ||
        (device_id >= 0x7930u && device_id <= 0x793Fu) ||
        (device_id >= 0x94C0u && device_id <= 0x94DFu) ||
        (device_id >= 0x9580u && device_id <= 0x9599u) ||
        (device_id >= 0x9500u && device_id <= 0x9519u) ||
        (device_id >= 0x9610u && device_id <= 0x9616u)) {
        return "r6xx";
    }
    if ((device_id >= 0x9540u && device_id <= 0x954Fu) ||
        (device_id >= 0x9490u && device_id <= 0x949Fu) ||
        (device_id >= 0x9440u && device_id <= 0x9462u)) {
        return "r7xx";
    }
    if ((device_id >= 0x6898u && device_id <= 0x689Fu) ||
        (device_id >= 0x68A0u && device_id <= 0x68BFu) ||
        (device_id >= 0x68B8u && device_id <= 0x68DFu) ||
        (device_id >= 0x68E0u && device_id <= 0x68FFu)) {
        return "evergreen";
    }
    return "unknown";
}

static int kernel_drm_radeon_is_modeset_candidate(uint16_t device_id) {
    if ((device_id >= 0x7910u && device_id <= 0x791Fu) ||
        (device_id >= 0x7930u && device_id <= 0x793Fu) ||
        (device_id >= 0x94C0u && device_id <= 0x94DFu) ||
        (device_id >= 0x9580u && device_id <= 0x9599u) ||
        (device_id >= 0x9500u && device_id <= 0x9519u) ||
        (device_id >= 0x9610u && device_id <= 0x9616u)) {
        return 1;
    }
    return 0;
}

static uint32_t kernel_drm_radeon_mmio_read32(const struct kernel_drm_candidate *candidate,
                                              uint32_t reg);
static void kernel_drm_radeon_mmio_write32(const struct kernel_drm_candidate *candidate,
                                           uint32_t reg, uint32_t value);

static void kernel_drm_radeon_prepare_for_bios_modeset(void) {
    const struct kernel_drm_radeon_stage_result *stage;
    const struct kernel_drm_candidate *cand;
    struct kernel_drm_candidate local_cand;
    struct kernel_drm_radeon_d1_stage_snapshot snap;
    unsigned int timeout;

    if (!g_kernel_drm_radeon_last_commit_valid) {
        return;
    }

    stage = &g_kernel_drm_radeon_last_commit_stage;
    cand = &g_kernel_drm_radeon_last_commit_candidate;
    local_cand = *cand;
    snap = stage->before;

    kernel_drm_radeon_mmio_write32(&local_cand, AVIVO_D1GRPH_ENABLE, 0u);
    kernel_drm_radeon_mmio_write32(&local_cand, AVIVO_D2GRPH_ENABLE, 0u);

    timeout = 1000000u;
    while (timeout > 0u) {
        if ((kernel_drm_radeon_mmio_read32(&local_cand, AVIVO_D1CRTC_STATUS) & AVIVO_D1CRTC_V_BLANK) != 0u) {
            break;
        }
        --timeout;
    }

    timeout = 1000000u;
    while (timeout > 0u) {
        if ((kernel_drm_radeon_mmio_read32(&local_cand, AVIVO_D2CRTC_STATUS) & AVIVO_D1CRTC_V_BLANK) != 0u) {
            break;
        }
        --timeout;
    }

    kernel_drm_radeon_mmio_write32(&local_cand, AVIVO_D1CRTC_CONTROL,
                                   snap.crtc_control & ~AVIVO_CRTC_EN);
    kernel_drm_radeon_mmio_write32(&local_cand, AVIVO_D2CRTC_CONTROL, 0u);

    kernel_drm_radeon_mmio_write32(&local_cand, AVIVO_D1CRTC_BLANK_CONTROL, 1u);
    kernel_drm_radeon_mmio_write32(&local_cand, AVIVO_D2CRTC_BLANK_CONTROL, 1u);
}

static int kernel_drm_radeon_is_r6xx_asic(uint16_t device_id) {
    return ((device_id >= 0x7910u && device_id <= 0x791Fu) ||
            (device_id >= 0x7930u && device_id <= 0x793Fu) ||
            (device_id >= 0x94C0u && device_id <= 0x94DFu) ||
            (device_id >= 0x9580u && device_id <= 0x9599u) ||
            (device_id >= 0x9500u && device_id <= 0x9519u) ||
            (device_id >= 0x9610u && device_id <= 0x9616u));
}

static uint16_t kernel_drm_radeon_vga_crtc_index_port(uint8_t misc_output) {
    return (misc_output & 0x01u) != 0u ? VGA_CRTC_COLOR_INDEX : VGA_CRTC_MONO_INDEX;
}

static uint16_t kernel_drm_radeon_vga_crtc_data_port(uint8_t misc_output) {
    return (misc_output & 0x01u) != 0u ? VGA_CRTC_COLOR_DATA : VGA_CRTC_MONO_DATA;
}

static uint8_t kernel_drm_radeon_vga_read_crtc(uint8_t misc_output, uint8_t index) {
    outb(kernel_drm_radeon_vga_crtc_index_port(misc_output), index);
    return inb(kernel_drm_radeon_vga_crtc_data_port(misc_output));
}

static uintptr_t kernel_drm_radeon_mmio_ptr(const struct kernel_drm_candidate *candidate,
                                            uint32_t reg) {
    if (candidate == 0 || candidate->mmio_base == (uintptr_t)0) {
        return (uintptr_t)0;
    }
    if ((size_t)reg + sizeof(uint32_t) > candidate->mmio_size) {
        return (uintptr_t)0;
    }
    return candidate->mmio_base + (uintptr_t)reg;
}

static uint32_t kernel_drm_radeon_mmio_read32(const struct kernel_drm_candidate *candidate,
                                              uint32_t reg) {
    volatile uint32_t *ptr =
        (volatile uint32_t *)(uintptr_t)kernel_drm_radeon_mmio_ptr(candidate, reg);

    if (ptr == 0) {
        return 0u;
    }
    return *ptr;
}

static void kernel_drm_radeon_mmio_write32(const struct kernel_drm_candidate *candidate,
                                           uint32_t reg,
                                           uint32_t value) {
    volatile uint32_t *ptr =
        (volatile uint32_t *)(uintptr_t)kernel_drm_radeon_mmio_ptr(candidate, reg);

    if (ptr == 0) {
        return;
    }
    *ptr = value;
}

static int kernel_drm_radeon_snapshot_mmio(const struct kernel_drm_candidate *candidate,
                                           struct kernel_drm_radeon_mmio_snapshot *snapshot_out) {
    if (candidate == 0 || snapshot_out == 0 || candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }
    if (candidate->mmio_size < 0x10u) {
        return -1;
    }

    snapshot_out->reg00 = kernel_drm_radeon_mmio_read32(candidate, 0x00u);
    snapshot_out->reg04 = kernel_drm_radeon_mmio_read32(candidate, 0x04u);
    snapshot_out->reg08 = kernel_drm_radeon_mmio_read32(candidate, 0x08u);
    snapshot_out->reg0c = kernel_drm_radeon_mmio_read32(candidate, 0x0Cu);
    return 0;
}

static int kernel_drm_radeon_snapshot_vga(struct kernel_drm_radeon_vga_snapshot *snapshot_out) {
    uint8_t misc_output;

    if (snapshot_out == 0) {
        return -1;
    }

    misc_output = inb(VGA_MISC_OUTPUT_READ);
    snapshot_out->misc_output = misc_output;
    snapshot_out->crtc_htotal = kernel_drm_radeon_vga_read_crtc(misc_output, 0x00u);
    snapshot_out->crtc_hdisplay_end = kernel_drm_radeon_vga_read_crtc(misc_output, 0x01u);
    snapshot_out->crtc_vtotal = kernel_drm_radeon_vga_read_crtc(misc_output, 0x06u);
    snapshot_out->crtc_overflow = kernel_drm_radeon_vga_read_crtc(misc_output, 0x07u);
    snapshot_out->crtc_vdisplay_end = kernel_drm_radeon_vga_read_crtc(misc_output, 0x12u);
    snapshot_out->crtc_mode_control = kernel_drm_radeon_vga_read_crtc(misc_output, 0x17u);
    return 0;
}

static int kernel_drm_radeon_snapshot_r6xx_display(
    const struct kernel_drm_candidate *candidate,
    struct kernel_drm_radeon_r6xx_display_snapshot *snapshot_out) {
    if (candidate == 0 || snapshot_out == 0 || candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }
    if (!kernel_drm_radeon_is_r6xx_asic(candidate->pci.device_id)) {
        return -1;
    }
    if (candidate->mmio_size < (size_t)(AVIVO_D2MODE_VIEWPORT_SIZE + sizeof(uint32_t))) {
        return -1;
    }

    snapshot_out->d1_crtc_h_total =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_H_TOTAL);
    snapshot_out->d1_crtc_h_blank_start_end =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_H_BLANK_START_END);
    snapshot_out->d1_crtc_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_CONTROL);
    snapshot_out->d1_crtc_blank_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_BLANK_CONTROL);
    snapshot_out->d1_grph_enable =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_ENABLE);
    snapshot_out->d1_grph_primary_surface_address =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS);
    snapshot_out->d1_cur_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CUR_CONTROL);
    snapshot_out->d1_cur_size =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CUR_SIZE);
    snapshot_out->d1_mode_vline_status =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1MODE_VLINE_STATUS);
    snapshot_out->d1_mode_viewport_start =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1MODE_VIEWPORT_START);
    snapshot_out->d1_mode_viewport_size =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1MODE_VIEWPORT_SIZE);
    snapshot_out->d2_crtc_h_total =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2CRTC_H_TOTAL);
    snapshot_out->d2_crtc_h_blank_start_end =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2CRTC_H_BLANK_START_END);
    snapshot_out->d2_crtc_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2CRTC_CONTROL);
    snapshot_out->d2_crtc_blank_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2CRTC_BLANK_CONTROL);
    snapshot_out->d2_grph_enable =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2GRPH_ENABLE);
    snapshot_out->d2_grph_primary_surface_address =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2GRPH_PRIMARY_SURFACE_ADDRESS);
    snapshot_out->d2_mode_vline_status =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2MODE_VLINE_STATUS);
    snapshot_out->d2_mode_viewport_start =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2MODE_VIEWPORT_START);
    snapshot_out->d2_mode_viewport_size =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D2MODE_VIEWPORT_SIZE);
    return 0;
}

static unsigned int kernel_drm_radeon_count_live_registers(const uint32_t *registers,
                                                           size_t count) {
    size_t i;
    unsigned int live = 0;

    if (registers == 0) {
        return 0;
    }

    for (i = 0; i < count; ++i) {
        if (registers[i] != 0u && registers[i] != 0xFFFFFFFFu) {
            live++;
        }
    }
    return live;
}

static void kernel_drm_radeon_analyze_r6xx_display(
    const struct kernel_drm_radeon_r6xx_display_snapshot *snapshot,
    struct kernel_drm_radeon_r6xx_display_analysis *analysis_out) {
    const uint32_t regs[] = {
        snapshot != 0 ? snapshot->d1_crtc_h_total : 0u,
        snapshot != 0 ? snapshot->d1_crtc_h_blank_start_end : 0u,
        snapshot != 0 ? snapshot->d1_crtc_control : 0u,
        snapshot != 0 ? snapshot->d1_crtc_blank_control : 0u,
        snapshot != 0 ? snapshot->d1_grph_enable : 0u,
        snapshot != 0 ? snapshot->d1_grph_primary_surface_address : 0u,
        snapshot != 0 ? snapshot->d1_cur_control : 0u,
        snapshot != 0 ? snapshot->d1_cur_size : 0u,
        snapshot != 0 ? snapshot->d1_mode_vline_status : 0u,
        snapshot != 0 ? snapshot->d1_mode_viewport_start : 0u,
        snapshot != 0 ? snapshot->d1_mode_viewport_size : 0u,
        snapshot != 0 ? snapshot->d2_crtc_h_total : 0u,
        snapshot != 0 ? snapshot->d2_crtc_h_blank_start_end : 0u,
        snapshot != 0 ? snapshot->d2_crtc_control : 0u,
        snapshot != 0 ? snapshot->d2_crtc_blank_control : 0u,
        snapshot != 0 ? snapshot->d2_grph_enable : 0u,
        snapshot != 0 ? snapshot->d2_grph_primary_surface_address : 0u,
        snapshot != 0 ? snapshot->d2_mode_vline_status : 0u,
        snapshot != 0 ? snapshot->d2_mode_viewport_start : 0u,
        snapshot != 0 ? snapshot->d2_mode_viewport_size : 0u,
    };

    if (analysis_out == 0) {
        return;
    }

    *analysis_out = (struct kernel_drm_radeon_r6xx_display_analysis){0};
    if (snapshot == 0) {
        return;
    }

    analysis_out->live_registers = kernel_drm_radeon_count_live_registers(regs,
                                                                           sizeof(regs) /
                                                                               sizeof(regs[0]));
    analysis_out->display_block_responding = analysis_out->live_registers != 0u;
    analysis_out->crtc0_programmed =
        snapshot->d1_crtc_h_total != 0u ||
        snapshot->d1_crtc_h_blank_start_end != 0u ||
        snapshot->d1_crtc_control != 0u ||
        snapshot->d1_crtc_blank_control != 0u;
    analysis_out->crtc1_programmed =
        snapshot->d2_crtc_h_total != 0u ||
        snapshot->d2_crtc_h_blank_start_end != 0u ||
        snapshot->d2_crtc_control != 0u ||
        snapshot->d2_crtc_blank_control != 0u;
    analysis_out->grph0_programmed =
        snapshot->d1_grph_enable != 0u ||
        snapshot->d1_grph_primary_surface_address != 0u;
    analysis_out->grph1_programmed =
        snapshot->d2_grph_enable != 0u ||
        snapshot->d2_grph_primary_surface_address != 0u;
    analysis_out->viewport0_programmed =
        snapshot->d1_mode_viewport_start != 0u ||
        snapshot->d1_mode_viewport_size != 0u;
    analysis_out->viewport1_programmed =
        snapshot->d2_mode_viewport_start != 0u ||
        snapshot->d2_mode_viewport_size != 0u;
    analysis_out->cursor0_programmed =
        snapshot->d1_cur_control != 0u || snapshot->d1_cur_size != 0u;
}

static uint32_t kernel_drm_radeon_align_pitch(uint32_t width) {
    return (width + 63u) & ~63u;
}

static int kernel_drm_radeon_mode_supported(uint32_t width, uint32_t height) {
    if (width == 640u && height == 480u) {
        return 1;
    }
    if (width == 800u && height == 600u) {
        return 1;
    }
    if (width == 1024u && height == 768u) {
        return 1;
    }
    if (width == 1360u && height == 768u) {
        return 1;
    }
    if (width == 1366u && height == 768u) {
        return 1;
    }
    if (width == 1920u && height == 1080u) {
        return 1;
    }
    return 0;
}

struct kernel_drm_radeon_vesa_timing {
    uint32_t width;
    uint32_t height;
    uint32_t h_front_porch;
    uint32_t h_sync_width;
    uint32_t h_total;
    uint32_t v_front_porch;
    uint32_t v_sync_width;
    uint32_t v_total;
    uint32_t pixel_clock;
};

static const struct kernel_drm_radeon_vesa_timing g_kernel_drm_radeon_vesa_timings[] = {
    {640, 480, 16, 96, 800, 10, 2, 525, 25175},
    {800, 600, 40, 128, 1056, 1, 4, 628, 40000},
    {1024, 768, 24, 136, 1344, 3, 6, 806, 65000},
    {1360, 768, 64, 112, 1728, 3, 6, 812, 85500},
    {1366, 768, 70, 143, 1792, 3, 6, 812, 87250},
    {1920, 1080, 88, 44, 2200, 4, 5, 1125, 148500},
};

static int kernel_drm_radeon_lookup_timing(uint32_t width, uint32_t height,
                                           struct kernel_drm_radeon_vesa_timing *timing_out) {
    size_t i;

    if (timing_out == 0) {
        return -1;
    }

    for (i = 0u; i < (sizeof(g_kernel_drm_radeon_vesa_timings) /
                      sizeof(g_kernel_drm_radeon_vesa_timings[0])); ++i) {
        if (g_kernel_drm_radeon_vesa_timings[i].width == width &&
            g_kernel_drm_radeon_vesa_timings[i].height == height) {
            *timing_out = g_kernel_drm_radeon_vesa_timings[i];
            return 0;
        }
    }
    return -1;
}

static int kernel_drm_radeon_build_mode_plan(uint32_t width,
                                             uint32_t height,
                                             struct kernel_drm_radeon_mode_plan *plan_out) {
    uint32_t pitch;
    uint32_t frame_bytes;
    struct kernel_drm_radeon_vesa_timing timing;
    uint32_t hactive;
    uint32_t hblank_start;
    uint32_t hblank_end;
    uint32_t hsync_start;
    uint32_t hsync_end;
    uint32_t vactive;
    uint32_t vblank_start;
    uint32_t vblank_end;
    uint32_t vsync_start;
    uint32_t vsync_end;

    if (plan_out == 0) {
        return -1;
    }
    *plan_out = (struct kernel_drm_radeon_mode_plan){0};

    if (width < GRAPHICS_MIN_WIDTH || width > GRAPHICS_MAX_WIDTH ||
        height < GRAPHICS_MIN_HEIGHT || height > GRAPHICS_MAX_HEIGHT) {
        return -1;
    }
    if (!kernel_drm_radeon_mode_supported(width, height)) {
        return -1;
    }

    if (kernel_drm_radeon_lookup_timing(width, height, &timing) != 0) {
        return -1;
    }

    pitch = kernel_drm_radeon_align_pitch(width);
    if (pitch < width) {
        return -1;
    }
    if (height != 0u && pitch > (0xFFFFFFFFu / height)) {
        return -1;
    }
    frame_bytes = pitch * height;
    if (frame_bytes == 0u) {
        return -1;
    }

    hactive = width;
    hblank_start = hactive;
    hblank_end = timing.h_total - 1u;
    hsync_start = hactive + timing.h_front_porch;
    hsync_end = hsync_start + timing.h_sync_width;

    vactive = height;
    vblank_start = vactive;
    vblank_end = timing.v_total - 1u;
    vsync_start = vactive + timing.v_front_porch;
    vsync_end = vsync_start + timing.v_sync_width;

    plan_out->width = width;
    plan_out->height = height;
    plan_out->pitch = pitch;
    plan_out->frame_bytes = frame_bytes;
    plan_out->bpp = GRAPHICS_BPP;
    plan_out->h_total = timing.h_total - 1u;
    plan_out->h_blank_start_end = (hblank_end << 16u) | hblank_start;
    plan_out->h_sync_a = (hsync_end << 16u) | hsync_start;
    plan_out->v_total = timing.v_total - 1u;
    plan_out->v_blank_start_end = (vblank_end << 16u) | vblank_start;
    plan_out->v_sync_a = (vsync_end << 16u) | vsync_start;
    plan_out->pixel_clock = timing.pixel_clock;
    return 0;
}

static int kernel_drm_radeon_boot_fb_addr(uint32_t *fb_addr_out) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;

    if (fb_addr_out == 0) {
        return -1;
    }
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    if ((bootinfo->flags & BOOTINFO_FLAG_VESA_VALID) != 0u &&
        bootinfo->vesa.fb_addr >= GRAPHICS_MIN_FB_ADDR) {
        *fb_addr_out = bootinfo->vesa.fb_addr;
        return 0;
    }
    if (bootinfo->vesa.fb_addr >= GRAPHICS_MIN_FB_ADDR) {
        *fb_addr_out = bootinfo->vesa.fb_addr;
        return 0;
    }
    return -1;
}

static void kernel_drm_radeon_clear_mode_out(struct video_mode *mode_out) {
    if (mode_out == 0) {
        return;
    }
    *mode_out = (struct video_mode){0};
}

static int kernel_drm_radeon_fill_mode_out(uint32_t fb_addr,
                                           const struct kernel_drm_radeon_mode_plan *plan,
                                           struct video_mode *mode_out) {
    if (plan == 0 || mode_out == 0 || fb_addr < GRAPHICS_MIN_FB_ADDR) {
        return -1;
    }

    mode_out->fb_addr = fb_addr;
    mode_out->width = plan->width;
    mode_out->height = plan->height;
    mode_out->pitch = plan->pitch;
    mode_out->bpp = plan->bpp;
    return 0;
}

static int kernel_drm_radeon_snapshot_boot_mode(struct bootinfo_vesa *vesa_out,
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

static int kernel_drm_radeon_boot_mode_matches(const struct bootinfo_vesa *expected_vesa,
                                                uint32_t expected_flags,
                                                const struct bootinfo_vesa *actual_vesa,
                                                uint32_t actual_flags) {
    if (expected_vesa == 0 || actual_vesa == 0) {
        return 0;
    }

    return expected_vesa->mode == actual_vesa->mode &&
           expected_vesa->fb_addr == actual_vesa->fb_addr &&
           expected_vesa->pitch == actual_vesa->pitch &&
           expected_vesa->width == actual_vesa->width &&
           expected_vesa->height == actual_vesa->height &&
           expected_vesa->bpp == actual_vesa->bpp &&
           (expected_flags & BOOTINFO_FLAG_VESA_VALID) ==
               (actual_flags & BOOTINFO_FLAG_VESA_VALID);
}

static void kernel_drm_radeon_log_boot_mode_transition(
    const char *reason,
    const struct bootinfo_vesa *expected_vesa,
    uint32_t expected_flags,
    const struct bootinfo_vesa *actual_vesa,
    uint32_t actual_flags) {
    if (expected_vesa == 0 || actual_vesa == 0) {
        return;
    }

    kernel_debug_printf("radeon: %s bootinfo expected mode=%x fb=%x pitch=%x %ux%u bpp=%u valid=%u | actual mode=%x fb=%x pitch=%x %ux%u bpp=%u valid=%u\n",
                        reason != 0 ? reason : "bootinfo",
                        expected_vesa->mode,
                        expected_vesa->fb_addr,
                        expected_vesa->pitch,
                        (unsigned int)expected_vesa->width,
                        (unsigned int)expected_vesa->height,
                        (unsigned int)expected_vesa->bpp,
                        (unsigned int)((expected_flags & BOOTINFO_FLAG_VESA_VALID) != 0u),
                        actual_vesa->mode,
                        actual_vesa->fb_addr,
                        actual_vesa->pitch,
                        (unsigned int)actual_vesa->width,
                        (unsigned int)actual_vesa->height,
                        (unsigned int)actual_vesa->bpp,
                        (unsigned int)((actual_flags & BOOTINFO_FLAG_VESA_VALID) != 0u));
}

static int kernel_drm_radeon_verify_boot_mode(const char *reason,
                                              const struct bootinfo_vesa *expected_vesa,
                                              uint32_t expected_flags) {
    struct bootinfo_vesa actual_vesa;
    uint32_t actual_flags;

    if (kernel_drm_radeon_snapshot_boot_mode(&actual_vesa, &actual_flags) != 0) {
        kernel_debug_printf("radeon: %s bootinfo readback failed\n",
                            reason != 0 ? reason : "bootinfo");
        return -1;
    }
    if (!kernel_drm_radeon_boot_mode_matches(expected_vesa,
                                             expected_flags,
                                             &actual_vesa,
                                             actual_flags)) {
        kernel_drm_radeon_log_boot_mode_transition(reason,
                                                   expected_vesa,
                                                   expected_flags,
                                                   &actual_vesa,
                                                   actual_flags);
        return -1;
    }
    return 0;
}

static int kernel_drm_radeon_publish_boot_mode(uint16_t mode_id,
                                               const struct video_mode *mode) {
    volatile struct bootinfo *bootinfo;
    struct bootinfo_vesa expected_vesa;
    uint32_t expected_flags;

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
    expected_vesa = bootinfo->vesa;
    expected_flags = bootinfo->flags;
    return kernel_drm_radeon_verify_boot_mode("modeset-commit publish",
                                              &expected_vesa,
                                              expected_flags);
}

static int kernel_drm_radeon_restore_boot_mode(const struct bootinfo_vesa *vesa,
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
    return kernel_drm_radeon_verify_boot_mode("modeset-commit revert",
                                              vesa,
                                              flags);
}

static int kernel_drm_radeon_restore_boot_mode_and_log(const struct bootinfo_vesa *vesa,
                                                       uint32_t flags,
                                                       const char *reason) {
    if (kernel_drm_radeon_restore_boot_mode(vesa, flags) != 0) {
        kernel_debug_printf("radeon: %s bootinfo rollback failed\n",
                            reason != 0 ? reason : "bootinfo");
        return -1;
    }
    return 0;
}

static void kernel_drm_radeon_remember_last_commit(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_radeon_stage_result *stage,
    const struct bootinfo_vesa *boot_vesa,
    uint32_t boot_flags) {
    if (candidate == 0 || stage == 0 || boot_vesa == 0) {
        g_kernel_drm_radeon_last_commit_valid = 0;
        return;
    }

    g_kernel_drm_radeon_last_commit_candidate = *candidate;
    g_kernel_drm_radeon_last_commit_stage = *stage;
    g_kernel_drm_radeon_last_commit_boot_vesa = *boot_vesa;
    g_kernel_drm_radeon_last_commit_boot_flags = boot_flags;
    g_kernel_drm_radeon_last_commit_valid = 1;
}

void kernel_drm_radeon_forget_last_commit(void) {
    g_kernel_drm_radeon_last_commit_valid = 0;
    g_kernel_drm_radeon_last_commit_candidate = (struct kernel_drm_candidate){0};
    g_kernel_drm_radeon_last_commit_stage = (struct kernel_drm_radeon_stage_result){0};
    g_kernel_drm_radeon_last_commit_boot_vesa = (struct bootinfo_vesa){0};
    g_kernel_drm_radeon_last_commit_boot_flags = 0u;
}

static int kernel_drm_radeon_boot_fb_in_aperture(
    const struct kernel_drm_candidate *candidate,
    uint32_t fb_addr,
    const struct kernel_drm_radeon_mode_plan *plan) {
    uintptr_t fb_end;
    uintptr_t aperture_end;

    if (candidate == 0 || plan == 0) {
        return 0;
    }
    if (candidate->fb_base < GRAPHICS_MIN_FB_ADDR || candidate->fb_size == 0u) {
        return 0;
    }
    if (fb_addr < (uint32_t)candidate->fb_base) {
        return 0;
    }

    fb_end = (uintptr_t)fb_addr + (uintptr_t)plan->frame_bytes;
    aperture_end = candidate->fb_base + candidate->fb_size;
    if (fb_end < (uintptr_t)fb_addr) {
        return 0;
    }
    return fb_end <= aperture_end;
}

static int kernel_drm_radeon_preflight_r6xx(
    const struct kernel_drm_candidate *candidate,
    uint32_t width,
    uint32_t height,
    struct kernel_drm_radeon_mode_plan *plan_out,
    struct kernel_drm_radeon_r6xx_display_analysis *analysis_out) {
    struct kernel_drm_radeon_r6xx_display_snapshot snapshot;

    if (candidate == 0 || plan_out == 0 || analysis_out == 0) {
        return -1;
    }
    *analysis_out = (struct kernel_drm_radeon_r6xx_display_analysis){0};

    if (!kernel_drm_radeon_is_modeset_candidate(candidate->pci.device_id)) {
        kernel_debug_printf("radeon: preflight unsupported candidate dev=%x family=%s asic=%s\n",
                            candidate->pci.device_id,
                            kernel_drm_radeon_family_name(candidate->pci.device_id),
                            kernel_drm_radeon_asic_name(candidate->pci.device_id));
        return -1;
    }
    if (!kernel_drm_radeon_is_r6xx_asic(candidate->pci.device_id)) {
        kernel_debug_printf("radeon: preflight currently targets r6xx only dev=%x asic=%s\n",
                            candidate->pci.device_id,
                            kernel_drm_radeon_asic_name(candidate->pci.device_id));
        return -1;
    }
    if (candidate->mmio_base == (uintptr_t)0 ||
        candidate->mmio_size < (size_t)(AVIVO_D2MODE_VIEWPORT_SIZE + sizeof(uint32_t))) {
        kernel_debug_printf("radeon: preflight missing r6xx display window dev=%x mmio=%x size=%x\n",
                            candidate->pci.device_id,
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->mmio_size);
        return -1;
    }
    if (kernel_drm_radeon_build_mode_plan(width, height, plan_out) != 0) {
        kernel_debug_printf("radeon: preflight unsupported mode %dx%d for first r6xx path\n",
                            (int)width,
                            (int)height);
        return -1;
    }
    if (candidate->fb_base < GRAPHICS_MIN_FB_ADDR) {
        kernel_debug_printf("radeon: preflight invalid fb base dev=%x fb=%x\n",
                            candidate->pci.device_id,
                            (uint32_t)candidate->fb_base);
        return -1;
    }
    if (candidate->fb_size != 0u && plan_out->frame_bytes > candidate->fb_size) {
        kernel_debug_printf("radeon: preflight aperture too small required=%x available=%x for %dx%d\n",
                            plan_out->frame_bytes,
                            (uint32_t)candidate->fb_size,
                            (int)width,
                            (int)height);
        return -1;
    }
    if (kernel_drm_radeon_snapshot_r6xx_display(candidate, &snapshot) != 0) {
        kernel_debug_printf("radeon: preflight display snapshot unavailable dev=%x\n",
                            candidate->pci.device_id);
        return -1;
    }

    kernel_drm_radeon_analyze_r6xx_display(&snapshot, analysis_out);
    if (!analysis_out->display_block_responding) {
        kernel_debug_printf("radeon: preflight display block not responding dev=%x live_regs=%u\n",
                            candidate->pci.device_id,
                            analysis_out->live_registers);
        return -1;
    }
    return 0;
}

static int kernel_drm_radeon_compute_pll(uint32_t pixel_clock,
                                         uint32_t *ref_div_out,
                                         uint32_t *fb_div_out,
                                         uint32_t *post_div_out) {
    uint32_t ref_clock;
    uint32_t best_fb_div;
    uint32_t best_post_div;
    uint32_t best_ref_div;
    uint32_t best_error;
    uint32_t ref_div;
    uint32_t fb_div;
    uint32_t post_div;
    uint32_t vco;
    uint32_t error;
    int found;

    if (ref_div_out == 0 || fb_div_out == 0 || post_div_out == 0) {
        return -1;
    }

    ref_clock = 27000u;
    best_fb_div = 1u;
    best_post_div = 1u;
    best_ref_div = 1u;
    best_error = 0xFFFFFFFFu;
    found = 0;

    for (ref_div = 1u; ref_div <= 0x3FFu; ++ref_div) {
        for (post_div = 1u; post_div <= 7u; ++post_div) {
            for (fb_div = 1u; fb_div <= 0x7FFu; ++fb_div) {
                vco = (ref_clock / ref_div) * fb_div;
                if (vco < 500000u || vco > 1000000u) {
                    continue;
                }
                if (vco / post_div < pixel_clock) {
                    continue;
                }
                error = vco / post_div - pixel_clock;
                if (error < best_error) {
                    best_error = error;
                    best_fb_div = fb_div;
                    best_post_div = post_div;
                    best_ref_div = ref_div;
                    found = 1;
                    if (error == 0u) {
                        goto done;
                    }
                }
            }
        }
    }

done:
    if (!found) {
        return -1;
    }

    *ref_div_out = best_ref_div;
    *fb_div_out = best_fb_div;
    *post_div_out = best_post_div;
    return 0;
}

static void kernel_drm_radeon_program_d1_pll(const struct kernel_drm_candidate *candidate,
                                             const struct kernel_drm_radeon_mode_plan *plan) {
    uint32_t ref_div;
    uint32_t fb_div;
    uint32_t post_div;
    uint32_t pll_fb_val;

    if (candidate == 0 || plan == 0) {
        return;
    }

    if (kernel_drm_radeon_compute_pll(plan->pixel_clock, &ref_div, &fb_div, &post_div) != 0) {
        kernel_debug_printf("radeon: D1 PLL compute failed for pixel_clock=%u\n",
                            plan->pixel_clock);
        return;
    }

    kernel_drm_radeon_mmio_write32(candidate, RADEON_P1PLL_REF_DIV,
                                   ref_div & RADEON_P1PLL_REF_DIV_MASK);
    pll_fb_val = (fb_div & RADEON_P1PLL_FB_DIV_MASK) |
                 ((post_div << RADEON_P1PLL_POST_DIV_SHIFT) & RADEON_P1PLL_POST_DIV_MASK);
    kernel_drm_radeon_mmio_write32(candidate, RADEON_P1PLL_FB_DIV, pll_fb_val);
    kernel_drm_radeon_mmio_write32(candidate, AVIVO_PCLK_CRTC1_CNTL, 1u);
}

__attribute__((unused))
static void kernel_drm_radeon_program_d2_pll(const struct kernel_drm_candidate *candidate,
                                             const struct kernel_drm_radeon_mode_plan *plan) {
    uint32_t ref_div;
    uint32_t fb_div;
    uint32_t post_div;
    uint32_t pll_fb_val;

    if (candidate == 0 || plan == 0) {
        return;
    }

    if (kernel_drm_radeon_compute_pll(plan->pixel_clock, &ref_div, &fb_div, &post_div) != 0) {
        kernel_debug_printf("radeon: D2 PLL compute failed for pixel_clock=%u\n",
                            plan->pixel_clock);
        return;
    }

    kernel_drm_radeon_mmio_write32(candidate, RADEON_P2PLL_REF_DIV,
                                   ref_div & RADEON_P2PLL_REF_DIV_MASK);
    pll_fb_val = (fb_div & RADEON_P2PLL_FB_DIV_MASK) |
                 ((post_div << RADEON_P2PLL_POST_DIV_SHIFT) & RADEON_P2PLL_POST_DIV_MASK);
    kernel_drm_radeon_mmio_write32(candidate, RADEON_P2PLL_FB_DIV, pll_fb_val);
    kernel_drm_radeon_mmio_write32(candidate, AVIVO_PCLK_CRTC2_CNTL, 1u);
}

static int kernel_drm_radeon_snapshot_d1_stage(
    const struct kernel_drm_candidate *candidate,
    struct kernel_drm_radeon_d1_stage_snapshot *snapshot_out) {
    if (candidate == 0 || snapshot_out == 0 || candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }
    if (candidate->mmio_size < (size_t)(AVIVO_D1MODE_VIEWPORT_SIZE + sizeof(uint32_t))) {
        return -1;
    }

    snapshot_out->master_update_lock =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1MODE_MASTER_UPDATE_LOCK);
    snapshot_out->master_update_mode =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1MODE_MASTER_UPDATE_MODE);
    snapshot_out->crtc_update_lock =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_UPDATE_LOCK);
    snapshot_out->crtc_h_total =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_H_TOTAL);
    snapshot_out->crtc_h_blank_start_end =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_H_BLANK_START_END);
    snapshot_out->crtc_h_sync_a =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_H_SYNC_A);
    snapshot_out->crtc_v_total =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_V_TOTAL);
    snapshot_out->crtc_v_blank_start_end =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_V_BLANK_START_END);
    snapshot_out->crtc_v_sync_a =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_V_SYNC_A);
    snapshot_out->crtc_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_CONTROL);
    snapshot_out->crtc_blank_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CRTC_BLANK_CONTROL);
    snapshot_out->grph_update =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_UPDATE);
    snapshot_out->grph_enable =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_ENABLE);
    snapshot_out->grph_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_CONTROL);
    snapshot_out->grph_primary_surface_address =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS);
    snapshot_out->grph_secondary_surface_address =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_SECONDARY_SURFACE_ADDRESS);
    snapshot_out->grph_pitch =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_PITCH);
    snapshot_out->grph_x_start =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_X_START);
    snapshot_out->grph_y_start =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_Y_START);
    snapshot_out->grph_x_end =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_X_END);
    snapshot_out->grph_y_end =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1GRPH_Y_END);
    snapshot_out->cur_control =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CUR_CONTROL);
    snapshot_out->cur_size =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1CUR_SIZE);
    snapshot_out->viewport_start =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1MODE_VIEWPORT_START);
    snapshot_out->viewport_size =
        kernel_drm_radeon_mmio_read32(candidate, AVIVO_D1MODE_VIEWPORT_SIZE);
    return 0;
}

static uint32_t kernel_drm_radeon_expected_d1_grph_control(void) {
    return AVIVO_D1GRPH_CONTROL_DEPTH_8BPP | AVIVO_D1GRPH_CONTROL_8BPP_INDEXED;
}

static uint32_t kernel_drm_radeon_expected_d1_grph_update(
    const struct kernel_drm_radeon_d1_stage_snapshot *before) {
    if (before == 0) {
        return 0u;
    }
    return before->grph_update | AVIVO_D1GRPH_UPDATE_LOCK;
}

static uint32_t kernel_drm_radeon_expected_d1_grph_enable(void) {
    return 1u;
}

static uint32_t kernel_drm_radeon_expected_d1_master_update_lock(void) {
    return 1u;
}

static uint32_t kernel_drm_radeon_expected_d1_master_update_mode(void) {
    return 3u;
}

static uint32_t kernel_drm_radeon_expected_d1_viewport_start(void) {
    return 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_grph_x_start(void) {
    return 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_grph_y_start(void) {
    return 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_viewport_size(
    const struct kernel_drm_radeon_mode_plan *plan) {
    if (plan == 0) {
        return 0u;
    }
    return (plan->width << 16) | plan->height;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_update_lock(
    const struct kernel_drm_radeon_d1_stage_snapshot *before) {
    return before != 0 ? before->crtc_update_lock : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_h_total(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->h_total : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_h_blank_start_end(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->h_blank_start_end : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_h_sync_a(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->h_sync_a : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_v_total(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->v_total : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_v_blank_start_end(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->v_blank_start_end : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_v_sync_a(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->v_sync_a : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_control(
    const struct kernel_drm_radeon_d1_stage_snapshot *before) {
    return before != 0 ? before->crtc_control : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_crtc_blank_control(
    const struct kernel_drm_radeon_d1_stage_snapshot *before) {
    return before != 0 ? before->crtc_blank_control : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_cur_control(
    const struct kernel_drm_radeon_d1_stage_snapshot *before) {
    return before != 0 ? before->cur_control : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_cur_size(
    const struct kernel_drm_radeon_d1_stage_snapshot *before) {
    return before != 0 ? before->cur_size : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_surface_address(uint32_t fb_addr) {
    return fb_addr;
}

static uint32_t kernel_drm_radeon_expected_d1_pitch(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->pitch : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_grph_x_end(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->width : 0u;
}

static uint32_t kernel_drm_radeon_expected_d1_grph_y_end(
    const struct kernel_drm_radeon_mode_plan *plan) {
    return plan != 0 ? plan->height : 0u;
}

static int kernel_drm_radeon_d1_stage_matches(
    const struct kernel_drm_radeon_d1_stage_snapshot *snapshot,
    const struct kernel_drm_radeon_d1_stage_snapshot *before,
    uint32_t expected_grph_control,
    uint32_t expected_surface_address,
    const struct kernel_drm_radeon_mode_plan *plan) {
    uint32_t expected_grph_update;
    uint32_t expected_grph_enable;
    uint32_t expected_master_update_lock;
    uint32_t expected_master_update_mode;
    uint32_t expected_viewport_size;

    if (snapshot == 0 || before == 0 || plan == 0) {
        return 0;
    }

    expected_grph_update = kernel_drm_radeon_expected_d1_grph_update(before);
    expected_grph_enable = kernel_drm_radeon_expected_d1_grph_enable();
    expected_master_update_lock = kernel_drm_radeon_expected_d1_master_update_lock();
    expected_master_update_mode = kernel_drm_radeon_expected_d1_master_update_mode();
    expected_viewport_size = kernel_drm_radeon_expected_d1_viewport_size(plan);
    return snapshot->grph_update == expected_grph_update &&
           snapshot->master_update_lock == expected_master_update_lock &&
           snapshot->crtc_update_lock == kernel_drm_radeon_expected_d1_crtc_update_lock(before) &&
           snapshot->crtc_h_total == kernel_drm_radeon_expected_d1_crtc_h_total(plan) &&
           snapshot->crtc_h_blank_start_end ==
               kernel_drm_radeon_expected_d1_crtc_h_blank_start_end(plan) &&
           snapshot->crtc_h_sync_a == kernel_drm_radeon_expected_d1_crtc_h_sync_a(plan) &&
           snapshot->crtc_v_total == kernel_drm_radeon_expected_d1_crtc_v_total(plan) &&
           snapshot->crtc_v_blank_start_end ==
               kernel_drm_radeon_expected_d1_crtc_v_blank_start_end(plan) &&
           snapshot->crtc_v_sync_a == kernel_drm_radeon_expected_d1_crtc_v_sync_a(plan) &&
           snapshot->crtc_control == kernel_drm_radeon_expected_d1_crtc_control(before) &&
           snapshot->crtc_blank_control ==
               kernel_drm_radeon_expected_d1_crtc_blank_control(before) &&
           snapshot->grph_enable == expected_grph_enable &&
           snapshot->grph_control == expected_grph_control &&
           snapshot->grph_primary_surface_address == expected_surface_address &&
           snapshot->grph_secondary_surface_address == expected_surface_address &&
           snapshot->grph_pitch == kernel_drm_radeon_expected_d1_pitch(plan) &&
           snapshot->grph_x_start == kernel_drm_radeon_expected_d1_grph_x_start() &&
           snapshot->grph_y_start == kernel_drm_radeon_expected_d1_grph_y_start() &&
           snapshot->grph_x_end == kernel_drm_radeon_expected_d1_grph_x_end(plan) &&
           snapshot->grph_y_end == kernel_drm_radeon_expected_d1_grph_y_end(plan) &&
           snapshot->cur_control == kernel_drm_radeon_expected_d1_cur_control(before) &&
           snapshot->cur_size == kernel_drm_radeon_expected_d1_cur_size(before) &&
           snapshot->viewport_start == kernel_drm_radeon_expected_d1_viewport_start() &&
           snapshot->viewport_size == expected_viewport_size &&
           snapshot->master_update_mode == expected_master_update_mode;
}

static void kernel_drm_radeon_log_d1_stage_mismatch_u32(const char *name,
                                                        uint32_t expected,
                                                        uint32_t actual) {
    if (name == 0 || expected == actual) {
        return;
    }
    kernel_debug_printf("radeon: d1 stage mismatch %s expected=%x actual=%x\n",
                        name,
                        expected,
                        actual);
}

static void kernel_drm_radeon_log_d1_stage_snapshot_summary(
    const char *label,
    const struct kernel_drm_radeon_d1_stage_snapshot *snapshot) {
    if (label == 0 || snapshot == 0) {
        return;
    }

    kernel_debug_printf("radeon: %s master_lock=%x master_mode=%x crtc_lock=%x grph_update=%x grph_en=%x pitch=%x surf=%x viewport=%x size=%x x=%x y=%x cursor_ctl=%x cursor_size=%x\n",
                        label,
                        snapshot->master_update_lock,
                        snapshot->master_update_mode,
                        snapshot->crtc_update_lock,
                        snapshot->grph_update,
                        snapshot->grph_enable,
                        snapshot->grph_pitch,
                        snapshot->grph_primary_surface_address,
                        snapshot->viewport_start,
                        snapshot->viewport_size,
                        snapshot->grph_x_end,
                        snapshot->grph_y_end,
                        snapshot->cur_control,
                        snapshot->cur_size);
}

static void kernel_drm_radeon_log_d1_snapshot_failure(
    const struct kernel_drm_candidate *candidate,
    const char *stage,
    uint32_t width,
    uint32_t height,
    uint32_t fb_addr) {
    if (candidate == 0 || stage == 0) {
        return;
    }

    kernel_debug_printf("radeon: d1 snapshot failed stage=%s dev=%x requested=%dx%d fb=%x mmio=%x mmio_size=%x aperture=%x+%x\n",
                        stage,
                        candidate->pci.device_id,
                        (int)width,
                        (int)height,
                        fb_addr,
                        (uint32_t)candidate->mmio_base,
                        (uint32_t)candidate->mmio_size,
                        (uint32_t)candidate->fb_base,
                        (uint32_t)candidate->fb_size);
}

static int kernel_drm_radeon_d1_restore_matches(
    const struct kernel_drm_radeon_d1_stage_snapshot *expected,
    const struct kernel_drm_radeon_d1_stage_snapshot *actual) {
    if (expected == 0 || actual == 0) {
        return 0;
    }

    return expected->master_update_lock == actual->master_update_lock &&
           expected->master_update_mode == actual->master_update_mode &&
           expected->crtc_update_lock == actual->crtc_update_lock &&
           expected->crtc_h_total == actual->crtc_h_total &&
           expected->crtc_h_blank_start_end == actual->crtc_h_blank_start_end &&
           expected->crtc_h_sync_a == actual->crtc_h_sync_a &&
           expected->crtc_v_total == actual->crtc_v_total &&
           expected->crtc_v_blank_start_end == actual->crtc_v_blank_start_end &&
           expected->crtc_v_sync_a == actual->crtc_v_sync_a &&
           expected->crtc_control == actual->crtc_control &&
           expected->crtc_blank_control == actual->crtc_blank_control &&
           expected->grph_update == actual->grph_update &&
           expected->grph_enable == actual->grph_enable &&
           expected->grph_control == actual->grph_control &&
           expected->grph_primary_surface_address == actual->grph_primary_surface_address &&
           expected->grph_secondary_surface_address == actual->grph_secondary_surface_address &&
           expected->grph_pitch == actual->grph_pitch &&
           expected->grph_x_start == actual->grph_x_start &&
           expected->grph_y_start == actual->grph_y_start &&
           expected->grph_x_end == actual->grph_x_end &&
           expected->grph_y_end == actual->grph_y_end &&
           expected->cur_control == actual->cur_control &&
           expected->cur_size == actual->cur_size &&
           expected->viewport_start == actual->viewport_start &&
           expected->viewport_size == actual->viewport_size;
}

static void kernel_drm_radeon_log_d1_restore_mismatches(
    const struct kernel_drm_radeon_d1_stage_snapshot *expected,
    const struct kernel_drm_radeon_d1_stage_snapshot *actual) {
    if (expected == 0 || actual == 0) {
        return;
    }

    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1MODE_MASTER_UPDATE_LOCK",
                                                expected->master_update_lock,
                                                actual->master_update_lock);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1MODE_MASTER_UPDATE_MODE",
                                                expected->master_update_mode,
                                                actual->master_update_mode);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_UPDATE_LOCK",
                                                expected->crtc_update_lock,
                                                actual->crtc_update_lock);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_H_TOTAL",
                                                expected->crtc_h_total,
                                                actual->crtc_h_total);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_H_BLANK_START_END",
                                                expected->crtc_h_blank_start_end,
                                                actual->crtc_h_blank_start_end);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_H_SYNC_A",
                                                expected->crtc_h_sync_a,
                                                actual->crtc_h_sync_a);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_V_TOTAL",
                                                expected->crtc_v_total,
                                                actual->crtc_v_total);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_V_BLANK_START_END",
                                                expected->crtc_v_blank_start_end,
                                                actual->crtc_v_blank_start_end);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_V_SYNC_A",
                                                expected->crtc_v_sync_a,
                                                actual->crtc_v_sync_a);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_CONTROL",
                                                expected->crtc_control,
                                                actual->crtc_control);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CRTC_BLANK_CONTROL",
                                                expected->crtc_blank_control,
                                                actual->crtc_blank_control);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_UPDATE",
                                                expected->grph_update,
                                                actual->grph_update);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_ENABLE",
                                                expected->grph_enable,
                                                actual->grph_enable);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_CONTROL",
                                                expected->grph_control,
                                                actual->grph_control);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_PRIMARY_SURFACE_ADDRESS",
                                                expected->grph_primary_surface_address,
                                                actual->grph_primary_surface_address);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_SECONDARY_SURFACE_ADDRESS",
                                                expected->grph_secondary_surface_address,
                                                actual->grph_secondary_surface_address);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_PITCH",
                                                expected->grph_pitch,
                                                actual->grph_pitch);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_X_START",
                                                expected->grph_x_start,
                                                actual->grph_x_start);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_Y_START",
                                                expected->grph_y_start,
                                                actual->grph_y_start);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_X_END",
                                                expected->grph_x_end,
                                                actual->grph_x_end);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1GRPH_Y_END",
                                                expected->grph_y_end,
                                                actual->grph_y_end);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CUR_CONTROL",
                                                expected->cur_control,
                                                actual->cur_control);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1CUR_SIZE",
                                                expected->cur_size,
                                                actual->cur_size);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1MODE_VIEWPORT_START",
                                                expected->viewport_start,
                                                actual->viewport_start);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("restore D1MODE_VIEWPORT_SIZE",
                                                expected->viewport_size,
                                                actual->viewport_size);
}

static void kernel_drm_radeon_log_d1_stage_mismatches(
    const struct kernel_drm_radeon_d1_stage_snapshot *snapshot,
    const struct kernel_drm_radeon_d1_stage_snapshot *before,
    uint32_t expected_grph_control,
    uint32_t expected_surface_address,
    const struct kernel_drm_radeon_mode_plan *plan) {
    uint32_t expected_grph_update;
    uint32_t expected_grph_enable;
    uint32_t expected_master_update_lock;
    uint32_t expected_master_update_mode;
    uint32_t expected_viewport_size;

    if (snapshot == 0 || before == 0 || plan == 0) {
        return;
    }

    expected_grph_update = kernel_drm_radeon_expected_d1_grph_update(before);
    expected_grph_enable = kernel_drm_radeon_expected_d1_grph_enable();
    expected_master_update_lock = kernel_drm_radeon_expected_d1_master_update_lock();
    expected_master_update_mode = kernel_drm_radeon_expected_d1_master_update_mode();
    expected_viewport_size = kernel_drm_radeon_expected_d1_viewport_size(plan);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_UPDATE",
                                                expected_grph_update,
                                                snapshot->grph_update);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1MODE_MASTER_UPDATE_LOCK",
                                                expected_master_update_lock,
                                                snapshot->master_update_lock);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_UPDATE_LOCK",
                                                kernel_drm_radeon_expected_d1_crtc_update_lock(before),
                                                snapshot->crtc_update_lock);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_H_TOTAL",
                                                kernel_drm_radeon_expected_d1_crtc_h_total(plan),
                                                snapshot->crtc_h_total);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_H_BLANK_START_END",
                                                kernel_drm_radeon_expected_d1_crtc_h_blank_start_end(plan),
                                                snapshot->crtc_h_blank_start_end);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_H_SYNC_A",
                                                kernel_drm_radeon_expected_d1_crtc_h_sync_a(plan),
                                                snapshot->crtc_h_sync_a);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_V_TOTAL",
                                                kernel_drm_radeon_expected_d1_crtc_v_total(plan),
                                                snapshot->crtc_v_total);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_V_BLANK_START_END",
                                                kernel_drm_radeon_expected_d1_crtc_v_blank_start_end(plan),
                                                snapshot->crtc_v_blank_start_end);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_V_SYNC_A",
                                                kernel_drm_radeon_expected_d1_crtc_v_sync_a(plan),
                                                snapshot->crtc_v_sync_a);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_CONTROL",
                                                kernel_drm_radeon_expected_d1_crtc_control(before),
                                                snapshot->crtc_control);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CRTC_BLANK_CONTROL",
                                                kernel_drm_radeon_expected_d1_crtc_blank_control(before),
                                                snapshot->crtc_blank_control);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_ENABLE",
                                                expected_grph_enable,
                                                snapshot->grph_enable);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_CONTROL",
                                                expected_grph_control,
                                                snapshot->grph_control);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_PRIMARY_SURFACE_ADDRESS",
                                                expected_surface_address,
                                                snapshot->grph_primary_surface_address);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_SECONDARY_SURFACE_ADDRESS",
                                                expected_surface_address,
                                                snapshot->grph_secondary_surface_address);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_PITCH",
                                                kernel_drm_radeon_expected_d1_pitch(plan),
                                                snapshot->grph_pitch);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_X_START",
                                                kernel_drm_radeon_expected_d1_grph_x_start(),
                                                snapshot->grph_x_start);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_Y_START",
                                                kernel_drm_radeon_expected_d1_grph_y_start(),
                                                snapshot->grph_y_start);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_X_END",
                                                kernel_drm_radeon_expected_d1_grph_x_end(plan),
                                                snapshot->grph_x_end);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1GRPH_Y_END",
                                                kernel_drm_radeon_expected_d1_grph_y_end(plan),
                                                snapshot->grph_y_end);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CUR_CONTROL",
                                                kernel_drm_radeon_expected_d1_cur_control(before),
                                                snapshot->cur_control);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1CUR_SIZE",
                                                kernel_drm_radeon_expected_d1_cur_size(before),
                                                snapshot->cur_size);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1MODE_MASTER_UPDATE_MODE",
                                                expected_master_update_mode,
                                                snapshot->master_update_mode);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1MODE_VIEWPORT_START",
                                                kernel_drm_radeon_expected_d1_viewport_start(),
                                                snapshot->viewport_start);
    kernel_drm_radeon_log_d1_stage_mismatch_u32("D1MODE_VIEWPORT_SIZE",
                                                expected_viewport_size,
                                                snapshot->viewport_size);
}

static void kernel_drm_radeon_restore_d1_stage(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_radeon_d1_stage_snapshot *snapshot) {
    if (candidate == 0 || snapshot == 0) {
        return;
    }

    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1MODE_MASTER_UPDATE_LOCK,
                                   snapshot->master_update_lock);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1MODE_MASTER_UPDATE_MODE,
                                   snapshot->master_update_mode);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_UPDATE_LOCK,
                                   snapshot->crtc_update_lock);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_H_TOTAL,
                                   snapshot->crtc_h_total);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_H_BLANK_START_END,
                                   snapshot->crtc_h_blank_start_end);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_H_SYNC_A,
                                   snapshot->crtc_h_sync_a);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_V_TOTAL,
                                   snapshot->crtc_v_total);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_V_BLANK_START_END,
                                   snapshot->crtc_v_blank_start_end);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_V_SYNC_A,
                                   snapshot->crtc_v_sync_a);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_CONTROL,
                                   snapshot->crtc_control);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_BLANK_CONTROL,
                                   snapshot->crtc_blank_control);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_UPDATE,
                                   snapshot->grph_update);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_ENABLE,
                                   snapshot->grph_enable);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_CONTROL,
                                   snapshot->grph_control);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS,
                                   snapshot->grph_primary_surface_address);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_SECONDARY_SURFACE_ADDRESS,
                                   snapshot->grph_secondary_surface_address);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_PITCH,
                                   snapshot->grph_pitch);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_X_START,
                                   snapshot->grph_x_start);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_Y_START,
                                   snapshot->grph_y_start);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_X_END,
                                   snapshot->grph_x_end);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_Y_END,
                                   snapshot->grph_y_end);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CUR_CONTROL,
                                   snapshot->cur_control);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CUR_SIZE,
                                   snapshot->cur_size);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1MODE_VIEWPORT_START,
                                   snapshot->viewport_start);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1MODE_VIEWPORT_SIZE,
                                   snapshot->viewport_size);
}

static int kernel_drm_radeon_restore_d1_stage_and_verify(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_radeon_d1_stage_snapshot *snapshot,
    struct kernel_drm_radeon_stage_result *result_out) {
    if (candidate == 0 || snapshot == 0 || result_out == 0) {
        return -1;
    }

    kernel_drm_radeon_restore_d1_stage(candidate, snapshot);
    if (kernel_drm_radeon_snapshot_d1_stage(candidate, &result_out->restored) != 0) {
        result_out->restored = (struct kernel_drm_radeon_d1_stage_snapshot){0};
        kernel_drm_radeon_log_d1_snapshot_failure(candidate,
                                                  "restore",
                                                  snapshot->grph_x_end,
                                                  snapshot->grph_y_end,
                                                  snapshot->grph_primary_surface_address);
        result_out->restore_status = -1;
        return -1;
    }
    if (!kernel_drm_radeon_d1_restore_matches(snapshot, &result_out->restored)) {
        kernel_drm_radeon_log_d1_stage_snapshot_summary("d1 restore expected", snapshot);
        kernel_drm_radeon_log_d1_stage_snapshot_summary("d1 restored", &result_out->restored);
        kernel_drm_radeon_log_d1_restore_mismatches(snapshot, &result_out->restored);
        result_out->restore_status = -1;
        return -1;
    }

    result_out->restore_status = 0;
    return 0;
}

static int kernel_drm_radeon_stage_d1_scanout(
    const struct kernel_drm_candidate *candidate,
    uint32_t fb_addr,
    const struct kernel_drm_radeon_mode_plan *plan,
    const struct kernel_drm_radeon_d1_stage_snapshot *before,
    struct kernel_drm_radeon_stage_result *result_out) {
    uint32_t expected_grph_control;
    uint32_t update;

    if (candidate == 0 || plan == 0 || before == 0 || result_out == 0) {
        return -1;
    }

    *result_out = (struct kernel_drm_radeon_stage_result){0};
    result_out->before = *before;
    expected_grph_control = kernel_drm_radeon_expected_d1_grph_control();

    kernel_drm_radeon_program_d1_pll(candidate, plan);

    update = kernel_drm_radeon_expected_d1_grph_update(before);
    kernel_drm_radeon_mmio_write32(candidate, AVIVO_D1GRPH_UPDATE, update);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1MODE_MASTER_UPDATE_LOCK,
                                   kernel_drm_radeon_expected_d1_master_update_lock());
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_H_TOTAL,
                                   kernel_drm_radeon_expected_d1_crtc_h_total(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_H_BLANK_START_END,
                                   kernel_drm_radeon_expected_d1_crtc_h_blank_start_end(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_H_SYNC_A,
                                   kernel_drm_radeon_expected_d1_crtc_h_sync_a(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_V_TOTAL,
                                   kernel_drm_radeon_expected_d1_crtc_v_total(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_V_BLANK_START_END,
                                   kernel_drm_radeon_expected_d1_crtc_v_blank_start_end(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1CRTC_V_SYNC_A,
                                   kernel_drm_radeon_expected_d1_crtc_v_sync_a(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS,
                                   kernel_drm_radeon_expected_d1_surface_address(fb_addr));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_SECONDARY_SURFACE_ADDRESS,
                                   kernel_drm_radeon_expected_d1_surface_address(fb_addr));
    kernel_drm_radeon_mmio_write32(candidate, AVIVO_D1GRPH_CONTROL, expected_grph_control);
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_X_START,
                                   kernel_drm_radeon_expected_d1_grph_x_start());
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_Y_START,
                                   kernel_drm_radeon_expected_d1_grph_y_start());
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_X_END,
                                   kernel_drm_radeon_expected_d1_grph_x_end(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_Y_END,
                                   kernel_drm_radeon_expected_d1_grph_y_end(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_PITCH,
                                   kernel_drm_radeon_expected_d1_pitch(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1GRPH_ENABLE,
                                   kernel_drm_radeon_expected_d1_grph_enable());
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1MODE_VIEWPORT_START,
                                   kernel_drm_radeon_expected_d1_viewport_start());
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1MODE_VIEWPORT_SIZE,
                                   kernel_drm_radeon_expected_d1_viewport_size(plan));
    kernel_drm_radeon_mmio_write32(candidate,
                                   AVIVO_D1MODE_MASTER_UPDATE_MODE,
                                   kernel_drm_radeon_expected_d1_master_update_mode());

    if (kernel_drm_radeon_snapshot_d1_stage(candidate, &result_out->staged) != 0) {
        kernel_drm_radeon_log_d1_snapshot_failure(candidate,
                                                  "stage",
                                                  plan->width,
                                                  plan->height,
                                                  fb_addr);
        result_out->stage_status = -1;
        return -1;
    }
    if (!kernel_drm_radeon_d1_stage_matches(&result_out->staged,
                                            before,
                                            expected_grph_control,
                                            fb_addr,
                                            plan)) {
        kernel_drm_radeon_log_d1_stage_snapshot_summary("d1 before", before);
        kernel_drm_radeon_log_d1_stage_snapshot_summary("d1 staged", &result_out->staged);
        kernel_drm_radeon_log_d1_stage_mismatches(&result_out->staged,
                                                  before,
                                                  expected_grph_control,
                                                  fb_addr,
                                                  plan);
        result_out->stage_status = -1;
        return -1;
    }

    result_out->stage_status = 0;
    return 0;
}

static int kernel_drm_radeon_verify_committed_d1_stage(
    const struct kernel_drm_candidate *candidate,
    uint32_t fb_addr,
    const struct kernel_drm_radeon_mode_plan *plan,
    const struct kernel_drm_radeon_d1_stage_snapshot *before,
    struct kernel_drm_radeon_stage_result *result_out) {
    uint32_t expected_grph_control;

    if (candidate == 0 || plan == 0 || before == 0 || result_out == 0) {
        return -1;
    }

    if (kernel_drm_radeon_snapshot_d1_stage(candidate, &result_out->committed) != 0) {
        result_out->committed = (struct kernel_drm_radeon_d1_stage_snapshot){0};
        kernel_drm_radeon_log_d1_snapshot_failure(candidate,
                                                  "commit",
                                                  plan->width,
                                                  plan->height,
                                                  fb_addr);
        return -1;
    }

    expected_grph_control = kernel_drm_radeon_expected_d1_grph_control();
    if (!kernel_drm_radeon_d1_stage_matches(&result_out->committed,
                                            before,
                                            expected_grph_control,
                                            fb_addr,
                                            plan)) {
        kernel_drm_radeon_log_d1_stage_snapshot_summary("d1 before", before);
        kernel_drm_radeon_log_d1_stage_snapshot_summary("d1 committed", &result_out->committed);
        kernel_drm_radeon_log_d1_stage_mismatches(&result_out->committed,
                                                  before,
                                                  expected_grph_control,
                                                  fb_addr,
                                                  plan);
        return -1;
    }

    return 0;
}

static int kernel_drm_radeon_read_probe_info(const struct kernel_drm_candidate *candidate,
                                             struct kernel_drm_radeon_probe_info *info_out) {
    if (candidate == 0 || info_out == 0) {
        return -1;
    }
    if (candidate->pci.vendor_id != PCI_VENDOR_AMD) {
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
    info_out->family_name = kernel_drm_radeon_family_name(candidate->pci.device_id);
    info_out->asic_name = kernel_drm_radeon_asic_name(candidate->pci.device_id);
    info_out->modeset_candidate =
        kernel_drm_radeon_is_modeset_candidate(candidate->pci.device_id);
    return 0;
}

static int kernel_drm_radeon_probe(const struct kernel_drm_candidate *candidate) {
    struct kernel_drm_radeon_probe_info probe;
    struct kernel_drm_radeon_mmio_snapshot mmio;
    struct kernel_drm_radeon_vga_snapshot vga;
    struct kernel_drm_radeon_r6xx_display_snapshot r6xx_display;
    struct kernel_drm_radeon_r6xx_display_analysis r6xx_analysis;
    int have_mmio_snapshot = 0;
    int have_vga_snapshot = 0;
    int have_r6xx_display_snapshot = 0;

    if (kernel_drm_radeon_read_probe_info(candidate, &probe) != 0) {
        return -1;
    }
    if ((probe.command & PCI_COMMAND_MEMORY_SPACE) == 0u) {
        kernel_debug_printf("radeon: memory decode disabled dev=%x rev=%x mmio=%x fb=%x\n",
                            candidate->pci.device_id,
                            candidate->pci.revision,
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->fb_base);
        return -1;
    }
    if (candidate->mmio_base == (uintptr_t)0 || candidate->mmio_size == 0u) {
        kernel_debug_printf("radeon: missing MMIO BAR dev=%x bar0=%x mmio=%x size=%x\n",
                            candidate->pci.device_id,
                            candidate->pci.bars[0],
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->mmio_size);
        return -1;
    }
    if (kernel_drm_radeon_snapshot_mmio(candidate, &mmio) == 0) {
        have_mmio_snapshot = 1;
    }
    if (kernel_drm_radeon_snapshot_vga(&vga) == 0) {
        have_vga_snapshot = 1;
    }
    if (kernel_drm_radeon_snapshot_r6xx_display(candidate, &r6xx_display) == 0) {
        kernel_drm_radeon_analyze_r6xx_display(&r6xx_display, &r6xx_analysis);
        have_r6xx_display_snapshot = 1;
    }

    kernel_debug_printf("radeon: probe dev=%x rev=%x family=%s asic=%s modeset_candidate=%d class=%x subclass=%x prog_if=%x command=%x mmio=%x mmio_size=%x fb=%x fb_size=%x subsys=%x:%x irq=%x rom=%x\n",
                        candidate->pci.device_id,
                        candidate->pci.revision,
                        probe.family_name,
                        probe.asic_name,
                        probe.modeset_candidate,
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
        kernel_debug_printf("radeon: mmio reg00=%x reg04=%x reg08=%x reg0c=%x\n",
                            mmio.reg00,
                            mmio.reg04,
                            mmio.reg08,
                            mmio.reg0c);
    }
    if (have_vga_snapshot) {
        kernel_debug_printf("radeon: vga misc=%x htotal=%x hdisp_end=%x vtotal=%x overflow=%x vdisp_end=%x mode_ctl=%x\n",
                            vga.misc_output,
                            vga.crtc_htotal,
                            vga.crtc_hdisplay_end,
                            vga.crtc_vtotal,
                            vga.crtc_overflow,
                            vga.crtc_vdisplay_end,
                            vga.crtc_mode_control);
    }
    if (have_r6xx_display_snapshot) {
        kernel_debug_printf("radeon: r6xx d1 h_total=%x h_blank=%x control=%x blank=%x grph_en=%x grph_addr=%x cursor_ctl=%x cursor_size=%x vline=%x viewport_start=%x viewport_size=%x\n",
                            r6xx_display.d1_crtc_h_total,
                            r6xx_display.d1_crtc_h_blank_start_end,
                            r6xx_display.d1_crtc_control,
                            r6xx_display.d1_crtc_blank_control,
                            r6xx_display.d1_grph_enable,
                            r6xx_display.d1_grph_primary_surface_address,
                            r6xx_display.d1_cur_control,
                            r6xx_display.d1_cur_size,
                            r6xx_display.d1_mode_vline_status,
                            r6xx_display.d1_mode_viewport_start,
                            r6xx_display.d1_mode_viewport_size);
        kernel_debug_printf("radeon: r6xx d2 h_total=%x h_blank=%x control=%x blank=%x grph_en=%x grph_addr=%x vline=%x viewport_start=%x viewport_size=%x\n",
                            r6xx_display.d2_crtc_h_total,
                            r6xx_display.d2_crtc_h_blank_start_end,
                            r6xx_display.d2_crtc_control,
                            r6xx_display.d2_crtc_blank_control,
                            r6xx_display.d2_grph_enable,
                            r6xx_display.d2_grph_primary_surface_address,
                            r6xx_display.d2_mode_vline_status,
                            r6xx_display.d2_mode_viewport_start,
                            r6xx_display.d2_mode_viewport_size);
        kernel_debug_printf("radeon: r6xx display-state responding=%d live_regs=%u crtc0=%d crtc1=%d grph0=%d grph1=%d viewport0=%d viewport1=%d cursor0=%d\n",
                            r6xx_analysis.display_block_responding,
                            r6xx_analysis.live_registers,
                            r6xx_analysis.crtc0_programmed,
                            r6xx_analysis.crtc1_programmed,
                            r6xx_analysis.grph0_programmed,
                            r6xx_analysis.grph1_programmed,
                            r6xx_analysis.viewport0_programmed,
                            r6xx_analysis.viewport1_programmed,
                            r6xx_analysis.cursor0_programmed);
    }
    kernel_drm_radeon_enable_modeset();
    return 0;
}

int kernel_drm_radeon_revert_last_commit(void) {
    struct kernel_drm_candidate candidate;
    struct kernel_drm_radeon_stage_result stage;
    struct bootinfo_vesa boot_vesa;
    uint32_t boot_flags;

    if (!g_kernel_drm_radeon_last_commit_valid) {
        return -1;
    }

    candidate = g_kernel_drm_radeon_last_commit_candidate;
    stage = g_kernel_drm_radeon_last_commit_stage;
    boot_vesa = g_kernel_drm_radeon_last_commit_boot_vesa;
    boot_flags = g_kernel_drm_radeon_last_commit_boot_flags;
    kernel_drm_radeon_forget_last_commit();

    if (kernel_drm_radeon_restore_d1_stage_and_verify(&candidate,
                                                       &stage.before,
                                                       &stage) != 0) {
        kernel_debug_puts("radeon: modeset-commit revert display restore failed\n");
        return -1;
    }
    if (kernel_drm_radeon_restore_boot_mode(&boot_vesa, boot_flags) != 0) {
        kernel_debug_puts("radeon: modeset-commit revert bootinfo restore failed\n");
        return -1;
    }

    kernel_debug_printf("radeon: modeset-commit revert ok viewport=%x pitch=%x\n",
                        stage.restored.viewport_size,
                        stage.restored.grph_pitch);
    return 0;
}

static int kernel_drm_radeon_set_mode(const struct kernel_drm_candidate *candidate,
                                      uint32_t width,
                                      uint32_t height,
                                      uint16_t mode_id,
                                      struct video_mode *mode_out) {
    struct kernel_drm_radeon_mode_plan plan;
    struct kernel_drm_radeon_r6xx_display_analysis analysis;
    struct kernel_drm_radeon_d1_stage_snapshot before;
    struct kernel_drm_radeon_stage_result stage;
    struct video_mode committed_mode = {0};
    struct bootinfo_vesa previous_boot_vesa = {0};
    uint32_t previous_boot_flags = 0u;
    uint32_t boot_fb_addr = 0u;

    kernel_drm_radeon_clear_mode_out(mode_out);
    kernel_drm_radeon_forget_last_commit();

    if (!g_radeon_modeset_enabled) {
        kernel_debug_printf("radeon: modeset blocked (safe mode), would set %dx%d mode=%x dev=%x\n",
                            (int)width,
                            (int)height,
                            mode_id,
                            candidate != 0 ? candidate->pci.device_id : 0u);
        kernel_drm_radeon_probe(candidate);
        return -1;
    }

    if (candidate == 0) {
        return -1;
    }
    if (kernel_drm_radeon_preflight_r6xx(candidate,
                                          width,
                                          height,
                                          &plan,
                                          &analysis) != 0) {
        kernel_debug_printf("radeon: modeset preflight rejected dev=%x family=%s asic=%s requested=%dx%d mode=%x mmio=%x fb=%x\n",
                            candidate->pci.device_id,
                            kernel_drm_radeon_family_name(candidate->pci.device_id),
                            kernel_drm_radeon_asic_name(candidate->pci.device_id),
                            (int)width,
                            (int)height,
                            mode_id,
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->fb_base);
        return -1;
    }
    if (kernel_drm_radeon_boot_fb_addr(&boot_fb_addr) != 0) {
        kernel_debug_printf("radeon: modeset preflight lacks boot framebuffer handoff dev=%x requested=%dx%d plan_pitch=%x frame_bytes=%x\n",
                            candidate->pci.device_id,
                            (int)width,
                            (int)height,
                            plan.pitch,
                            plan.frame_bytes);
        return -1;
    }
    if (!kernel_drm_radeon_boot_fb_in_aperture(candidate, boot_fb_addr, &plan)) {
        kernel_debug_printf("radeon: modeset preflight boot framebuffer outside aperture dev=%x boot_fb=%x aperture=%x+%x frame_bytes=%x\n",
                            candidate->pci.device_id,
                            boot_fb_addr,
                            (uint32_t)candidate->fb_base,
                            (uint32_t)candidate->fb_size,
                            plan.frame_bytes);
        return -1;
    }
    if (kernel_drm_radeon_fill_mode_out(boot_fb_addr, &plan, &committed_mode) != 0) {
        kernel_debug_printf("radeon: modeset preflight mode_out reject dev=%x boot_fb=%x requested=%dx%d pitch=%x\n",
                            candidate->pci.device_id,
                            boot_fb_addr,
                            (int)width,
                            (int)height,
                            plan.pitch);
        return -1;
    }
    if (kernel_drm_radeon_snapshot_boot_mode(&previous_boot_vesa, &previous_boot_flags) != 0) {
        kernel_debug_puts("radeon: modeset preflight reject bootinfo snapshot\n");
        return -1;
    }
    if (kernel_drm_radeon_snapshot_d1_stage(candidate, &before) != 0) {
        kernel_drm_radeon_log_d1_snapshot_failure(candidate,
                                                  "before",
                                                  width,
                                                  height,
                                                  boot_fb_addr);
        return -1;
    }

    kernel_debug_printf("radeon: modeset-preflight dev=%x family=%s asic=%s requested=%dx%d mode=%x plan_pitch=%x frame_bytes=%x aperture=%x boot_fb=%x responding=%d live_regs=%u crtc0=%d crtc1=%d grph0=%d grph1=%d viewport0=%d viewport1=%d cursor0=%d\n",
                        candidate->pci.device_id,
                        kernel_drm_radeon_family_name(candidate->pci.device_id),
                        kernel_drm_radeon_asic_name(candidate->pci.device_id),
                        (int)width,
                        (int)height,
                        mode_id,
                        plan.pitch,
                        plan.frame_bytes,
                        (uint32_t)candidate->fb_size,
                        boot_fb_addr,
                        analysis.display_block_responding,
                        analysis.live_registers,
                        analysis.crtc0_programmed,
                        analysis.crtc1_programmed,
                        analysis.grph0_programmed,
                        analysis.grph1_programmed,
                        analysis.viewport0_programmed,
                        analysis.viewport1_programmed,
                        analysis.cursor0_programmed);
    if (kernel_drm_radeon_stage_d1_scanout(candidate,
                                           boot_fb_addr,
                                           &plan,
                                           &before,
                                           &stage) != 0) {
        kernel_debug_printf("radeon: d1 stage rejected dev=%x requested=%dx%d boot_fb=%x\n",
                            candidate->pci.device_id,
                            (int)width,
                            (int)height,
                            boot_fb_addr);
        if (kernel_drm_radeon_restore_d1_stage_and_verify(candidate, &before, &stage) == 0) {
            kernel_debug_printf("radeon: d1 stage rollback verified dev=%x requested=%dx%d\n",
                                candidate->pci.device_id,
                                (int)width,
                                (int)height);
        } else {
            kernel_debug_printf("radeon: d1 stage rollback failed dev=%x requested=%dx%d\n",
                                candidate->pci.device_id,
                                (int)width,
                                (int)height);
        }
        return -1;
    }
    if (kernel_drm_radeon_publish_boot_mode(mode_id, &committed_mode) != 0) {
        (void)kernel_drm_radeon_restore_d1_stage_and_verify(candidate, &before, &stage);
        (void)kernel_drm_radeon_restore_boot_mode_and_log(&previous_boot_vesa,
                                                          previous_boot_flags,
                                                          "modeset-commit bootinfo");
        kernel_drm_radeon_clear_mode_out(mode_out);
        kernel_debug_puts("radeon: modeset-commit rollback after bootinfo handoff failure\n");
        return -1;
    }
    if (kernel_drm_radeon_verify_committed_d1_stage(candidate,
                                                    boot_fb_addr,
                                                    &plan,
                                                    &before,
                                                    &stage) != 0) {
        (void)kernel_drm_radeon_restore_d1_stage_and_verify(candidate, &before, &stage);
        (void)kernel_drm_radeon_restore_boot_mode_and_log(&previous_boot_vesa,
                                                          previous_boot_flags,
                                                          "modeset-commit readback");
        kernel_drm_radeon_clear_mode_out(mode_out);
        kernel_debug_puts("radeon: modeset-commit rollback after final D1 readback failure\n");
        return -1;
    }

    kernel_drm_radeon_remember_last_commit(candidate,
                                           &stage,
                                           &previous_boot_vesa,
                                           previous_boot_flags);
    if (mode_out != 0) {
        *mode_out = committed_mode;
    }
    kernel_debug_printf("radeon: commit ok boot_fb=%x pitch=%x viewport=%x committed_viewport=%x mode=%x\n",
                        committed_mode.fb_addr,
                        stage.staged.grph_pitch,
                        stage.staged.viewport_size,
                        stage.committed.viewport_size,
                        mode_id);
    return 0;
}

const struct kernel_drm_backend_ops g_kernel_drm_radeon_ops = {
    KERNEL_DRM_BACKEND_RADEON,
    "native_gpu_radeon",
    kernel_drm_radeon_probe,
    kernel_drm_radeon_set_mode,
    kernel_drm_radeon_revert_last_commit,
    kernel_drm_radeon_forget_last_commit,
    kernel_drm_radeon_prepare_for_bios_modeset
};
