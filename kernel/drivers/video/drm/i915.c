#include <kernel/drivers/video/drm/drm.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/drm/i915/i915.h>
#include <kernel/bootinfo.h>

#ifndef INTEL_I915_EXPERIMENTAL_COMMIT
#define INTEL_I915_EXPERIMENTAL_COMMIT 1
#endif

static int g_kernel_drm_i915_last_commit_valid = 0;
static struct kernel_drm_candidate g_kernel_drm_i915_last_commit_candidate;
static struct kernel_drm_i915_stage_result g_kernel_drm_i915_last_commit_stage;
static struct bootinfo_vesa g_kernel_drm_i915_last_commit_boot_vesa;
static uint32_t g_kernel_drm_i915_last_commit_boot_flags = 0u;
static int g_kernel_drm_i915_modeset_active = 1;
static struct kernel_drm_candidate g_kernel_drm_i915_active_candidate = {0};

static uint32_t kernel_drm_i915_primary_stride_bytes(uint32_t width) {
    return (width + 63u) & ~63u;
}

static void kernel_drm_i915_log_snapshot_failure(
    const char *stage,
    const struct kernel_drm_candidate *candidate) {
    kernel_debug_printf("i915: snapshot failed stage=%s mmio=%x fb=%x\n",
                        stage != 0 ? stage : "unknown",
                        candidate != 0 ? (uint32_t)candidate->mmio_base : 0u,
                        candidate != 0 ? (uint32_t)candidate->fb_base : 0u);
}

static int kernel_drm_i915_verify_boot_mode(
    const char *reason,
    const struct bootinfo_vesa *expected_vesa,
    uint32_t expected_flags);

static void kernel_drm_i915_clear_mode_out(struct video_mode *mode_out) {
    if (mode_out == 0) {
        return;
    }
    *mode_out = (struct video_mode){0};
}

static void kernel_drm_i915_delay_loop(void) {
    uint32_t i;
    for (i = 0u; i < 100000u; ++i) {
        __asm__ __volatile__ ("pause");
    }
}

static void kernel_drm_i915_prepare_for_bios_modeset(void) {
    uint32_t val;

    if (g_kernel_drm_i915_active_candidate.mmio_base == (uintptr_t)0) {
        kernel_debug_puts("i915: prepare skipped no candidate\n");
        return;
    }

    kernel_debug_puts("i915: prepare for bios modeset\n");

    val = kernel_drm_i915_mmio_read32(&g_kernel_drm_i915_active_candidate,
                                       INTEL_I915_MMIO_DSPCNTR_A);
    val &= ~INTEL_I915_DSPCNTR_ENABLE;
    kernel_drm_i915_mmio_write32(&g_kernel_drm_i915_active_candidate,
                                  INTEL_I915_MMIO_DSPCNTR_A, val);

    kernel_drm_i915_delay_loop();

    val = kernel_drm_i915_mmio_read32(&g_kernel_drm_i915_active_candidate,
                                       INTEL_I915_MMIO_PIPECONF_A);
    val &= ~INTEL_I915_PIPECONF_ENABLE;
    kernel_drm_i915_mmio_write32(&g_kernel_drm_i915_active_candidate,
                                  INTEL_I915_MMIO_PIPECONF_A, val);

    val = kernel_drm_i915_mmio_read32(&g_kernel_drm_i915_active_candidate,
                                       INTEL_I915_MMIO_TRANS_CONF_A);
    val &= ~INTEL_I915_TRANS_CONF_ENABLE;
    kernel_drm_i915_mmio_write32(&g_kernel_drm_i915_active_candidate,
                                  INTEL_I915_MMIO_TRANS_CONF_A, val);

    val = kernel_drm_i915_mmio_read32(&g_kernel_drm_i915_active_candidate,
                                       INTEL_I915_MMIO_DPLL_A);
    val &= ~INTEL_I915_DPLL_VCO_ENABLE;
    kernel_drm_i915_mmio_write32(&g_kernel_drm_i915_active_candidate,
                                  INTEL_I915_MMIO_DPLL_A, val);

    kernel_debug_puts("i915: prepare done\n");
}

static size_t kernel_drm_i915_required_frame_bytes(uint32_t width, uint32_t height) {
    return (size_t)kernel_drm_i915_primary_stride_bytes(width) * (size_t)height;
}

