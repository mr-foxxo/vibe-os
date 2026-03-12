#ifndef KERNEL_USERLAND_H
#define KERNEL_USERLAND_H

#include <stdint.h>

/* Load embedded userland blob into memory and jump to its entry */
__attribute__((noreturn)) void userland_run(void);

#endif /* KERNEL_USERLAND_H */
