#ifndef KERNEL_MTRR_H
#define KERNEL_MTRR_H

#include <stdint.h>
#include <stddef.h>

void mtrr_init(void);
int mtrr_set_wc(uintptr_t base, size_t size);
int mtrr_available(void);
int mtrr_wc_supported(void);

#endif
