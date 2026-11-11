#ifndef KERNEL_DRIVERS_NETWORK_VIRTIO_NET_H
#define KERNEL_DRIVERS_NETWORK_VIRTIO_NET_H

#include <stdint.h>

void kernel_virtio_net_init(void);
int kernel_virtio_net_present(void);
uint32_t kernel_virtio_net_mtu(void);
int kernel_virtio_net_send(const uint8_t *frame, uint32_t frame_len);
int kernel_virtio_net_recv(uint8_t *frame, uint32_t *frame_len);
int kernel_virtio_net_link_up(void);
const uint8_t *kernel_virtio_net_mac(void);
uint32_t kernel_virtio_net_tx_completed(void);
uint32_t kernel_virtio_net_rx_completed(void);
int kernel_virtio_net_ioctl(int cmd, void *arg, size_t arg_size);

#endif
