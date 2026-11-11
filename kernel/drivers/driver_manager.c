#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>

#include <kernel/driver_manager.h>
#include <kernel/kernel.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/pci_bus.h>

#define MAX_DRIVERS 32

struct driver_entry {
    const char *name;
    const char *type;
    void (*init)(void);
};

static struct driver_entry g_drivers[MAX_DRIVERS];
static int g_driver_count = 0;

struct pci_driver_entry {
    const char *name;
    const char *type;
    pci_driver_probe_fn probe;
    pci_driver_match_t match;
    int registered;
};

static struct pci_driver_entry g_pci_drivers[PCI_DRIVER_MAX];
static int g_pci_driver_count = 0;

int register_driver(const char *name, const char *type, void (*init)(void)) {
    if (g_driver_count >= MAX_DRIVERS || !name || !type || !init) {
        return -1;
    }
    g_drivers[g_driver_count].name = name;
    g_drivers[g_driver_count].type = type;
    g_drivers[g_driver_count].init = init;
    g_driver_count++;
    return 0;
}

int register_pci_driver(const char *name, const char *type,
                        pci_driver_probe_fn probe,
                        const pci_driver_match_t *match) {
    if (g_pci_driver_count >= PCI_DRIVER_MAX || !name || !type || !probe || !match) {
        return -1;
    }
    g_pci_drivers[g_pci_driver_count].name = name;
    g_pci_drivers[g_pci_driver_count].type = type;
    g_pci_drivers[g_pci_driver_count].probe = probe;
    g_pci_drivers[g_pci_driver_count].match = *match;
    g_pci_drivers[g_pci_driver_count].registered = 0;
    g_pci_driver_count++;
    return 0;
}

void pci_probe_drivers(void) {
    int total = pci_device_count();

    if (total == 0) {
        return;
    }

    kernel_debug_puts("pci: probing drivers...\n");

    for (int di = 0; di < g_pci_driver_count; ++di) {
        struct pci_driver_entry *drv = &g_pci_drivers[di];

        if (drv->registered) {
            continue;
        }

        for (int pi = 0; pi < total; ++pi) {
            pci_device_t *dev = pci_device_get(pi);
            int match = 0;

            if (drv->match.use_class) {
                if (dev->class_code == (uint8_t)(drv->match.class_code & 0xFFu) &&
                    dev->subclass == (uint8_t)(drv->match.subclass & 0xFFu)) {
                    if (drv->match.prog_if == 0xFFu ||
                        dev->prog_if == (uint8_t)(drv->match.prog_if & 0xFFu)) {
                        match = 1;
                    }
                }
            } else {
                for (int id = 0; id < drv->match.count && id < PCI_DRIVER_MAX_ID; ++id) {
                    if (dev->vendor_id == drv->match.vendors[id] &&
                        dev->device_id == drv->match.devices[id]) {
                        match = 1;
                        break;
                    }
                }
            }

            if (match) {
                kernel_debug_printf("pci: %s matches %s %04x:%04x at %02x:%02x.%x\n",
                                    drv->name, drv->type,
                                    (unsigned int)dev->vendor_id,
                                    (unsigned int)dev->device_id,
                                    (unsigned int)dev->bus,
                                    (unsigned int)dev->slot,
                                    (unsigned int)dev->function);
                if (drv->probe(dev) == 0) {
                    drv->registered = 1;
                    kernel_debug_printf("pci: %s initialized\n", drv->name);
                }
                break;
            }
        }
    }
}

void driver_manager_init(void) {
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].init) {
            kernel_debug_printf("drv: init %s (%s)\n", g_drivers[i].name, g_drivers[i].type);
            g_drivers[i].init();
            kernel_debug_printf("drv: %s done\n", g_drivers[i].name);
        }
    }
    kernel_debug_puts("drv: probing pci drivers...\n");
    pci_probe_drivers();
    kernel_debug_puts("drv: all done\n");
}
