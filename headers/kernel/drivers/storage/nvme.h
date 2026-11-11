#ifndef KERNEL_DRIVERS_STORAGE_NVME_H
#define KERNEL_DRIVERS_STORAGE_NVME_H

#include <stdint.h>
#include <stddef.h>

int kernel_nvme_init(void);
int kernel_nvme_present(void);
int kernel_nvme_ioctl(int cmd, void *arg, size_t arg_size);

#endif
