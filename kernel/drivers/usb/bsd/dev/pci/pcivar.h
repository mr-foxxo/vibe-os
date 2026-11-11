#ifndef _DEV_PCI_PCIVAR_H_
#define _DEV_PCI_PCIVAR_H_

#include <stdint.h>

typedef uint32_t pci_chipset_tag_t;
typedef uint32_t pcitag_t;
typedef uint32_t pci_intr_handle_t;
typedef uint32_t pcireg_t;

struct pci_attach_args {
    pci_chipset_tag_t pa_pc;
    pcitag_t pa_tag;
    uint32_t pa_id;
    uint32_t pa_class;
    bus_dma_tag_t pa_dmat;
    int pa_bus;
    int pa_device;
    int pa_function;
    uint8_t pa_irq;
    uint32_t pa_bars[6];
};

#define PCI_VENDOR(id)       ((id) & 0xFFFF)
#define PCI_PRODUCT(id)      (((id) >> 16) & 0xFFFF)
#define PCI_REVISION(cls)    ((cls) & 0xFF)

#define PCI_CLASS(cls)       (((cls) >> 24) & 0xFF)
#define PCI_SUBCLASS(cls)    (((cls) >> 16) & 0xFF)
#define PCI_INTERFACE(cls)   (((cls) >> 8)  & 0xFF)

#define PCI_CLASS_SERIALBUS       0x0C
#define PCI_SUBCLASS_SERIALBUS_USB 0x03
#define PCI_INTERFACE_UHCI        0x00
#define PCI_INTERFACE_EHCI        0x20
#define PCI_INTERFACE_OHCI        0x10
#define PCI_INTERFACE_XHCI        0x30

#define PCI_CBIO  0x20
#define PCI_CBMEM 0x10

#define PCI_MAPREG_TYPE_IO  0x01
#define PCI_MAPREG_TYPE_MEM 0x00
#define PCI_MAPREG_TYPE_MEM64 0x02

int pci_mapreg_map(struct pci_attach_args *pa, int reg, uint32_t type,
    int flags, bus_space_tag_t *tagp, bus_space_handle_t *hp,
    bus_addr_t *mapp, bus_size_t *sizep, int *rofsp);

int pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ih);
const char *pci_intr_string(pci_chipset_tag_t pc, pci_intr_handle_t ih);
void *pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih,
    int level, int (*handler)(void *), void *arg, const char *name);
void pci_intr_disestablish(pci_chipset_tag_t pc, void *ih);

uint32_t pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg);
void pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, uint32_t val);

const char *pci_findvendor(uint32_t id);

#endif
