#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>

#include <kernel/ipc.h>
#include <kernel/event.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/kernel.h>
#include <kernel/interrupt.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>

#define IPC_QUEUE_SIZE 64
#define IPC_MAX_QUEUES 64

/* messages are simple pointers to a buffer with length */
typedef struct {
    void *data;
    size_t len;
} ipc_msg_t;

/* one queue per process for simplicity */
struct ipc_queue {
    int owner_pid;
    kernel_waitable_t waitable;
    spinlock_t lock;
    ipc_msg_t msgs[IPC_QUEUE_SIZE];
    int head;
    int tail;
    int count;
};

static struct ipc_queue *g_queues[IPC_MAX_QUEUES];
static spinlock_t g_queue_table_lock;

static struct ipc_queue *ensure_queue(process_t *p) {
    int slot;
    int empty_slot = -1;
    struct ipc_queue *queue;
    uint32_t flags;

    if (!p || p->pid <= 0) {
        return NULL;
    }

    flags = spinlock_lock_irqsave(&g_queue_table_lock);
    for (slot = 0; slot < IPC_MAX_QUEUES; ++slot) {
        if (g_queues[slot] != NULL && g_queues[slot]->owner_pid == p->pid) {
            queue = g_queues[slot];
            spinlock_unlock_irqrestore(&g_queue_table_lock, flags);
            return queue;
        }
        if (g_queues[slot] == NULL && empty_slot < 0) {
            empty_slot = slot;
        }
    }
    if (empty_slot < 0) {
        spinlock_unlock_irqrestore(&g_queue_table_lock, flags);
        return NULL;
    }

    queue = kernel_malloc(sizeof(struct ipc_queue));
    if (!queue) {
        spinlock_unlock_irqrestore(&g_queue_table_lock, flags);
        return NULL;
    }

    memset(queue, 0, sizeof(struct ipc_queue));
    queue->owner_pid = p->pid;
    kernel_waitable_init_ex(&queue->waitable,
                            TASK_WAIT_EVENT_QUEUE,
                            TASK_WAIT_CLASS_IPC,
                            p->service_type);
    spinlock_init(&queue->lock);
    g_queues[empty_slot] = queue;
    spinlock_unlock_irqrestore(&g_queue_table_lock, flags);
    return queue;
}

int ipc_send(process_t *dest, const void *data, size_t len) {
    struct ipc_queue *q;
    ipc_msg_t *m;
    uint32_t flags;

    if (!dest || !data || len == 0u) {
        return -1;
    }

    q = ensure_queue(dest);
    if (!q) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&q->lock);
    if (q->count >= IPC_QUEUE_SIZE) {
        spinlock_unlock_irqrestore(&q->lock, flags);
        return -1;
    }

    m = &q->msgs[q->tail];
    m->data = kernel_malloc(len);
    if (!m->data) {
        spinlock_unlock_irqrestore(&q->lock, flags);
        return -1;
    }

    memcpy(m->data, data, len);
    m->len = len;
    q->tail = (q->tail + 1) % IPC_QUEUE_SIZE;
    q->count++;
    spinlock_unlock_irqrestore(&q->lock, flags);
    kernel_waitable_signal(&q->waitable, 1u);
    return 0;
}

/*
 * P4 wake-loss instrumentation (read-only; never blocks beyond the queue's
 * own spinlock): queue depth + waitable counters. The service timeout trace
 * uses this to localize the storage stall by measurement instead of theory:
 * host queue depth>0 while host state RUNNING = host loop not draining;
 * client reply depth==0 = reply never produced; pending_signals>0 with
 * waiters==0 = signal landed before the wait; wake/timeout counters show
 * which side of the handshake is actually firing.
 */
void ipc_queue_stats(process_t *p, int *depth_out, uint32_t *pending_out,
                     uint32_t *wake_out, uint32_t *timeout_out,
                     int *waiters_out) {
    struct ipc_queue *q;
    uint32_t flags;

    if (depth_out) {
        *depth_out = -1;
    }
    if (pending_out) {
        *pending_out = 0u;
    }
    if (wake_out) {
        *wake_out = 0u;
    }
    if (timeout_out) {
        *timeout_out = 0u;
    }
    if (waiters_out) {
        *waiters_out = -1;
    }
    if (p == NULL) {
        return;
    }
    q = ensure_queue(p);
    if (q == NULL) {
        return;
    }
    flags = spinlock_lock_irqsave(&q->lock);
    if (depth_out) {
        *depth_out = q->count;
    }
    spinlock_unlock_irqrestore(&q->lock, flags);

    flags = spinlock_lock_irqsave(&q->waitable.lock);
    if (pending_out) {
        *pending_out = q->waitable.pending_signals;
    }
    if (wake_out) {
        *wake_out = q->waitable.wake_count;
    }
    if (timeout_out) {
        *timeout_out = q->waitable.timeout_count;
    }
    if (waiters_out) {
        int n = 0;
        process_t *w = (process_t *)q->waitable.wait_head;

        while (w != NULL && n < 64) {
            n++;
            w = (process_t *)w->wait_next;
        }
        *waiters_out = n;
    }
    spinlock_unlock_irqrestore(&q->waitable.lock, flags);
}

int ipc_receive(process_t *self, void *buf, size_t bufsize) {
    struct ipc_queue *q;
    ipc_msg_t *m;
    size_t tocopy;
    uint32_t flags;

    if (!self || !buf || bufsize == 0u) {
        return -1;
    }

    q = ensure_queue(self);
    if (!q) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&q->lock);
    if (q->count == 0) {
        spinlock_unlock_irqrestore(&q->lock, flags);
        return -1;
    }

    m = &q->msgs[q->head];
    tocopy = m->len < bufsize ? m->len : bufsize;
    memcpy(buf, m->data, tocopy);
    kernel_free(m->data);
    m->data = NULL;
    m->len = 0u;
    q->head = (q->head + 1) % IPC_QUEUE_SIZE;
    q->count--;
    spinlock_unlock_irqrestore(&q->lock, flags);
    return (int)tocopy;
}

int ipc_receive_wait_timeout(process_t *self,
                             void *buf,
                             size_t bufsize,
                             uint32_t timeout_ticks) {
    int received;

    if (!self || !buf || bufsize == 0u) {
        return -1;
    }

    for (;;) {
        struct ipc_queue *q = ensure_queue(self);

        if (!q) {
            return -1;
        }

        received = ipc_receive(self, buf, bufsize);
        if (received >= 0) {
            return received;
        }
        if (timeout_ticks == 0u) {
            if (kernel_waitable_wait(&q->waitable) < 0) {
                return -1;
            }
            continue;
        }
        if (kernel_waitable_wait_timeout(&q->waitable, timeout_ticks) !=
            TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}

int ipc_receive_wait(process_t *self, void *buf, size_t bufsize) {
    return ipc_receive_wait_timeout(self, buf, bufsize, 0u);
}
