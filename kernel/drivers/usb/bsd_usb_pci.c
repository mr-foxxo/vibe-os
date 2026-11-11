#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/queue.h>
#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>
#include <dev/usb/uhcireg.h>
#include <dev/usb/uhcivar.h>

#include <kernel/pci_bus.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/interrupt.h>
#include <kernel/memory/heap.h>

#define MAX_BSD_USB_CONTROLLERS 8

struct bsd_usb_controller {
    int type;
    pci_device_t *pdev;
    void *softc;
    int active;
};

static struct bsd_usb_controller g_bsd_controllers[MAX_BSD_USB_CONTROLLERS];
static int g_bsd_controller_count = 0;
static int g_bsd_initialized = 0;

extern int uhci_intr(void *);
extern usbd_status uhci_init(struct uhci_softc *);
extern usbd_status uhci_run(struct uhci_softc *, int);

extern struct pool *uhcixfer;

static int bsd_usb_attach_uhci(pci_device_t *pdev) {
    struct uhci_softc *sc;
    bus_space_handle_t ioh;
    int bar_idx = -1;
    uint8_t irq = pdev->irq_line;

    sc = kernel_malloc(sizeof(struct uhci_softc));
    if (!sc) {
        kernel_debug_printf("bsd_usb: OOM for uhci_softc\n");
        return -1;
    }

    for (int b = 0; b < 6; b++) {
        if (pdev->bar_type[b] == PCI_BAR_TYPE_IO && pdev->bar_size[b] > 0) {
            bar_idx = b;
            break;
        }
    }
    if (bar_idx < 0) {
        kernel_debug_printf("bsd_usb: UHCI controller has no IO BAR\n");
        kernel_free(sc);
        return -1;
    }

    ioh = (bus_space_handle_t)(uintptr_t)pdev->bar_addr[bar_idx];
    sc->iot = 0;
    sc->ioh = ioh;
    sc->sc_size = (bus_size_t)pdev->bar_size[bar_idx];
    sc->sc_bus.dmatag = NULL;
    sc->sc_bus.use_polling = 1;
    sc->sc_bus.dying = 0;
    sc->sc_bus.pipe_size = sizeof(struct usbd_pipe);
    sc->sc_bus.usbrev = USBREV_1_1;
    sc->sc_bus.methods = NULL;
    sc->sc_bus.intr_context = 0;
    sc->sc_bus.no_intrs = 0;
    sc->sc_id_vendor = pdev->vendor_id;

    sc->sc_bus.bdev.dv_xname = "uhci";

    struct bsd_usb_controller *ctrl = &g_bsd_controllers[g_bsd_controller_count];
    ctrl->type = 1;
    ctrl->pdev = pdev;
    ctrl->softc = sc;
    ctrl->active = 0;  /* deferred - not yet initialized */
    g_bsd_controller_count++;

    kernel_debug_printf("bsd_usb: UHCI at %02x:%02x.%x IO=%x IRQ=%d (deferred)\n",
        pdev->bus, pdev->slot, pdev->function,
        (unsigned int)pdev->bar_addr[bar_idx], irq);
    return 0;
}

int bsd_usb_init_controller(struct bsd_usb_controller *ctrl) {
    if (!ctrl || !ctrl->softc || ctrl->active) return -1;
    struct uhci_softc *sc = (struct uhci_softc *)ctrl->softc;

    int s = splhardusb();
    bus_space_write_2(sc->iot, sc->ioh, UHCI_INTR, 0);
    uhci_run(sc, 0);
    bus_space_write_2(sc->iot, sc->ioh, UHCI_INTR, 0);
    usbd_status r = uhci_init(sc);
    splx(s);

    if (r == USBD_NORMAL_COMPLETION) {
        ctrl->active = 1;
        kernel_debug_printf("bsd_usb: UHCI hw init OK\n");
        return 0;
    }
    kernel_debug_printf("bsd_usb: UHCI hw init failed: %d\n", r);
    return -1;
}

void bsd_usb_init(void) {
    if (g_bsd_initialized) return;
    g_bsd_initialized = 1;

    if (!kernel_heap_is_initialized()) {
        kernel_debug_printf("bsd_usb: heap not ready, deferring\n");
        g_bsd_initialized = 0;
        return;
    }

    kernel_debug_printf("bsd_usb: probing for UHCI controllers...\n");

    int total = pci_device_count();
    if (total <= 0) {
        pci_bus_init();
        total = pci_device_count();
    }

    for (int i = 0; i < total && g_bsd_controller_count < MAX_BSD_USB_CONTROLLERS; i++) {
        pci_device_t *pdev = pci_device_get(i);
        if (!pdev) continue;
        if (pdev->class_code == 0x0C && pdev->subclass == 0x03) {
            if (pdev->prog_if == 0x00) {
                bsd_usb_attach_uhci(pdev);
            }
        }
    }

    if (g_bsd_controller_count == 0) {
        kernel_debug_printf("bsd_usb: no UHCI controllers found\n");
    } else {
        kernel_debug_printf("bsd_usb: %d UHCI controller(s) initialized\n",
            g_bsd_controller_count);
    }
}
