#ifndef KERNEL_DRIVERS_NETWORK_RTL8139_H
#define KERNEL_DRIVERS_NETWORK_RTL8139_H

#include <stdint.h>

void kernel_rtl8139_init(void);
void kernel_rtl8139_irq_handler(void);
int kernel_rtl8139_present(void);
int kernel_rtl8139_send_packet(const uint8_t *data, uint32_t len);
int kernel_rtl8139_recv_packet(uint8_t *buf, uint32_t max_len, uint32_t *len_out);
int kernel_rtl8139_ioctl(int cmd, void *arg, size_t arg_size);

#endif
