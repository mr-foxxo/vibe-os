#ifndef KERNEL_PCI_BUS_H
#define KERNEL_PCI_BUS_H

#include <stddef.h>
#include <stdint.h>

#define PCI_MAX_DEVICES 256

#define PCI_BAR_TYPE_IO    0
#define PCI_BAR_TYPE_MEM32 1
#define PCI_BAR_TYPE_MEM64 2

#define PCI_CLASS_NETWORK 0x02u
#define PCI_CLASS_MULTIMEDIA 0x04u
#define PCI_SUBCLASS_SATA 0x06u
#define PCI_PROGIF_AHCI 0x01u

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t revision;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t header_type;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;

    uint32_t bar_raw[6];
    uint8_t bar_type[6];
    uintptr_t bar_addr[6];
    size_t bar_size[6];
} pci_device_t;

typedef struct {
    uint16_t vendor;
    uint16_t device;
} pci_id_t;

int pci_bus_init(void);
int pci_device_count(void);
pci_device_t *pci_device_get(int index);
void pci_log_devices(void);

int pci_match(pci_device_t *dev, uint16_t vendor, uint16_t device);
int pci_match_class(pci_device_t *dev, uint8_t class, uint8_t subclass);
int pci_match_table(pci_device_t *dev, const pci_id_t *table);

pci_device_t *pci_find_device(uint16_t vendor, uint16_t device);
pci_device_t *pci_find_by_class(uint8_t class, uint8_t subclass);
pci_device_t *pci_find_by_class_prog_if(uint8_t class, uint8_t subclass, uint8_t prog_if);
int pci_find_all_by_class(uint8_t class, uint8_t subclass, pci_device_t **results, int max);

int pci_bar_type(pci_device_t *dev, int bar);
uintptr_t pci_bar_addr(pci_device_t *dev, int bar);
size_t pci_bar_size(pci_device_t *dev, int bar);
int pci_bar_map(pci_device_t *dev, int bar);

uint8_t pci_irq_line(pci_device_t *dev);
uint8_t pci_irq_pin(pci_device_t *dev);
void pci_set_irq_line(pci_device_t *dev, uint8_t irq);

#endif
