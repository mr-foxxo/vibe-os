#ifndef KERNEL_DRIVERS_NETWORK_WIFI_PCI_H
#define KERNEL_DRIVERS_NETWORK_WIFI_PCI_H

#include <stdint.h>

void kernel_wifi_pci_init(void);
int kernel_wifi_pci_present(void);
int kernel_wifi_pci_is_wifi_device(uint16_t vendor_id, uint16_t device_id);
uint16_t kernel_wifi_pci_vendor_id(void);
uint16_t kernel_wifi_pci_device_id(void);
const char *kernel_wifi_pci_chip_name(void);
void kernel_wifi_pci_get_chip_name(uint16_t vendor_id, uint16_t device_id,
                                   char *buffer, uint32_t buffer_size);
int kernel_wifi_pci_ioctl(int cmd, void *arg, size_t arg_size);

#endif