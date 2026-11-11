#include <kernel/drivers/video/drm/i915/i915.h>
#include <kernel/drivers/debug/debug.h>

static void kernel_drm_i915_log_mismatch_u32(const char *name,
                                             uint32_t expected,
                                             uint32_t actual);

static void kernel_drm_i915_log_snapshot_failure(
    const char *stage,
    const struct kernel_drm_candidate *candidate) {
    kernel_debug_printf("i915: snapshot failed stage=%s mmio=%x fb=%x\n",
                        stage != 0 ? stage : "unknown",
                        candidate != 0 ? (uint32_t)candidate->mmio_base : 0u,
                        candidate != 0 ? (uint32_t)candidate->fb_base : 0u);
}

static void kernel_drm_i915_record_restore_result(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_i915_display_snapshot *snapshot,
    struct kernel_drm_i915_stage_result *result_out) {
    if (result_out == 0) {
        return;
    }

    result_out->restore_status = kernel_drm_i915_restore_display(candidate, snapshot);
    if (result_out->restore_status == 0) {
        if (kernel_drm_i915_snapshot_display(candidate, &result_out->restored) != 0) {
            kernel_drm_i915_log_snapshot_failure("restore-result", candidate);
            result_out->restored = (struct kernel_drm_i915_display_snapshot){0};
            result_out->restore_status = -1;
        }
    } else {
        result_out->restored = (struct kernel_drm_i915_display_snapshot){0};
    }
}

static uint32_t kernel_drm_i915_primary_stride_bytes(uint32_t width) {
    return (width + 63u) & ~63u;
}

static const char *kernel_drm_i915_plane_format_name(uint32_t dspcntr) {
    switch (dspcntr & INTEL_I915_DSPCNTR_FORMAT_MASK) {
    case INTEL_I915_DSPCNTR_FORMAT_8BPP:
        return "8bpp";
    default:
        return "other";
    }
}

static uint32_t kernel_drm_i915_pack_timing(uint32_t start, uint32_t end) {
    if (end == 0u || end <= start) {
        return 0u;
    }
    return ((end - 1u) << 16) | (start - 1u);
}

