#ifndef KERNEL_DRIVERS_VIDEO_DRM_NOUVEAU_H
#define KERNEL_DRIVERS_VIDEO_DRM_NOUVEAU_H

#include <stdint.h>

/* NV40 (GeForce 6xxx / 7xxx) display controller register definitions */

/* CRTC0 MMIO base offset and size */
#define NV_PCRTC0_START 0x00600000u
#define NV_PCRTC0_SIZE  0x00001000u

/* CRTC register offsets (relative to CRTC base) */
#define NV_PCRTC_INTR        0x000000u
#define NV_PCRTC_INTR_EN     0x000004u
#define NV_PCRTC_CONFIG      0x000008u
#define NV_PCRTC_RASTER      0x000200u
#define NV_PCRTC_HORIZ       0x000204u
#define NV_PCRTC_VERT        0x000208u
#define NV_PCRTC_HSYNC       0x00020Cu
#define NV_PCRTC_VSYNC       0x000210u
#define NV_PCRTC_START       0x000300u
#define NV_PCRTC_PITCH       0x000308u

/* PRAMDAC MMIO base offset and size */
#define NV_PRAMDAC0          0x00680000u
#define NV_PRAMDAC0_SIZE     0x00001000u
#define NV_PRAMDAC_DACCLK     0x000000u
#define NV_PRAMDAC_DACCONTROL 0x000058u

/* Acceleration engine register bases (stubs only, not yet used for modeset) */
#define NV_PGRAPH            0x00400000u
#define NV_PFIFO             0x00002000u
#define NV_PRAMIN            0x00007000u
#define NV_PFIFO_CACHE1      0x00002000u

/* CRTC config register bits */
#define NV_PCRTC_CONFIG_ENABLE (1u << 0)

/* PRAMDAC DAC control register bits */
#define NV_PRAMDAC_DACCONTROL_ENABLE (1u << 0)

/* Register encoding helpers */
#define NV_ENCODE_HORIZ(htotal, hdisplay_end) \
    (((hdisplay_end) << 16) | (htotal))
#define NV_ENCODE_VERT(vtotal, vdisplay_end) \
    (((vdisplay_end) << 16) | (vtotal))
#define NV_ENCODE_HSYNC(hsync_start, hsync_end) \
    (((hsync_start) << 16) | (hsync_end))
#define NV_ENCODE_VSYNC(vsync_start, vsync_end) \
    (((vsync_start) << 16) | (vsync_end))

/* Enable modeset (off by default for probe-only safety) */
void kernel_drm_nouveau_enable_modeset(void);

#endif
