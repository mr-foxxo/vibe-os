#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/rwlock.h>
#include <sys/kthread.h>
#include <sys/atomic.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <machine/bus.h>
#include <dev/pci/pcivar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

uint32_t kernel_timer_get_ticks(void);
void *alloc_phys_pages(size_t count);
void free_phys_pages(void *page, size_t count);
void *kernel_malloc(size_t size);
void kernel_free(void *ptr);
int kernel_irq_register_handler(unsigned char irq_line, void (*handler)(void));
void kernel_irq_unmask(unsigned char irq_line);
uint32_t kernel_pci_config_read_u32(unsigned char bus, unsigned char slot, unsigned char function, unsigned char offset);
void kernel_pci_config_write_u32(unsigned char bus, unsigned char slot, unsigned char function, unsigned char offset, uint32_t value);

int cold = 1;
struct proc *curproc = NULL;

struct cfdriver *usb_cd = NULL;

void *config_found(struct device *parent, void *aux, void *print) {
    (void)parent; (void)aux; (void)print;
    return NULL;
}

void config_defer(struct device *dev, void (*func)(struct device *)) {
    (void)dev;
    if (func) func(dev);
}

void config_pending_incr(void) {}
void config_pending_decr(void) {}

int config_activate_children(struct device *dev, int act) {
    (void)dev; (void)act;
    return 0;
}
int config_detach_children(struct device *dev, int flags) {
    (void)dev; (void)flags;
    return 0;
}

int tsleep(const volatile void *ident, int priority, const char *wmesg, int timo) {
    (void)ident; (void)priority; (void)wmesg; (void)timo;
    return 0;
}

int tsleep_nsec(const volatile void *ident, int priority, const char *wmesg, long nsec) {
    (void)ident; (void)priority; (void)wmesg; (void)nsec;
    return 0;
}

void wakeup(const volatile void *ident) { (void)ident; }
void wakeup_one(const volatile void *ident) { (void)ident; }

int kthread_create(void (*func)(void *), void *arg,
    struct proc **procp, const char *name) {
    (void)func; (void)arg; (void)procp; (void)name;
    return 0;
}

void kthread_create_deferred(void (*func)(void *), void *arg) {
    (void)func; (void)arg;
}

void kthread_exit(void *retval) {
    (void)retval;
    for(;;) __asm__ volatile("cli; hlt");
}

struct rwlock usbpalock;
void rw_init(struct rwlock *rw, const char *name) {
    (void)name; rw->dummy = 0;
}
void rw_enter_read(struct rwlock *rw) { (void)rw; }
void rw_enter_write(struct rwlock *rw) { (void)rw; }
void rw_exit(struct rwlock *rw) { (void)rw; }
void rw_assert_anylock(struct rwlock *rw) { (void)rw; }
void rw_assert_wrlock(struct rwlock *rw) { (void)rw; }

void pool_init(struct pool *pp, size_t sz, unsigned int align, int ipl,
    int flags, const char *name, void *p) {
    (void)align; (void)ipl; (void)flags; (void)p;
    pp->pr_name = name;
    pp->pr_size = sz;
    pp->pr_align = align;
    pp->pr_nitems = 0;
}

void *pool_get(struct pool *pp, int flags) {
    (void)flags;
    void *v = kernel_malloc(pp->pr_size);
    if (v) pp->pr_nitems++;
    return v;
}

void pool_put(struct pool *pp, void *v) {
    if (v) { kernel_free(v); pp->pr_nitems--; }
}

void timeout_set(struct timeout *t, void (*func)(void *), void *arg) {
    t->func = func;
    t->arg = arg;
    t->flags = 0;
    t->pending = 0;
}

void timeout_add(struct timeout *t, int ticks) { (void)t; (void)ticks; }
void timeout_add_msec(struct timeout *t, int msec) { (void)t; (void)msec; }
void timeout_add_sec(struct timeout *t, int sec) { (void)t; (void)sec; }
void timeout_add_usec(struct timeout *t, int usec) { (void)t; (void)usec; }
int timeout_del(struct timeout *t) { (void)t; return 0; }
void timeout_del_barrier(struct timeout *t) { (void)t; }

void getmicrouptime(struct timeval *tv) {
    uint32_t ticks = kernel_timer_get_ticks();
    tv->tv_sec = ticks / hz;
    tv->tv_usec = (ticks % hz) * (1000000 / hz);
}

usbd_status usb_insert_transfer(struct usbd_xfer *xfer) {
    (void)xfer;
    return USBD_NORMAL_COMPLETION;
}

void usb_transfer_complete(struct usbd_xfer *xfer) {
    (void)xfer;
}

void usb_schedsoftintr(struct usbd_bus *bus) {
    (void)bus;
}

void usbd_delay_ms(struct usbd_device *dev, u_int ms) {
    (void)dev;
    DELAY(ms * 1000);
}

void usb_delay_ms(struct usbd_bus *bus, u_int ms) {
    if (bus->use_polling || cold)
        DELAY((ms + 1) * 1000);
}

int usbd_set_address(struct usbd_device *dev, int addr) {
    (void)dev; (void)addr;
    return 0;
}

int usbd_reset_port(struct usbd_device *dev, int port) {
    (void)dev; (void)port;
    return 0;
}

int usbd_getnewaddr(struct usbd_bus *bus) {
    (void)bus;
    static int next_addr = 1;
    return next_addr++;
}

int bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t align,
    bus_dma_flag_t flags, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    bus_dma_flag_t rflags) {
    (void)t; (void)align; (void)flags; (void)nsegs; (void)rflags;
    size_t npages = (size + 0xFFF) >> 12;
    void *p = alloc_phys_pages(npages);
    if (!p) return -1;
    segs[0].ds_addr = (bus_addr_t)(uintptr_t)p;
    segs[0].ds_len = size;
    *rsegs = 1;
    return 0;
}

void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs) {
    (void)t; (void)nsegs;
    free_phys_pages((void*)(uintptr_t)segs[0].ds_addr,
        (segs[0].ds_len + 0xFFF) >> 12);
}

int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    bus_size_t size, caddr_t *kaddr, bus_dma_flag_t flags) {
    (void)t; (void)nsegs; (void)flags;
    *kaddr = (caddr_t)(uintptr_t)segs[0].ds_addr;
    (void)size;
    return 0;
}

void bus_dmamem_unmap(bus_dma_tag_t t, caddr_t kaddr, bus_size_t size) {
    (void)t; (void)kaddr; (void)size;
}

int bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, bus_dma_flag_t flags,
    bus_dmamap_t *mapp) {
    (void)t; (void)size; (void)nsegments; (void)maxsegsz;
    (void)boundary; (void)flags;
    static struct bus_dmamap dummy;
    dummy.addr = 0;
    *mapp = &dummy;
    return 0;
}

void bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map) {
    (void)t; (void)map;
}

int bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, caddr_t buf,
    bus_size_t buflen, struct proc *p, bus_dma_flag_t flags) {
    (void)t; (void)p; (void)flags;
    map->addr = (bus_addr_t)(uintptr_t)buf;
    (void)buflen;
    return 0;
}

void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map) {
    (void)t; (void)map;
}

void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t len, int ops) {
    (void)t; (void)map; (void)offset; (void)len; (void)ops;
}

int pci_mapreg_map(struct pci_attach_args *pa, int reg, uint32_t type,
    int flags, bus_space_tag_t *tagp, bus_space_handle_t *hp,
    bus_addr_t *mapp, bus_size_t *sizep, int *rofsp) {
    (void)type; (void)flags;
    int bar_idx = 0;
    if (reg == 0x10) bar_idx = 0;
    else if (reg == 0x14) bar_idx = 1;
    else if (reg == 0x18) bar_idx = 2;
    else if (reg == 0x1C) bar_idx = 3;
    else if (reg == 0x20) bar_idx = 4;
    else if (reg == 0x24) bar_idx = 5;

    *tagp = 0;
    uint32_t bar = pa->pa_bars[bar_idx];
    uintptr_t addr;
    if (bar & 1) addr = (uintptr_t)(bar & 0xFFFFFFFC);
    else addr = (uintptr_t)(bar & 0xFFFFFFF0);
    *hp = (bus_space_handle_t)addr;
    if (sizep) *sizep = 256;
    if (mapp) *mapp = (bus_addr_t)addr;
    if (rofsp) *rofsp = 0;
    return 0;
}

int pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ih) {
    *ih = (pci_intr_handle_t)pa->pa_irq;
    return (pa->pa_irq == 0 || pa->pa_irq == 0xFF) ? 1 : 0;
}

const char *pci_intr_string(pci_chipset_tag_t pc, pci_intr_handle_t ih) {
    (void)pc; (void)ih;
    return "irq";
}

void *pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih,
    int level, int (*handler)(void *), void *arg, const char *name) {
    (void)pc; (void)level; (void)name; (void)arg;
    kernel_irq_register_handler((uint8_t)ih, (void (*)(void))handler);
    kernel_irq_unmask((uint8_t)ih);
    return (void*)(uintptr_t)1;
}

void pci_intr_disestablish(pci_chipset_tag_t pc, void *ih) {
    (void)pc; (void)ih;
}

uint32_t pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg) {
    (void)pc;
    uint8_t bus = (uint8_t)((tag >> 8) & 0xFF);
    uint8_t slot = (uint8_t)((tag >> 3) & 0x1F);
    uint8_t func = (uint8_t)(tag & 7);
    return kernel_pci_config_read_u32(bus, slot, func, (uint8_t)reg);
}

void pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, uint32_t val) {
    (void)pc;
    uint8_t bus = (uint8_t)((tag >> 8) & 0xFF);
    uint8_t slot = (uint8_t)((tag >> 3) & 0x1F);
    uint8_t func = (uint8_t)(tag & 7);
    kernel_pci_config_write_u32(bus, slot, func, (uint8_t)reg, val);
}

const char *pci_findvendor(uint32_t id) {
    (void)id;
    return NULL;
}

extern int uhci_intr(void *);
extern int uhci_detach(struct device *, int);
extern int uhci_activate(struct device *, int);

/* Stubs for USB functions used by uhci.c */
#include <dev/usb/usbdi.h>
#include <dev/usb/usb.h>

void usb_add_task(struct usbd_device *dev, struct usb_task *task) {
    (void)dev; (void)task;
}

void usb_rem_task(struct usbd_device *dev, struct usb_task *task) {
    (void)dev; (void)task;
}

int usbd_str(usb_string_descriptor_t *p, int l, const char *s) {
    (void)p; (void)l; (void)s;
    return 0;
}

