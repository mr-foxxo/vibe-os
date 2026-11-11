#include <kernel/drivers/video/drm/i915/i915.h>

int kernel_drm_i915_snapshot_display(const struct kernel_drm_candidate *candidate,
                                     struct kernel_drm_i915_display_snapshot *snapshot_out) {
    if (candidate == 0 || snapshot_out == 0 || candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }

    snapshot_out->trans_htotal_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_TRANS_HTOTAL_A);
    snapshot_out->trans_hblank_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_TRANS_HBLANK_A);
    snapshot_out->trans_hsync_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_TRANS_HSYNC_A);
    snapshot_out->trans_vtotal_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_TRANS_VTOTAL_A);
    snapshot_out->trans_vblank_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_TRANS_VBLANK_A);
    snapshot_out->trans_vsync_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_TRANS_VSYNC_A);
    snapshot_out->pipesrc_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_PIPESRC_A);
    snapshot_out->dspcntr_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_DSPCNTR_A);
    snapshot_out->dspstride_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_DSPSTRIDE_A);
    snapshot_out->dsppos_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_DSPPOS_A);
    snapshot_out->dspsize_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_DSPSIZE_A);
    snapshot_out->dspsurf_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_DSPSURF_A);
    snapshot_out->fuse_strap = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_FUSE_STRAP);
    snapshot_out->disp_arb_ctl2 = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_DISP_ARB_CTL2);
    snapshot_out->skl_dfsm = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_SKL_DFSM);
    snapshot_out->dpll_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_DPLL_A);
    snapshot_out->pipeconf_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_PIPECONF_A);
    snapshot_out->transconf_a = kernel_drm_i915_mmio_read32(candidate, INTEL_I915_MMIO_TRANS_CONF_A);
    return 0;
}
