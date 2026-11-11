#ifndef _SYS_DEVICE_H_
#define _SYS_DEVICE_H_

#include <stddef.h>
#include <stdint.h>

struct device {
    const char *dv_xname;
    int dv_unit;
    int dv_flags;
    void *dv_parent;
};

struct cfdriver {
    void **cd_devs;
    const char *cd_name;
    int cd_ndevs;
    int cd_flags;
};

struct cfattach {
    size_t ca_devsize;
    int (*ca_match)(struct device *, void *, void *);
    void (*ca_attach)(struct device *, struct device *, void *);
    int (*ca_detach)(struct device *, int);
    int (*ca_activate)(struct device *, int);
};

#define DV_DULL 0
#define CD_SKIPHIBERNATE 1

#define DVACT_QUIESCE 0
#define DVACT_RESUME 1
#define DVACT_WAKEUP 2
#define DVACT_POWERDOWN 3
#define DVACT_SUSPEND 4

#define UNCONF 0

void *config_found(struct device *parent, void *aux, void *print);
void config_defer(struct device *dev, void (*func)(struct device *));
void config_pending_incr(void);
void config_pending_decr(void);

extern struct cfdriver *usb_cd;
extern struct cfdriver uhci_cd;
extern struct cfdriver ehci_cd;

int config_activate_children(struct device *dev, int act);
int config_detach_children(struct device *dev, int flags);

#define config_found_sm(self, aux, print, subdev) config_found((self), (aux), (print))

#endif