int kernel_drm_i915_restore_display(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_i915_display_snapshot *snapshot) {
    struct kernel_drm_i915_display_snapshot restored;
    int mismatch = 0;

    if (candidate == 0 || snapshot == 0) {
        return -1;
    }

    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DPLL_A, snapshot->dpll_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_PIPECONF_A, snapshot->pipeconf_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_CONF_A, snapshot->transconf_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_HTOTAL_A, snapshot->trans_htotal_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_HBLANK_A, snapshot->trans_hblank_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_HSYNC_A, snapshot->trans_hsync_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_VTOTAL_A, snapshot->trans_vtotal_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_VBLANK_A, snapshot->trans_vblank_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_VSYNC_A, snapshot->trans_vsync_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_PIPESRC_A, snapshot->pipesrc_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPSTRIDE_A, snapshot->dspstride_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPPOS_A, snapshot->dsppos_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPSIZE_A, snapshot->dspsize_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPCNTR_A, snapshot->dspcntr_a);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPSURF_A, snapshot->dspsurf_a);
    if (kernel_drm_i915_snapshot_display(candidate, &restored) != 0) {
        kernel_drm_i915_log_snapshot_failure("restore-readback", candidate);
        return -1;
    }
    if (restored.trans_htotal_a != snapshot->trans_htotal_a) {
        kernel_drm_i915_log_mismatch_u32("restore TRANS_HTOTAL_A", snapshot->trans_htotal_a, restored.trans_htotal_a);
        mismatch = 1;
    }
    if (restored.trans_hblank_a != snapshot->trans_hblank_a) {
        kernel_drm_i915_log_mismatch_u32("restore TRANS_HBLANK_A", snapshot->trans_hblank_a, restored.trans_hblank_a);
        mismatch = 1;
    }
    if (restored.trans_hsync_a != snapshot->trans_hsync_a) {
        kernel_drm_i915_log_mismatch_u32("restore TRANS_HSYNC_A", snapshot->trans_hsync_a, restored.trans_hsync_a);
        mismatch = 1;
    }
    if (restored.trans_vtotal_a != snapshot->trans_vtotal_a) {
        kernel_drm_i915_log_mismatch_u32("restore TRANS_VTOTAL_A", snapshot->trans_vtotal_a, restored.trans_vtotal_a);
        mismatch = 1;
    }
    if (restored.trans_vblank_a != snapshot->trans_vblank_a) {
        kernel_drm_i915_log_mismatch_u32("restore TRANS_VBLANK_A", snapshot->trans_vblank_a, restored.trans_vblank_a);
        mismatch = 1;
    }
    if (restored.trans_vsync_a != snapshot->trans_vsync_a) {
        kernel_drm_i915_log_mismatch_u32("restore TRANS_VSYNC_A", snapshot->trans_vsync_a, restored.trans_vsync_a);
        mismatch = 1;
    }
    if (restored.pipesrc_a != snapshot->pipesrc_a) {
        kernel_drm_i915_log_mismatch_u32("restore PIPESRC_A", snapshot->pipesrc_a, restored.pipesrc_a);
        mismatch = 1;
    }
    if (restored.dspcntr_a != snapshot->dspcntr_a) {
        kernel_drm_i915_log_mismatch_u32("restore DSPCNTR_A", snapshot->dspcntr_a, restored.dspcntr_a);
        mismatch = 1;
    }
    if (restored.dspstride_a != snapshot->dspstride_a) {
        kernel_drm_i915_log_mismatch_u32("restore DSPSTRIDE_A", snapshot->dspstride_a, restored.dspstride_a);
        mismatch = 1;
    }
    if (restored.dsppos_a != snapshot->dsppos_a) {
        kernel_drm_i915_log_mismatch_u32("restore DSPPOS_A", snapshot->dsppos_a, restored.dsppos_a);
        mismatch = 1;
    }
    if (restored.dspsize_a != snapshot->dspsize_a) {
        kernel_drm_i915_log_mismatch_u32("restore DSPSIZE_A", snapshot->dspsize_a, restored.dspsize_a);
        mismatch = 1;
    }
    if (restored.dspsurf_a != snapshot->dspsurf_a) {
        kernel_drm_i915_log_mismatch_u32("restore DSPSURF_A", snapshot->dspsurf_a, restored.dspsurf_a);
        mismatch = 1;
    }
    if (restored.dpll_a != snapshot->dpll_a) {
        kernel_drm_i915_log_mismatch_u32("restore DPLL_A", snapshot->dpll_a, restored.dpll_a);
        mismatch = 1;
    }
    if (restored.pipeconf_a != snapshot->pipeconf_a) {
        kernel_drm_i915_log_mismatch_u32("restore PIPECONF_A", snapshot->pipeconf_a, restored.pipeconf_a);
        mismatch = 1;
    }
    if (restored.transconf_a != snapshot->transconf_a) {
        kernel_drm_i915_log_mismatch_u32("restore TRANS_CONF_A", snapshot->transconf_a, restored.transconf_a);
        mismatch = 1;
    }
    return mismatch == 0 ? 0 : -1;
}

static int kernel_drm_i915_stage_matches_plan(
    const struct kernel_drm_i915_display_snapshot *snapshot,
    const struct kernel_drm_i915_mode_plan *plan,
    uint32_t expected_dspcntr,
    uint32_t expected_dspstride,
    uint32_t expected_dsppos,
    uint32_t expected_dspsize,
    uint32_t expected_dspsurf) {
    if (snapshot == 0 || plan == 0) {
        return 0;
    }

    return snapshot->trans_htotal_a == plan->htotal &&
           snapshot->trans_hblank_a == plan->hblank &&
           snapshot->trans_hsync_a == plan->hsync &&
           snapshot->trans_vtotal_a == plan->vtotal &&
           snapshot->trans_vblank_a == plan->vblank &&
           snapshot->trans_vsync_a == plan->vsync &&
           snapshot->pipesrc_a == plan->pipesrc &&
           snapshot->dspcntr_a == expected_dspcntr &&
           snapshot->dspstride_a == expected_dspstride &&
           snapshot->dsppos_a == expected_dsppos &&
           snapshot->dspsize_a == expected_dspsize &&
           snapshot->dspsurf_a == expected_dspsurf;
}

