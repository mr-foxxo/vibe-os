#ifndef KERNEL_DRIVER_MANAGER_H
#define KERNEL_DRIVER_MANAGER_H

/* simple driver registration API */

int register_driver(const char *name, const char *type, void (*init)(void));
void driver_manager_init(void);

#endif /* KERNEL_DRIVER_MANAGER_H */