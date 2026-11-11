/*
 * Network Queue Implementation for Phase F
 * Service-owned TX/RX queues and socket readiness
 */

#include <kernel/memory/heap.h>
#include <kernel/microkernel/network_queue.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/hal/io.h>
#include <stddef.h>
#include <string.h>

struct network_queue_socket {
    int32_t handle;
    uint32_t owner_pid;
    uint32_t readiness_flags;
    uint32_t rx_queue_depth;
    uint32_t tx_queue_depth;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;
    uint32_t tx_head;
    uint32_t tx_tail;
    uint32_t tx_count;
    uint32_t rx_sequence;
    uint32_t tx_sequence;
    uint32_t rx_lengths[NETWORK_QUEUE_MAX_PACKETS];
    uint32_t tx_lengths[NETWORK_QUEUE_MAX_PACKETS];
    uint8_t rx_packets[NETWORK_QUEUE_MAX_PACKETS][NETWORK_QUEUE_PACKET_MAX];
    uint8_t tx_packets[NETWORK_QUEUE_MAX_PACKETS][NETWORK_QUEUE_PACKET_MAX];
};

static struct network_queue_socket g_socket_queue[NETWORK_QUEUE_MAX_SOCKETS];
static spinlock_t g_network_queue_lock;
static network_queue_ready_event_cb_t g_ready_event_callback;

static inline uint32_t network_queue_next_idx(uint32_t idx) {
    return (idx + 1) % NETWORK_QUEUE_MAX_PACKETS;
}

static void network_queue_raise_ready_callback(struct network_queue_socket *socket) {
    if (g_ready_event_callback && socket) {
        /* All fields captured under the caller-held lock; the callback must
         * never re-enter a locking network_queue API (see header comment). */
        g_ready_event_callback(socket->handle,
                               socket->readiness_flags,
                               socket->rx_queue_depth,
                               socket->tx_queue_depth);
    }
}

void network_queue_set_ready_event_callback(network_queue_ready_event_cb_t callback) {
    uint32_t flags = spinlock_lock_irqsave(&g_network_queue_lock);
    g_ready_event_callback = callback;
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
}

void network_queue_init(void) {
    memset(g_socket_queue, 0, sizeof(g_socket_queue));
    spinlock_init(&g_network_queue_lock);
    g_ready_event_callback = NULL;
}

int network_queue_register_socket(int32_t handle, uint32_t owner_pid) {
    uint32_t flags;
    int result = -1;

    if (handle <= 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);

    for (uint32_t i = 0; i < NETWORK_QUEUE_MAX_SOCKETS; i++) {
        if (g_socket_queue[i].handle == 0) {
            struct network_queue_socket *socket = &g_socket_queue[i];
            memset(socket, 0, sizeof(*socket));
            socket->handle = handle;
            socket->owner_pid = owner_pid;
            socket->readiness_flags = NETWORK_SOCKET_READY_SEND;
            socket->tx_queue_depth = 0;
            socket->rx_queue_depth = 0;
            network_queue_raise_ready_callback(socket);
            result = 0;
            break;
        }
    }

    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return result;
}

int network_queue_unregister_socket(int32_t handle) {
    uint32_t flags;
    int result = -1;

    if (handle <= 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);

    for (uint32_t i = 0; i < NETWORK_QUEUE_MAX_SOCKETS; i++) {
        if (g_socket_queue[i].handle == handle) {
            memset(&g_socket_queue[i], 0, sizeof(g_socket_queue[i]));
            result = 0;
            break;
        }
    }

    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return result;
}

void network_queue_update_telemetry(struct mk_network_status *status) {
    uint32_t flags;

    if (status == NULL) {
        return;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);

    for (uint32_t i = 0; i < NETWORK_QUEUE_MAX_SOCKETS; i++) {
        if (g_socket_queue[i].handle != 0) {
            if (g_socket_queue[i].rx_queue_depth > 0) {
                status->recv_ready_count++;
                status->pending_rx_bytes += g_socket_queue[i].rx_queue_depth * NETWORK_QUEUE_PACKET_MAX;
            }
        }
    }

    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
}

static struct network_queue_socket *network_queue_find_socket(int32_t handle) {
    for (uint32_t i = 0; i < NETWORK_QUEUE_MAX_SOCKETS; i++) {
        if (g_socket_queue[i].handle == handle) {
            return &g_socket_queue[i];
        }
    }
    return NULL;
}

uint32_t network_queue_get_socket_readiness(int32_t handle) {
    uint32_t flags;
    uint32_t readiness = 0;

    if (handle <= 0) {
        return 0;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);
    {
        struct network_queue_socket *socket = network_queue_find_socket(handle);
        if (socket) {
            readiness = socket->readiness_flags;
        }
    }
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return readiness;
}

uint32_t network_queue_get_rx_depth(int32_t handle) {
    uint32_t flags;
    uint32_t depth = 0;

    if (handle <= 0) {
        return 0;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);
    {
        struct network_queue_socket *socket = network_queue_find_socket(handle);
        if (socket) {
            depth = socket->rx_queue_depth;
        }
    }
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return depth;
}

uint32_t network_queue_get_tx_depth(int32_t handle) {
    uint32_t flags;
    uint32_t depth = 0;

    if (handle <= 0) {
        return 0;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);
    {
        struct network_queue_socket *socket = network_queue_find_socket(handle);
        if (socket) {
            depth = socket->tx_queue_depth;
        }
    }
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return depth;
}