static void kernel_drm_i915_log_mismatch_u32(const char *name,
                                             uint32_t expected,
                                             uint32_t actual) {
    if (name == 0 || expected == actual) {
        return;
    }
    kernel_debug_printf("i915: readback mismatch %s expected=%x actual=%x\n",
                        name,
                        expected,
                        actual);
}

static void kernel_drm_i915_log_stage_mismatches(
    const struct kernel_drm_i915_display_snapshot *snapshot,
    const struct kernel_drm_i915_mode_plan *plan,
    uint32_t expected_dspcntr,
    uint32_t expected_dspstride,
    uint32_t expected_dsppos,
    uint32_t expected_dspsize,
    uint32_t expected_dspsurf) {
    if (snapshot == 0 || plan == 0) {
        return;
    }

    kernel_drm_i915_log_mismatch_u32("TRANS_HTOTAL_A", plan->htotal, snapshot->trans_htotal_a);
    kernel_drm_i915_log_mismatch_u32("TRANS_HBLANK_A", plan->hblank, snapshot->trans_hblank_a);
    kernel_drm_i915_log_mismatch_u32("TRANS_HSYNC_A", plan->hsync, snapshot->trans_hsync_a);
    kernel_drm_i915_log_mismatch_u32("TRANS_VTOTAL_A", plan->vtotal, snapshot->trans_vtotal_a);
    kernel_drm_i915_log_mismatch_u32("TRANS_VBLANK_A", plan->vblank, snapshot->trans_vblank_a);
    kernel_drm_i915_log_mismatch_u32("TRANS_VSYNC_A", plan->vsync, snapshot->trans_vsync_a);
    kernel_drm_i915_log_mismatch_u32("PIPESRC_A", plan->pipesrc, snapshot->pipesrc_a);
    kernel_drm_i915_log_mismatch_u32("DSPCNTR_A", expected_dspcntr, snapshot->dspcntr_a);
    kernel_drm_i915_log_mismatch_u32("DSPSTRIDE_A", expected_dspstride, snapshot->dspstride_a);
    kernel_drm_i915_log_mismatch_u32("DSPPOS_A", expected_dsppos, snapshot->dsppos_a);
    kernel_drm_i915_log_mismatch_u32("DSPSIZE_A", expected_dspsize, snapshot->dspsize_a);
    kernel_drm_i915_log_mismatch_u32("DSPSURF_A", expected_dspsurf, snapshot->dspsurf_a);
}

static int kernel_drm_i915_scanout_matches_expected(
    const struct kernel_drm_i915_display_snapshot *snapshot,
    uint32_t expected_dspcntr,
    uint32_t expected_dspstride,
    uint32_t expected_dsppos,
    uint32_t expected_dspsize,
    uint32_t expected_dspsurf) {
    if (snapshot == 0) {
        return 0;
    }

    return (snapshot->dspcntr_a & INTEL_I915_DSPCNTR_ENABLE) != 0u &&
           (snapshot->dspcntr_a & INTEL_I915_DSPCNTR_PIPE_SEL_MASK) ==
               INTEL_I915_DSPCNTR_PIPE_SEL_A &&
           (snapshot->dspcntr_a & INTEL_I915_DSPCNTR_FORMAT_MASK) ==
               INTEL_I915_DSPCNTR_FORMAT_8BPP &&
           snapshot->dspcntr_a == expected_dspcntr &&
           snapshot->dspstride_a == expected_dspstride &&
           snapshot->dsppos_a == expected_dsppos &&
           snapshot->dspsize_a == expected_dspsize &&
           snapshot->dspsurf_a == expected_dspsurf;
}