static int kernel_drm_i915_aperture_can_fit(const struct kernel_drm_candidate *candidate,
                                            size_t required_frame_bytes) {
    return candidate != 0 &&
           candidate->fb_size != 0u &&
           required_frame_bytes != 0u &&
           required_frame_bytes <= candidate->fb_size;
}

static int kernel_drm_i915_commit_enabled(void) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;

    if (INTEL_I915_EXPERIMENTAL_COMMIT != 0) {
        return 1;
    }
    if (g_kernel_drm_i915_modeset_active) {
        return 1;
    }
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return 0;
    }
    return (bootinfo->flags & BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT) != 0u;
}

static const char *kernel_drm_i915_commit_enable_source(void) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;

    if (INTEL_I915_EXPERIMENTAL_COMMIT != 0) {
        return "build";
    }
    if (g_kernel_drm_i915_modeset_active) {
        return "runtime";
    }
    if (bootinfo->magic == BOOTINFO_MAGIC &&
        bootinfo->version == BOOTINFO_VERSION &&
        (bootinfo->flags & BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT) != 0u) {
        return "boot";
    }
    return "disabled";
}

static int kernel_drm_i915_fill_mode_out(const struct kernel_drm_candidate *candidate,
                                         uint32_t width,
                                         uint32_t height,
                                         struct video_mode *mode_out) {
    size_t frame_bytes;

    if (candidate == 0 || mode_out == 0 || candidate->fb_base == (uintptr_t)0) {
        return -1;
    }
    frame_bytes = kernel_drm_i915_required_frame_bytes(width, height);
    if (!kernel_drm_i915_aperture_can_fit(candidate, frame_bytes)) {
        return -1;
    }

    mode_out->fb_addr = (uint32_t)candidate->fb_base;
    mode_out->width = width;
    mode_out->height = height;
    mode_out->pitch = kernel_drm_i915_primary_stride_bytes(width);
    mode_out->bpp = 8u;
    return 0;
}

static void kernel_drm_i915_log_display_transition(
    const char *reason,
    const struct kernel_drm_i915_display_snapshot *before,
    const struct kernel_drm_i915_display_snapshot *after) {
    const char *before_format;
    const char *after_format;

    if (before == 0 || after == 0) {
        return;
    }

    before_format = ((before->dspcntr_a & INTEL_I915_DSPCNTR_FORMAT_MASK) ==
                     INTEL_I915_DSPCNTR_FORMAT_8BPP)
                        ? "8bpp"
                        : "other";
    after_format = ((after->dspcntr_a & INTEL_I915_DSPCNTR_FORMAT_MASK) ==
                    INTEL_I915_DSPCNTR_FORMAT_8BPP)
                       ? "8bpp"
                       : "other";

    kernel_debug_printf("i915: %s before pipesrc=%x htotal=%x vtotal=%x dspcntr=%x format=%s stride=%x surf=%x | after pipesrc=%x htotal=%x vtotal=%x dspcntr=%x format=%s stride=%x surf=%x\n",
                        reason != 0 ? reason : "display-transition",
                        before->pipesrc_a,
                        before->trans_htotal_a,
                        before->trans_vtotal_a,
                        before->dspcntr_a,
                        before_format,
                        before->dspstride_a,
                        before->dspsurf_a,
                        after->pipesrc_a,
                        after->trans_htotal_a,
                        after->trans_vtotal_a,
                        after->dspcntr_a,
                        after_format,
                        after->dspstride_a,
                        after->dspsurf_a);
}

static void kernel_drm_i915_log_restore_status(
    const char *reason,
    const struct kernel_drm_i915_stage_result *stage) {
    if (stage == 0) {
        return;
    }

    if (stage->restore_status == 0) {
        kernel_drm_i915_log_display_transition(reason, &stage->after, &stage->restored);
        kernel_debug_printf("i915: %s rollback verified\n",
                            reason != 0 ? reason : "restore");
        return;
    }

    kernel_debug_printf("i915: %s rollback failed\n",
                        reason != 0 ? reason : "restore");
}

