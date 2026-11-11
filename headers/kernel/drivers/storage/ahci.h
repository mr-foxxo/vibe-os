#ifndef KERNEL_DRIVERS_STORAGE_AHCI_H
#define KERNEL_DRIVERS_STORAGE_AHCI_H

#include <stdint.h>
#include <stddef.h>

int kernel_ahci_init(void);
int kernel_ahci_present(void);
int kernel_ahci_ioctl(int cmd, void *arg, size_t arg_size);

#endif