static void kernel_drm_i915_log_scanout_mismatches(
    const struct kernel_drm_i915_display_snapshot *snapshot,
    uint32_t expected_dspcntr,
    uint32_t expected_dspstride,
    uint32_t expected_dsppos,
    uint32_t expected_dspsize,
    uint32_t expected_dspsurf) {
    if (snapshot == 0) {
        return;
    }

    if ((snapshot->dspcntr_a & INTEL_I915_DSPCNTR_ENABLE) == 0u) {
        kernel_debug_puts("i915: readback mismatch DSPCNTR_A expected primary plane enabled\n");
    }
    if ((snapshot->dspcntr_a & INTEL_I915_DSPCNTR_PIPE_SEL_MASK) !=
        INTEL_I915_DSPCNTR_PIPE_SEL_A) {
        kernel_debug_printf("i915: readback mismatch DSPCNTR_A expected pipe=A actual_pipe_bits=%x\n",
                            snapshot->dspcntr_a & INTEL_I915_DSPCNTR_PIPE_SEL_MASK);
    }
    if ((snapshot->dspcntr_a & INTEL_I915_DSPCNTR_FORMAT_MASK) !=
        INTEL_I915_DSPCNTR_FORMAT_8BPP) {
        kernel_debug_printf("i915: readback mismatch DSPCNTR_A expected format=8bpp actual_format=%s raw=%x\n",
                            kernel_drm_i915_plane_format_name(snapshot->dspcntr_a),
                            snapshot->dspcntr_a);
    }
    kernel_drm_i915_log_mismatch_u32("DSPCNTR_A", expected_dspcntr, snapshot->dspcntr_a);
    kernel_drm_i915_log_mismatch_u32("DSPSTRIDE_A", expected_dspstride, snapshot->dspstride_a);
    kernel_drm_i915_log_mismatch_u32("DSPPOS_A", expected_dsppos, snapshot->dsppos_a);
    kernel_drm_i915_log_mismatch_u32("DSPSIZE_A", expected_dspsize, snapshot->dspsize_a);
    kernel_drm_i915_log_mismatch_u32("DSPSURF_A", expected_dspsurf, snapshot->dspsurf_a);
}

int kernel_drm_i915_build_mode_plan(uint32_t width,
                                    uint32_t height,
                                    struct kernel_drm_i915_mode_plan *plan_out) {
    uint32_t hsync_start;
    uint32_t hsync_end;
    uint32_t htotal;
    uint32_t vsync_start;
    uint32_t vsync_end;
    uint32_t vtotal;

    if (plan_out == 0 || width < 320u || height < 200u) {
        return -1;
    }

    hsync_start = width + 48u;
    hsync_end = hsync_start + 32u;
    htotal = width + 48u + 32u + 80u;
    vsync_start = height + 3u;
    vsync_end = vsync_start + 4u;
    vtotal = height + 3u + 4u + 23u;

    if (hsync_end >= htotal || vsync_end >= vtotal) {
        return -1;
    }

    plan_out->width = width;
    plan_out->height = height;
    plan_out->htotal = kernel_drm_i915_pack_timing(width, htotal);
    plan_out->hblank = kernel_drm_i915_pack_timing(width, htotal);
    plan_out->hsync = kernel_drm_i915_pack_timing(hsync_start, hsync_end);
    plan_out->vtotal = kernel_drm_i915_pack_timing(height, vtotal);
    plan_out->vblank = kernel_drm_i915_pack_timing(height, vtotal);
    plan_out->vsync = kernel_drm_i915_pack_timing(vsync_start, vsync_end);
    plan_out->pipesrc = ((width - 1u) << 16) | (height - 1u);
    return 0;
}

int kernel_drm_i915_stage_mode_plan(const struct kernel_drm_candidate *candidate,
                                    const struct kernel_drm_i915_mode_plan *plan,
                                    struct kernel_drm_i915_stage_result *result_out) {
    if (kernel_drm_i915_commit_mode_plan(candidate, plan, result_out) != 0) {
        return -1;
    }

    kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
    return result_out->restore_status == 0 ? 0 : -1;
}

