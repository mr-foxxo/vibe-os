#ifndef KERNEL_DRIVERS_NETWORK_E1000_H
#define KERNEL_DRIVERS_NETWORK_E1000_H

#include <stdint.h>

void kernel_e1000_init(void);
int kernel_e1000_present(void);
int kernel_e1000_send_packet(const uint8_t *data, uint32_t len);
int kernel_e1000_recv_packet(uint8_t *buf, uint32_t max_len, uint32_t *len_out);
int kernel_e1000_ioctl(int cmd, void *arg, size_t arg_size);

#endif