static int kernel_drm_i915_restore_and_log(
    const struct kernel_drm_candidate *candidate,
    struct kernel_drm_i915_stage_result *stage,
    const char *reason) {
    if (candidate == 0 || stage == 0) {
        return -1;
    }

    stage->restore_status = kernel_drm_i915_restore_display(candidate, &stage->before);
    if (stage->restore_status == 0) {
        if (kernel_drm_i915_snapshot_display(candidate, &stage->restored) != 0) {
            kernel_drm_i915_log_snapshot_failure("restore-log", candidate);
            stage->restored = (struct kernel_drm_i915_display_snapshot){0};
            stage->restore_status = -1;
        }
    } else {
        stage->restored = (struct kernel_drm_i915_display_snapshot){0};
    }
    kernel_drm_i915_log_restore_status(reason, stage);
    return stage->restore_status;
}

static int kernel_drm_i915_publish_boot_mode(uint16_t mode_id,
                                             const struct video_mode *mode) {
    volatile struct bootinfo *bootinfo;
    struct bootinfo_vesa expected_vesa;
    uint32_t expected_flags;

    if (mode == 0 ||
        mode->fb_addr < 0x00100000u ||
        mode->width > 0xFFFFu ||
        mode->height > 0xFFFFu ||
        mode->pitch > 0xFFFFu ||
        mode->bpp != 8u) {
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
    return kernel_drm_i915_verify_boot_mode("modeset-commit publish",
                                            &expected_vesa,
                                            expected_flags);
}

static int kernel_drm_i915_snapshot_boot_mode(struct bootinfo_vesa *vesa_out,
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

static int kernel_drm_i915_boot_mode_matches(const struct bootinfo_vesa *expected_vesa,
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

static void kernel_drm_i915_log_boot_mode_transition(
    const char *reason,
    const struct bootinfo_vesa *expected_vesa,
    uint32_t expected_flags,
    const struct bootinfo_vesa *actual_vesa,
    uint32_t actual_flags) {
    if (expected_vesa == 0 || actual_vesa == 0) {
        return;
    }

    kernel_debug_printf("i915: %s bootinfo expected mode=%x fb=%x pitch=%x %ux%u bpp=%u valid=%u | actual mode=%x fb=%x pitch=%x %ux%u bpp=%u valid=%u\n",
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

static int kernel_drm_i915_verify_boot_mode(
    const char *reason,
    const struct bootinfo_vesa *expected_vesa,
    uint32_t expected_flags) {
    struct bootinfo_vesa actual_vesa;
    uint32_t actual_flags;

    if (kernel_drm_i915_snapshot_boot_mode(&actual_vesa, &actual_flags) != 0) {
        kernel_debug_printf("i915: %s bootinfo readback failed\n",
                            reason != 0 ? reason : "bootinfo");
        return -1;
    }
    if (!kernel_drm_i915_boot_mode_matches(expected_vesa,
                                           expected_flags,
                                           &actual_vesa,
                                           actual_flags)) {
        kernel_drm_i915_log_boot_mode_transition(reason,
                                                 expected_vesa,
                                                 expected_flags,
                                                 &actual_vesa,
                                                 actual_flags);
        return -1;
    }
    return 0;
}

static int kernel_drm_i915_prepare_commit_handoff(
    const struct kernel_drm_candidate *candidate,
    uint32_t width,
    uint32_t height,
    struct video_mode *mode_out,
    struct bootinfo_vesa *boot_vesa_out,
    uint32_t *boot_flags_out) {
    if (kernel_drm_i915_fill_mode_out(candidate, width, height, mode_out) != 0) {
        kernel_debug_printf("i915: modeset-commit preflight reject mode_out %dx%d fb=%x aperture=%x\n",
                            (int)width,
                            (int)height,
                            candidate != 0 ? (uint32_t)candidate->fb_base : 0u,
                            candidate != 0 ? (uint32_t)candidate->fb_size : 0u);
        return -1;
    }
    if (kernel_drm_i915_snapshot_boot_mode(boot_vesa_out, boot_flags_out) != 0) {
        kernel_debug_puts("i915: modeset-commit preflight reject bootinfo snapshot\n");
        return -1;
    }
    return 0;
}

static int kernel_drm_i915_restore_boot_mode(const struct bootinfo_vesa *vesa,
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
    return kernel_drm_i915_verify_boot_mode("modeset-commit revert",
                                            vesa,
                                            flags);
}

static int kernel_drm_i915_restore_boot_mode_and_log(const struct bootinfo_vesa *vesa,
                                                     uint32_t flags,
                                                     const char *reason) {
    if (kernel_drm_i915_restore_boot_mode(vesa, flags) != 0) {
        kernel_debug_printf("i915: %s bootinfo rollback failed\n",
                            reason != 0 ? reason : "bootinfo");
        return -1;
    }
    return 0;
}

static void kernel_drm_i915_remember_last_commit(
    const struct kernel_drm_candidate *candidate,
    const struct kernel_drm_i915_stage_result *stage,
    const struct bootinfo_vesa *boot_vesa,
    uint32_t boot_flags) {
    if (candidate == 0 || stage == 0 || boot_vesa == 0) {
        g_kernel_drm_i915_last_commit_valid = 0;
        return;
    }

    g_kernel_drm_i915_last_commit_candidate = *candidate;
    g_kernel_drm_i915_last_commit_stage = *stage;
    g_kernel_drm_i915_last_commit_boot_vesa = *boot_vesa;
    g_kernel_drm_i915_last_commit_boot_flags = boot_flags;
    g_kernel_drm_i915_last_commit_valid = 1;
}

void kernel_drm_i915_forget_last_commit(void) {
    g_kernel_drm_i915_last_commit_valid = 0;
    g_kernel_drm_i915_last_commit_candidate = (struct kernel_drm_candidate){0};
    g_kernel_drm_i915_last_commit_stage = (struct kernel_drm_i915_stage_result){0};
    g_kernel_drm_i915_last_commit_boot_vesa = (struct bootinfo_vesa){0};
    g_kernel_drm_i915_last_commit_boot_flags = 0u;
}

void kernel_drm_i915_enable_modeset(void) {
    g_kernel_drm_i915_modeset_active = 1;
    kernel_debug_puts("i915: modeset active (runtime flag)\n");
}

static int kernel_drm_i915_probe(const struct kernel_drm_candidate *candidate) {
    struct kernel_drm_i915_probe_info probe;
    struct kernel_drm_i915_mmio_snapshot mmio;
    struct kernel_drm_i915_display_snapshot display;
    int have_mmio_snapshot = 0;
    int have_display_snapshot = 0;

    if (candidate == 0) {
        return -1;
    }
    g_kernel_drm_i915_active_candidate = *candidate;

    if (!kernel_drm_i915_is_supported_device(candidate->pci.device_id)) {
        kernel_debug_printf("i915: unsupported device id=%x\n",
                            candidate->pci.device_id);
        return -1;
    }
    if (!kernel_drm_i915_is_modeset_supported_device(candidate->pci.device_id)) {
        kernel_debug_printf("i915: device id=%x recognized but staged modeset currently requires gen5+, got platform=%s gen=%d\n",
                            candidate->pci.device_id,
                            kernel_drm_i915_platform_name(candidate->pci.device_id),
                            (int)kernel_drm_i915_guess_gen(candidate->pci.device_id));
        return -1;
    }

    if (kernel_drm_i915_read_probe_info(candidate, &probe) != 0) {
        return -1;
    }
    if ((probe.command & PCI_COMMAND_MEMORY_SPACE) == 0u) {
        return -1;
    }
    if (candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }
    if (probe.gen == 0u) {
        return -1;
    }
    if (kernel_drm_i915_snapshot_mmio(candidate, &mmio) == 0) {
        have_mmio_snapshot = 1;
    }
    if (kernel_drm_i915_snapshot_display(candidate, &display) == 0) {
        have_display_snapshot = 1;
    }

    kernel_debug_printf("i915: probe dev=%x platform=%s gen=%d mmio=%x gmadr=%x ggc=%x bsm=%x stolen_mb=%d opregion=%x\n",
                        candidate->pci.device_id,
                        probe.platform_name,
                        (int)probe.gen,
                        (uint32_t)candidate->mmio_base,
                        (uint32_t)candidate->fb_base,
                        (uint32_t)probe.gmch_ctl,
                        probe.bsm,
                        (int)(probe.stolen_size / (1024u * 1024u)),
                        probe.opregion);
    if (have_mmio_snapshot) {
        kernel_debug_printf("i915: mmio dpll_test=%x d_state=%x disp_arb_ctl=%x\n",
                            mmio.dpll_test,
                            mmio.d_state,
                            mmio.disp_arb_ctl);
    }
    if (have_display_snapshot) {
        const char *plane_format =
            ((display.dspcntr_a & INTEL_I915_DSPCNTR_FORMAT_MASK) ==
             INTEL_I915_DSPCNTR_FORMAT_8BPP)
                ? "8bpp"
                : "other";

        kernel_debug_printf("i915: display htotal=%x hblank=%x hsync=%x vtotal=%x vblank=%x vsync=%x pipesrc=%x dspcntr=%x format=%s stride=%x surf=%x aperture=%x fuse=%x arb2=%x dfsm=%x\n",
                            display.trans_htotal_a,
                            display.trans_hblank_a,
                            display.trans_hsync_a,
                            display.trans_vtotal_a,
                            display.trans_vblank_a,
                            display.trans_vsync_a,
                            display.pipesrc_a,
                            display.dspcntr_a,
                            plane_format,
                            display.dspstride_a,
                            display.dspsurf_a,
                            (uint32_t)candidate->fb_size,
                            display.fuse_strap,
                            display.disp_arb_ctl2,
                            display.skl_dfsm);
    }
    kernel_drm_i915_enable_modeset();
    return 0;
}

static int kernel_drm_i915_set_mode(const struct kernel_drm_candidate *candidate,
                                    uint32_t width,
                                    uint32_t height,
                                    uint16_t mode_id,
                                    struct video_mode *mode_out) {
    struct kernel_drm_i915_mode_plan plan;
    struct kernel_drm_i915_stage_result stage = {0};
    struct kernel_drm_i915_probe_info probe;
    struct video_mode preflight_mode = {0};
    struct bootinfo_vesa previous_boot_vesa = {0};
    uint32_t previous_boot_flags = 0u;
    size_t required_frame_bytes;

    (void)mode_id;

    kernel_drm_i915_clear_mode_out(mode_out);
    kernel_drm_i915_forget_last_commit();

    if (candidate == 0) {
        return -1;
    }
    if (kernel_drm_i915_read_probe_info(candidate, &probe) != 0) {
        return -1;
    }
    if (probe.gen < 5u) {
        kernel_debug_printf("i915: staged modeset currently targets gen5+ only, got gen=%d\n",
                            (int)probe.gen);
        return -1;
    }
    if (kernel_drm_i915_build_mode_plan(width, height, &plan) != 0) {
        return -1;
    }
    required_frame_bytes = kernel_drm_i915_required_frame_bytes(width, height);
    if (candidate->fb_size != 0u && required_frame_bytes > candidate->fb_size) {
        kernel_debug_printf("i915: aperture too small required=%x available=%x for %dx%d\n",
                            (uint32_t)required_frame_bytes,
                            (uint32_t)candidate->fb_size,
                            (int)width,
                            (int)height);
        return -1;
    }

    kernel_debug_printf("i915: modeset-plan %dx%d htotal=%x hblank=%x hsync=%x vtotal=%x vblank=%x vsync=%x pipesrc=%x frame_bytes=%x aperture=%x\n",
                        (int)width,
                        (int)height,
                        plan.htotal,
                        plan.hblank,
                        plan.hsync,
                        plan.vtotal,
                        plan.vblank,
                        plan.vsync,
                        plan.pipesrc,
                        (uint32_t)required_frame_bytes,
                        (uint32_t)candidate->fb_size);

    if (kernel_drm_i915_commit_enabled()) {
        kernel_debug_printf("i915: modeset-commit path enabled via %s\n",
                            kernel_drm_i915_commit_enable_source());
        if (candidate->fb_size == 0u) {
            kernel_debug_puts("i915: modeset-commit blocked because aperture size is unknown\n");
            return -1;
        }
        if (kernel_drm_i915_prepare_commit_handoff(candidate,
                                                   width,
                                                   height,
                                                   &preflight_mode,
                                                   &previous_boot_vesa,
                                                   &previous_boot_flags) != 0) {
            return -1;
        }
        if (kernel_drm_i915_full_commit_mode_plan(candidate, &plan, &stage) != 0) {
            kernel_drm_i915_log_display_transition("modeset-commit reject",
                                                   &stage.before,
                                                   &stage.after);
            kernel_drm_i915_log_restore_status("modeset-commit", &stage);
            kernel_drm_i915_clear_mode_out(mode_out);
            kernel_debug_printf("i915: modeset-commit failed for %dx%d\n",
                                (int)width,
                                (int)height);
            return -1;
        }
        if (mode_out != 0) {
            *mode_out = preflight_mode;
        }
        if (kernel_drm_i915_publish_boot_mode(mode_id, &preflight_mode) != 0) {
            (void)kernel_drm_i915_restore_and_log(candidate,
                                                  &stage,
                                                  "modeset-commit bootinfo");
            (void)kernel_drm_i915_restore_boot_mode_and_log(&previous_boot_vesa,
                                                            previous_boot_flags,
                                                            "modeset-commit bootinfo");
            kernel_drm_i915_clear_mode_out(mode_out);
            kernel_debug_puts("i915: modeset-commit rollback after bootinfo handoff failure\n");
            return -1;
        }
        kernel_drm_i915_remember_last_commit(candidate,
                                             &stage,
                                             &previous_boot_vesa,
                                             previous_boot_flags);

        kernel_debug_printf("i915: commit ok pipesrc=%x dspcntr=%x stride=%x surf=%x fb=%x mode=%x\n",
                            stage.after.pipesrc_a,
                            stage.after.dspcntr_a,
                            stage.after.dspstride_a,
                            stage.after.dspsurf_a,
                            preflight_mode.fb_addr,
                            mode_id);
        return 0;
    }

    if (kernel_drm_i915_stage_mode_plan(candidate, &plan, &stage) != 0) {
        kernel_drm_i915_log_display_transition("modeset-stage reject",
                                               &stage.before,
                                               &stage.after);
        kernel_drm_i915_log_restore_status("modeset-stage", &stage);
        kernel_drm_i915_clear_mode_out(mode_out);
        kernel_debug_printf("i915: modeset-stage readback mismatch for %dx%d\n",
                            (int)width,
                            (int)height);
        return -1;
    }

    kernel_drm_i915_log_display_transition("stage ok", &stage.before, &stage.after);
    kernel_drm_i915_log_restore_status("modeset-stage", &stage);
    kernel_drm_i915_clear_mode_out(mode_out);
    kernel_debug_puts("i915: timing + primary plane stage/restore ok; enable modeset to try persistent handoff\n");
    return -1;
}

int kernel_drm_i915_revert_last_commit(void) {
    struct kernel_drm_candidate candidate;
    struct kernel_drm_i915_stage_result stage;
    struct bootinfo_vesa boot_vesa;
    uint32_t boot_flags;
    int restore_rc;

    if (!g_kernel_drm_i915_last_commit_valid) {
        return -1;
    }

    candidate = g_kernel_drm_i915_last_commit_candidate;
    stage = g_kernel_drm_i915_last_commit_stage;
    boot_vesa = g_kernel_drm_i915_last_commit_boot_vesa;
    boot_flags = g_kernel_drm_i915_last_commit_boot_flags;
    kernel_drm_i915_forget_last_commit();

    restore_rc = kernel_drm_i915_restore_and_log(&candidate,
                                                 &stage,
                                                 "modeset-commit revert");
    if (restore_rc != 0) {
        return -1;
    }
    if (kernel_drm_i915_restore_boot_mode(&boot_vesa, boot_flags) != 0) {
        kernel_debug_puts("i915: modeset-commit revert bootinfo restore failed\n");
        return -1;
    }

    kernel_debug_puts("i915: modeset-commit revert ok\n");
    return 0;
}

const struct kernel_drm_backend_ops g_kernel_drm_i915_ops = {
    KERNEL_DRM_BACKEND_I915,
    "native_gpu_i915",
    kernel_drm_i915_probe,
    kernel_drm_i915_set_mode,
    kernel_drm_i915_revert_last_commit,
    kernel_drm_i915_forget_last_commit,
    kernel_drm_i915_prepare_for_bios_modeset
};