int kernel_drm_i915_commit_mode_plan(const struct kernel_drm_candidate *candidate,
                                     const struct kernel_drm_i915_mode_plan *plan,
                                     struct kernel_drm_i915_stage_result *result_out) {
    uint32_t dspcntr;
    uint32_t dspstride;
    uint32_t dsppos;
    uint32_t dspsize;
    uint32_t dspsurf;

    if (candidate == 0 || plan == 0 || result_out == 0 || candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }
    *result_out = (struct kernel_drm_i915_stage_result){0};
    if (candidate->fb_base == (uintptr_t)0 || (candidate->fb_base & 0xFFFu) != 0u) {
        return -1;
    }

    if (kernel_drm_i915_snapshot_display(candidate, &result_out->before) != 0) {
        kernel_drm_i915_log_snapshot_failure("commit-before", candidate);
        return -1;
    }

    dspstride = kernel_drm_i915_primary_stride_bytes(plan->width);
    dsppos = 0u;
    dspsize = ((plan->height - 1u) << 16) | (plan->width - 1u);
    dspsurf = (uint32_t)candidate->fb_base;
    dspcntr = INTEL_I915_DSPCNTR_ENABLE |
              INTEL_I915_DSPCNTR_PIPE_SEL_A |
              INTEL_I915_DSPCNTR_FORMAT_8BPP;
    if ((result_out->before.dspcntr_a & INTEL_I915_DSPCNTR_PIPE_GAMMA) != 0u) {
        dspcntr |= INTEL_I915_DSPCNTR_PIPE_GAMMA;
    }

    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_HTOTAL_A, plan->htotal);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_HBLANK_A, plan->hblank);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_HSYNC_A, plan->hsync);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_VTOTAL_A, plan->vtotal);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_VBLANK_A, plan->vblank);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_VSYNC_A, plan->vsync);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_PIPESRC_A, plan->pipesrc);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPSTRIDE_A, dspstride);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPPOS_A, dsppos);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPSIZE_A, dspsize);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPCNTR_A, dspcntr);
    kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPSURF_A, dspsurf);

    if (kernel_drm_i915_snapshot_display(candidate, &result_out->after) != 0) {
        kernel_drm_i915_log_snapshot_failure("commit-after", candidate);
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }

    if (!kernel_drm_i915_stage_matches_plan(&result_out->after,
                                            plan,
                                            dspcntr,
                                            dspstride,
                                            dsppos,
                                            dspsize,
                                            dspsurf)) {
        kernel_drm_i915_log_stage_mismatches(&result_out->after,
                                             plan,
                                             dspcntr,
                                             dspstride,
                                             dsppos,
                                             dspsize,
                                             dspsurf);
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }
    if (!kernel_drm_i915_scanout_matches_expected(&result_out->after,
                                                  dspcntr,
                                                  dspstride,
                                                  dsppos,
                                                  dspsize,
                                                  dspsurf)) {
        kernel_drm_i915_log_scanout_mismatches(&result_out->after,
                                               dspcntr,
                                               dspstride,
                                               dsppos,
                                               dspsize,
                                               dspsurf);
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }
    return 0;
}

static void kernel_drm_i915_delay_loop(void) {
    uint32_t i;
    for (i = 0u; i < 100000u; ++i) {
        __asm__ __volatile__ ("pause");
    }
}

static int kernel_drm_i915_program_pll(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_i915_mode_plan *plan,
    uint8_t gen) {
    uint32_t ref_freq_khz;
    uint32_t htotal_val;
    uint32_t vtotal_val;
    uint32_t pixel_clock_khz;
    uint32_t n_reg;
    uint32_t p1_div;
    uint32_t dpll_val;

    if (candidate == 0 || plan == 0) {
        return -1;
    }

    if (gen >= 6u) {
        ref_freq_khz = 120000u;
    } else {
        ref_freq_khz = 100000u;
    }

    htotal_val = ((plan->htotal >> 16) & 0xFFFFu) + 1u;
    vtotal_val = ((plan->vtotal >> 16) & 0xFFFFu) + 1u;
    if (htotal_val == 0u || vtotal_val == 0u) {
        return -1;
    }

    pixel_clock_khz = (htotal_val * vtotal_val * 60u) / 1000u;
    if (pixel_clock_khz < 10000u) {
        pixel_clock_khz = 25175u;
    }
    if (pixel_clock_khz > 500000u) {
        pixel_clock_khz = 500000u;
    }

    n_reg = 0u;
    p1_div = 16u;

    {
        uint32_t vco_khz = pixel_clock_khz * p1_div;
        uint32_t m_total;

        while (vco_khz < 1600000u && p1_div < 64u) {
            p1_div += 1u;
            vco_khz = pixel_clock_khz * p1_div;
        }

        m_total = (pixel_clock_khz * (n_reg + 2u)) / ref_freq_khz;
        if (m_total < 2u) {
            m_total = 2u;
        }
        if (m_total > 200u) {
            m_total = 200u;
        }

        dpll_val = INTEL_I915_DPLL_VCO_ENABLE |
                   ((p1_div << INTEL_I915_DPLL_P1_SHIFT) & INTEL_I915_DPLL_P1_MASK) |
                   ((n_reg << INTEL_I915_DPLL_N_SHIFT) & INTEL_I915_DPLL_N_MASK);

        kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DPLL_A, dpll_val);

        {
            uint32_t divider_val = ((m_total + 2u) << 24);
            kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DPLL_DIVIDER_A, divider_val);
        }

        kernel_debug_printf("i915: pll gen=%d ref=%u pix=%u n=%u p1=%u m=%u dpll=%x\n",
                           (int)gen, ref_freq_khz, pixel_clock_khz,
                           n_reg + 2u, p1_div, m_total, dpll_val);
    }

    kernel_drm_i915_delay_loop();
    return 0;
}

