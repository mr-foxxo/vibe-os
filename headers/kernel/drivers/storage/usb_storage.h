#ifndef KERNEL_DRIVERS_STORAGE_USB_STORAGE_H
#define KERNEL_DRIVERS_STORAGE_USB_STORAGE_H

#include <stdint.h>
#include <stddef.h>

void kernel_usb_storage_init(void);
int kernel_usb_storage_present(void);
int kernel_usb_storage_ioctl(int cmd, void *arg, size_t arg_size);

#endif
