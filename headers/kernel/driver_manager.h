#ifndef KERNEL_DRIVER_MANAGER_H
#define KERNEL_DRIVER_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/pci_bus.h>

/* simple driver registration API */
int register_driver(const char *name, const char *type, void (*init)(void));
void driver_manager_init(void);

/* PCI driver registration */
#define PCI_DRIVER_MAX_ID 16
#define PCI_DRIVER_MAX 16

typedef int (*pci_driver_probe_fn)(pci_device_t *dev);

typedef struct {
    uint16_t vendors[PCI_DRIVER_MAX_ID];
    uint16_t devices[PCI_DRIVER_MAX_ID];
    uint16_t class_code;
    uint16_t subclass;
    uint16_t prog_if;
    int use_class;
    int count;
} pci_driver_match_t;

int register_pci_driver(const char *name, const char *type,
                        pci_driver_probe_fn probe,
                        const pci_driver_match_t *match);
void pci_probe_drivers(void);

/* Generic ioctl commands */
#define DRV_IOCTL_GET_STATUS   1
#define DRV_IOCTL_RESET        2
#define DRV_IOCTL_GET_INFO     3

/* Network driver ioctl commands */
#define NET_IOCTL_GET_MAC      0x0101
#define NET_IOCTL_SET_MAC      0x0102
#define NET_IOCTL_GET_LINK     0x0103
#define NET_IOCTL_GET_STATS    0x0104
#define NET_IOCTL_SEND_FRAME   0x0105
#define NET_IOCTL_RECV_FRAME   0x0106

/* Storage driver ioctl commands */
#define STOR_IOCTL_GET_INFO    0x0201
#define STOR_IOCTL_READ        0x0202
#define STOR_IOCTL_WRITE       0x0203
#define STOR_IOCTL_FLUSH       0x0204

struct drv_storage_info {
    uint32_t sectors;
    uint32_t sector_size;
};

/* Audio driver ioctl commands */
#define AUDIO_IOCTL_GET_INFO   0x0301
#define AUDIO_IOCTL_PLAYBACK   0x0302
#define AUDIO_IOCTL_START      0x0303
#define AUDIO_IOCTL_STOP       0x0304

/* Video driver ioctl commands */
#define VID_IOCTL_GET_MODE     0x0401
#define VID_IOCTL_GET_BACKEND  0x0402
#define VID_IOCTL_GET_FRAMEBUFFER 0x0403

/* Video ioctl data structures */
struct vid_mode_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t fb_addr;
};

struct vid_backend_info {
    char name[32];
    uint32_t active;
    uint32_t present;
    char drm_backend[32];
    uint32_t drm_active;
};

struct vid_framebuffer_info {
    uint32_t fb_addr;
    uint32_t fb_size;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

/* Device status structure returned by DRV_IOCTL_GET_STATUS */
struct drv_device_status {
    uint32_t present;
    uint32_t active;
    uint32_t error;
    uint32_t link_up;
    uint32_t mtu;
    uint8_t mac[6];
};

#endif /* KERNEL_DRIVER_MANAGER_H */