static int kernel_drm_i915_enable_pipe_chain(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_i915_mode_plan *plan,
    struct kernel_drm_i915_stage_result *result_out,
    uint8_t gen) {

    if (kernel_drm_i915_program_pll(candidate, plan, gen) != 0) {
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }

    {
        uint32_t transconf = INTEL_I915_TRANS_CONF_ENABLE;
        kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_TRANS_CONF_A, transconf);
    }

    {
        uint32_t pipeconf = INTEL_I915_PIPECONF_ENABLE;
        kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_PIPECONF_A, pipeconf);
    }

    {
        uint32_t dspcntr = INTEL_I915_DSPCNTR_ENABLE |
                           INTEL_I915_DSPCNTR_PIPE_SEL_A |
                           INTEL_I915_DSPCNTR_FORMAT_8BPP;
        if ((result_out->before.dspcntr_a & INTEL_I915_DSPCNTR_PIPE_GAMMA) != 0u) {
            dspcntr |= INTEL_I915_DSPCNTR_PIPE_GAMMA;
        }
        kernel_drm_i915_mmio_write32(candidate, INTEL_I915_MMIO_DSPCNTR_A, dspcntr);
    }

    return 0;
}

int kernel_drm_i915_full_commit_mode_plan(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_i915_mode_plan *plan,
    struct kernel_drm_i915_stage_result *result_out) {
    uint8_t gen;

    if (candidate == 0 || plan == 0 || result_out == 0) {
        return -1;
    }

    gen = kernel_drm_i915_guess_gen(candidate->pci.device_id);
    if (gen < 5u) {
        return -1;
    }

    if (kernel_drm_i915_commit_mode_plan(candidate, plan, result_out) != 0) {
        return -1;
    }

    if (kernel_drm_i915_enable_pipe_chain(candidate, plan, result_out, gen) != 0) {
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }

    if (kernel_drm_i915_snapshot_display(candidate, &result_out->after) != 0) {
        kernel_drm_i915_log_snapshot_failure("full-commit-after", candidate);
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }

    if ((result_out->after.dpll_a & INTEL_I915_DPLL_VCO_ENABLE) == 0u) {
        kernel_debug_printf("i915: full-commit DPLL_A VCO not enabled after write\n");
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }
    if ((result_out->after.pipeconf_a & INTEL_I915_PIPECONF_ENABLE) == 0u) {
        kernel_debug_printf("i915: full-commit PIPECONF_A pipe not enabled\n");
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }
    if ((result_out->after.transconf_a & INTEL_I915_TRANS_CONF_ENABLE) == 0u) {
        kernel_debug_printf("i915: full-commit TRANS_CONF_A transcoder not enabled\n");
        kernel_drm_i915_record_restore_result(candidate, &result_out->before, result_out);
        return -1;
    }

    kernel_debug_puts("i915: full-commit pipe chain enabled ok\n");
    return 0;
}
