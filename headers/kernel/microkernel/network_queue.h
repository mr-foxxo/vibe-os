#ifndef KERNEL_MICROKERNEL_NETWORK_QUEUE_H
#define KERNEL_MICROKERNEL_NETWORK_QUEUE_H

#include <stdint.h>

struct mk_network_status;

enum {
    NETWORK_QUEUE_MAX_SOCKETS = 32,
    NETWORK_QUEUE_MAX_PACKETS = 8,
    NETWORK_QUEUE_PACKET_MAX = 1024,
};

// Socket readiness flags
#define NETWORK_SOCKET_READY_RECV   0x01
#define NETWORK_SOCKET_READY_SEND   0x02
#define NETWORK_SOCKET_READY_ACCEPT 0x04
#define NETWORK_SOCKET_READY_CONNECT 0x08

/*
 * The ready callback fires while the queue lock is STILL held by the caller,
 * so it must not call back into any network_queue API that re-acquires it —
 * get_rx_depth/get_tx_depth self-deadlocked on the non-recursive spinlock
 * (found 14/10: any socket() path hung the network service). All data the
 * listener needs is therefore captured from the socket under the lock and
 * passed by value. Internal kernel-kernel interface; not a userland ABI.
 */
typedef void (*network_queue_ready_event_cb_t)(int32_t socket_handle,
                                               uint32_t readiness_flags,
                                               uint32_t rx_depth,
                                               uint32_t tx_depth);

void network_queue_init(void);
int network_queue_rx_enqueue(const uint8_t *data, uint32_t length, int32_t socket_handle);
int network_queue_tx_enqueue(const uint8_t *data, uint32_t length, int32_t socket_handle);
int network_queue_rx_dequeue(uint8_t *buffer, uint32_t buffer_size, int32_t socket_handle,
                           uint32_t *out_length, uint32_t *out_sequence);
int network_queue_tx_dequeue(uint8_t *buffer, uint32_t buffer_size, int32_t socket_handle,
                           uint32_t *out_length, uint32_t *out_sequence);
int network_queue_register_socket(int32_t handle, uint32_t owner_pid);
int network_queue_unregister_socket(int32_t handle);
uint32_t network_queue_get_socket_readiness(int32_t handle);
uint32_t network_queue_get_rx_depth(int32_t handle);
uint32_t network_queue_get_tx_depth(int32_t handle);
void network_queue_publish_socket_events(void);
void network_queue_set_ready_event_callback(network_queue_ready_event_cb_t callback);
void network_queue_update_telemetry(struct mk_network_status *status);

#endif