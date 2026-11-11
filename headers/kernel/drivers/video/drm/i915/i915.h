#ifndef KERNEL_DRIVERS_VIDEO_DRM_I915_H
#define KERNEL_DRIVERS_VIDEO_DRM_I915_H

#include <stdint.h>

#include <kernel/drivers/video/drm/drm.h>

#define INTEL_I915_PCI_COMMAND 0x04u
#define INTEL_I915_GMCH_CTRL 0x50u
#define INTEL_I915_BSM 0x5Cu
#define INTEL_I915_OPREGION 0xFCu
#define INTEL_I915_BSM_MASK 0xFFF00000u

struct kernel_drm_i915_id_info {
    uint16_t device_id;
    uint8_t gen;
    const char *platform;
};

struct kernel_drm_i915_probe_info {
    uint16_t command;
    uint16_t gmch_ctl;
    uint32_t bsm;
    uint32_t opregion;
    uint32_t stolen_size;
    uint8_t gen;
    const char *platform_name;
};

struct kernel_drm_i915_mmio_snapshot {
    uint32_t dpll_test;
    uint32_t d_state;
    uint32_t disp_arb_ctl;
};

struct kernel_drm_i915_display_snapshot {
    uint32_t trans_htotal_a;
    uint32_t trans_hblank_a;
    uint32_t trans_hsync_a;
    uint32_t trans_vtotal_a;
    uint32_t trans_vblank_a;
    uint32_t trans_vsync_a;
    uint32_t pipesrc_a;
    uint32_t dspcntr_a;
    uint32_t dspstride_a;
    uint32_t dsppos_a;
    uint32_t dspsize_a;
    uint32_t dspsurf_a;
    uint32_t fuse_strap;
    uint32_t disp_arb_ctl2;
    uint32_t skl_dfsm;
    uint32_t dpll_a;
    uint32_t pipeconf_a;
    uint32_t transconf_a;
};

struct kernel_drm_i915_mode_plan {
    uint32_t width;
    uint32_t height;
    uint32_t htotal;
    uint32_t hblank;
    uint32_t hsync;
    uint32_t vtotal;
    uint32_t vblank;
    uint32_t vsync;
    uint32_t pipesrc;
};

struct kernel_drm_i915_stage_result {
    struct kernel_drm_i915_display_snapshot before;
    struct kernel_drm_i915_display_snapshot after;
    struct kernel_drm_i915_display_snapshot restored;
    int restore_status;
};

#define INTEL_I915_MMIO_DPLL_TEST 0x0000606Cu
#define INTEL_I915_MMIO_D_STATE 0x00006104u
#define INTEL_I915_MMIO_DISP_ARB_CTL 0x00045000u
#define INTEL_I915_MMIO_TRANS_HTOTAL_A 0x00060000u
#define INTEL_I915_MMIO_TRANS_HBLANK_A 0x00060004u
#define INTEL_I915_MMIO_TRANS_HSYNC_A 0x00060008u
#define INTEL_I915_MMIO_TRANS_VTOTAL_A 0x0006000Cu
#define INTEL_I915_MMIO_TRANS_VBLANK_A 0x00060010u
#define INTEL_I915_MMIO_TRANS_VSYNC_A 0x00060014u
#define INTEL_I915_MMIO_PIPESRC_A 0x0006001Cu
#define INTEL_I915_MMIO_DSPCNTR_A 0x00070180u
#define INTEL_I915_MMIO_DSPSTRIDE_A 0x00070188u
#define INTEL_I915_MMIO_DSPPOS_A 0x0007018Cu
#define INTEL_I915_MMIO_DSPSIZE_A 0x00070190u
#define INTEL_I915_MMIO_DSPSURF_A 0x0007019Cu
#define INTEL_I915_MMIO_FUSE_STRAP 0x00042014u
#define INTEL_I915_MMIO_DISP_ARB_CTL2 0x00045004u
#define INTEL_I915_MMIO_SKL_DFSM 0x00051000u

