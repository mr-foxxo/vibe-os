#ifndef KERNEL_DRIVERS_VIDEO_DRM_H
#define KERNEL_DRIVERS_VIDEO_DRM_H

#include <stddef.h>
#include <stdint.h>

#include <include/userland_api.h>
#include <kernel/drivers/pci/pci.h>

enum kernel_drm_backend_kind {
    KERNEL_DRM_BACKEND_NONE = 0,
    KERNEL_DRM_BACKEND_BGA,
    KERNEL_DRM_BACKEND_I915,
    KERNEL_DRM_BACKEND_RADEON,
    KERNEL_DRM_BACKEND_NOUVEAU,
    KERNEL_DRM_BACKEND_UNKNOWN
};

struct kernel_drm_candidate {
    enum kernel_drm_backend_kind backend_kind;
    const char *backend_name;
    const char *device_name;
    struct kernel_pci_device_info pci;
    uintptr_t mmio_base;
    size_t mmio_size;
    uintptr_t fb_base;
    size_t fb_size;
};

struct kernel_drm_backend_ops {
    enum kernel_drm_backend_kind kind;
    const char *name;
    int (*probe)(const struct kernel_drm_candidate *candidate);
    int (*set_mode)(const struct kernel_drm_candidate *candidate,
                    uint32_t width,
                    uint32_t height,
                    uint16_t mode_id,
                    struct video_mode *mode_out);
    int (*revert_last_modeset)(void);
    void (*forget_last_modeset)(void);
    void (*prepare_for_bios_modeset)(void);
};

size_t kernel_drm_probe_candidates(struct kernel_drm_candidate *out, size_t capacity);
void kernel_drm_log_candidates(void);
int kernel_drm_try_set_mode(uint32_t width,
                            uint32_t height,
                            uint16_t mode_id,
                            struct video_mode *mode_out);
enum kernel_drm_backend_kind kernel_drm_active_backend_kind(void);
const char *kernel_drm_active_backend_name(void);
enum kernel_drm_backend_kind kernel_drm_detected_backend_kind(void);
const char *kernel_drm_detected_backend_name(void);
const struct kernel_pci_device_info *kernel_drm_active_pci_info(void);
const struct kernel_pci_device_info *kernel_drm_detected_pci_info(void);
int kernel_drm_revert_last_modeset(void);
void kernel_drm_forget_last_modeset(void);
void kernel_drm_prepare_for_bios_modeset(void);

extern const struct kernel_drm_backend_ops g_kernel_drm_bga_ops;
extern const struct kernel_drm_backend_ops g_kernel_drm_i915_ops;
extern const struct kernel_drm_backend_ops g_kernel_drm_radeon_ops;
extern const struct kernel_drm_backend_ops g_kernel_drm_nouveau_ops;

int kernel_drm_bga_revert_last_mode(void);
void kernel_drm_bga_forget_last_mode(void);
void kernel_drm_bga_prepare_for_bios_modeset(void);
int kernel_drm_i915_revert_last_commit(void);
void kernel_drm_i915_forget_last_commit(void);
int kernel_drm_radeon_revert_last_commit(void);
void kernel_drm_radeon_forget_last_commit(void);
void kernel_drm_radeon_enable_modeset(void);

int kernel_drm_ioctl(int cmd, void *arg, size_t arg_size);

#endif