int network_queue_rx_enqueue(const uint8_t *data, uint32_t length, int32_t socket_handle) {
    uint32_t flags;
    int result = -1;

    if (data == NULL || length == 0 || socket_handle <= 0) {
        return -1;
    }

    if (length > NETWORK_QUEUE_PACKET_MAX) {
        length = NETWORK_QUEUE_PACKET_MAX;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);
    {
        struct network_queue_socket *socket = network_queue_find_socket(socket_handle);
        if (socket && socket->rx_count < NETWORK_QUEUE_MAX_PACKETS) {
            uint32_t idx = socket->rx_tail;
            memcpy(socket->rx_packets[idx], data, length);
            socket->rx_lengths[idx] = length;
            socket->rx_tail = network_queue_next_idx(socket->rx_tail);
            socket->rx_count++;
            socket->rx_queue_depth = socket->rx_count;
            socket->readiness_flags |= NETWORK_SOCKET_READY_RECV;
            network_queue_raise_ready_callback(socket);
            result = 0;
        }
    }
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return result;
}

int network_queue_tx_enqueue(const uint8_t *data, uint32_t length, int32_t socket_handle) {
    uint32_t flags;
    int result = -1;

    if (data == NULL || length == 0 || socket_handle <= 0) {
        return -1;
    }

    if (length > NETWORK_QUEUE_PACKET_MAX) {
        length = NETWORK_QUEUE_PACKET_MAX;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);
    {
        struct network_queue_socket *socket = network_queue_find_socket(socket_handle);
        if (socket && socket->tx_count < NETWORK_QUEUE_MAX_PACKETS) {
            uint32_t idx = socket->tx_tail;
            memcpy(socket->tx_packets[idx], data, length);
            socket->tx_lengths[idx] = length;
            socket->tx_tail = network_queue_next_idx(socket->tx_tail);
            socket->tx_count++;
            socket->tx_queue_depth = socket->tx_count;
            socket->readiness_flags |= NETWORK_SOCKET_READY_SEND;
            network_queue_raise_ready_callback(socket);
            result = 0;
        }
    }
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return result;
}

int network_queue_rx_dequeue(uint8_t *buffer, uint32_t buffer_size, int32_t socket_handle,
                           uint32_t *out_length, uint32_t *out_sequence) {
    uint32_t flags;
    int result = -1;

    if (buffer == NULL || buffer_size == 0 || socket_handle <= 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);
    {
        struct network_queue_socket *socket = network_queue_find_socket(socket_handle);
        if (socket && socket->rx_count > 0) {
            uint32_t idx = socket->rx_head;
            uint32_t packet_len = socket->rx_lengths[idx];
            uint32_t copy_len = packet_len;
            if (copy_len > buffer_size) {
                copy_len = buffer_size;
            }

            memcpy(buffer, socket->rx_packets[idx], copy_len);
            if (out_length) {
                *out_length = packet_len;
            }
            if (out_sequence) {
                *out_sequence = socket->rx_sequence++;
            }

            socket->rx_head = network_queue_next_idx(socket->rx_head);
            socket->rx_count--;
            socket->rx_queue_depth = socket->rx_count;
            if (socket->rx_count == 0) {
                socket->readiness_flags &= ~NETWORK_SOCKET_READY_RECV;
            }
            network_queue_raise_ready_callback(socket);
            result = (int)copy_len;
        }
    }
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return result;
}

int network_queue_tx_dequeue(uint8_t *buffer, uint32_t buffer_size, int32_t socket_handle,
                           uint32_t *out_length, uint32_t *out_sequence) {
    uint32_t flags;
    int result = -1;

    if (buffer == NULL || buffer_size == 0 || socket_handle <= 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_network_queue_lock);
    {
        struct network_queue_socket *socket = network_queue_find_socket(socket_handle);
        if (socket && socket->tx_count > 0) {
            uint32_t idx = socket->tx_head;
            uint32_t packet_len = socket->tx_lengths[idx];
            uint32_t copy_len = packet_len;
            if (copy_len > buffer_size) {
                copy_len = buffer_size;
            }

            memcpy(buffer, socket->tx_packets[idx], copy_len);
            if (out_length) {
                *out_length = packet_len;
            }
            if (out_sequence) {
                *out_sequence = socket->tx_sequence++;
            }

            socket->tx_head = network_queue_next_idx(socket->tx_head);
            socket->tx_count--;
            socket->tx_queue_depth = socket->tx_count;
            if (socket->tx_count == 0) {
                socket->readiness_flags &= ~NETWORK_SOCKET_READY_SEND;
            }
            network_queue_raise_ready_callback(socket);
            result = (int)copy_len;
        }
    }
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
    return result;
}

void network_queue_publish_socket_events(void) {
    uint32_t flags;

    flags = spinlock_lock_irqsave(&g_network_queue_lock);
    {
        for (uint32_t i = 0; i < NETWORK_QUEUE_MAX_SOCKETS; ++i) {
            struct network_queue_socket *socket = &g_socket_queue[i];
            if (socket->handle != 0 && socket->readiness_flags != 0) {
                network_queue_raise_ready_callback(socket);
            }
        }
    }
    spinlock_unlock_irqrestore(&g_network_queue_lock, flags);
}