#define INTEL_I915_DSPCNTR_ENABLE (1u << 31)
#define INTEL_I915_DSPCNTR_PIPE_GAMMA (1u << 30)
#define INTEL_I915_DSPCNTR_FORMAT_SHIFT 26u
#define INTEL_I915_DSPCNTR_FORMAT_MASK (7u << INTEL_I915_DSPCNTR_FORMAT_SHIFT)
#define INTEL_I915_DSPCNTR_FORMAT_8BPP (2u << INTEL_I915_DSPCNTR_FORMAT_SHIFT)
#define INTEL_I915_DSPCNTR_PIPE_SEL_SHIFT 24u
#define INTEL_I915_DSPCNTR_PIPE_SEL_MASK (3u << INTEL_I915_DSPCNTR_PIPE_SEL_SHIFT)
#define INTEL_I915_DSPCNTR_PIPE_SEL_A (0u << INTEL_I915_DSPCNTR_PIPE_SEL_SHIFT)

#define INTEL_I915_MMIO_PIPECONF_A     0x00070008u
#define INTEL_I915_MMIO_PIPESTAT_A     0x00070024u
#define INTEL_I915_MMIO_TRANS_CONF_A   0x0006001Cu
#define INTEL_I915_MMIO_DPLL_A         0x00006014u
#define INTEL_I915_MMIO_DPLL_DIVIDER_A 0x00006040u

#define INTEL_I915_PIPECONF_ENABLE       (1u << 31)
#define INTEL_I915_PIPESTAT_VBLANK_START (1u << 1)
#define INTEL_I915_TRANS_CONF_ENABLE     (1u << 31)
#define INTEL_I915_DPLL_VCO_ENABLE       (1u << 31)
#define INTEL_I915_DPLL_P1_SHIFT         16u
#define INTEL_I915_DPLL_P1_MASK          (0x7Fu << INTEL_I915_DPLL_P1_SHIFT)
#define INTEL_I915_DPLL_P2_SHIFT         8u
#define INTEL_I915_DPLL_P2_MASK          (0x1Fu << INTEL_I915_DPLL_P2_SHIFT)
#define INTEL_I915_DPLL_N_SHIFT          0u
#define INTEL_I915_DPLL_N_MASK           (0xFFu << INTEL_I915_DPLL_N_SHIFT)

const struct kernel_drm_i915_id_info *kernel_drm_i915_find_id(uint16_t device_id);
int kernel_drm_i915_is_supported_device(uint16_t device_id);
int kernel_drm_i915_is_modeset_supported_device(uint16_t device_id);
uint8_t kernel_drm_i915_guess_gen(uint16_t device_id);
const char *kernel_drm_i915_platform_name(uint16_t device_id);
uint32_t kernel_drm_i915_decode_stolen_size(uint8_t gen, uint16_t gmch_ctl);
int kernel_drm_i915_read_probe_info(const struct kernel_drm_candidate *candidate,
                                    struct kernel_drm_i915_probe_info *info_out);
uint32_t kernel_drm_i915_mmio_read32(const struct kernel_drm_candidate *candidate, uint32_t reg);
void kernel_drm_i915_mmio_write32(const struct kernel_drm_candidate *candidate,
                                  uint32_t reg,
                                  uint32_t value);
int kernel_drm_i915_snapshot_mmio(const struct kernel_drm_candidate *candidate,
                                  struct kernel_drm_i915_mmio_snapshot *snapshot_out);
int kernel_drm_i915_snapshot_display(const struct kernel_drm_candidate *candidate,
                                     struct kernel_drm_i915_display_snapshot *snapshot_out);
int kernel_drm_i915_build_mode_plan(uint32_t width,
                                    uint32_t height,
                                    struct kernel_drm_i915_mode_plan *plan_out);
int kernel_drm_i915_restore_display(const struct kernel_drm_candidate *candidate,
                                    const struct kernel_drm_i915_display_snapshot *snapshot);
int kernel_drm_i915_stage_mode_plan(const struct kernel_drm_candidate *candidate,
                                    const struct kernel_drm_i915_mode_plan *plan,
                                    struct kernel_drm_i915_stage_result *result_out);
int kernel_drm_i915_commit_mode_plan(const struct kernel_drm_candidate *candidate,
                                     const struct kernel_drm_i915_mode_plan *plan,
                                     struct kernel_drm_i915_stage_result *result_out);
int kernel_drm_i915_revert_last_commit(void);
void kernel_drm_i915_forget_last_commit(void);
int kernel_drm_i915_full_commit_mode_plan(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_i915_mode_plan *plan,
    struct kernel_drm_i915_stage_result *result_out);
void kernel_drm_i915_enable_modeset(void);

#endif
