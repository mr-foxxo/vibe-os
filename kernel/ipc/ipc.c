#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>

#include <kernel/ipc.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/kernel.h>
#include <kernel/memory/heap.h>

#define IPC_QUEUE_SIZE 16
#define IPC_MAX_QUEUES 64

/* messages are simple pointers to a buffer with length */
typedef struct {
    void *data;
    size_t len;
} ipc_msg_t;

/* one queue per process for simplicity */
struct ipc_queue {
    ipc_msg_t msgs[IPC_QUEUE_SIZE];
    int head;
    int tail;
    int count;
};

static struct ipc_queue *g_queues[IPC_MAX_QUEUES];

static struct ipc_queue *ensure_queue(process_t *p) {
    if (!p)
        return NULL;
    int idx = p->pid % IPC_MAX_QUEUES;
    if (!g_queues[idx]) {
        g_queues[idx] = kernel_malloc(sizeof(struct ipc_queue));
        if (!g_queues[idx])
            return NULL;
        memset(g_queues[idx], 0, sizeof(struct ipc_queue));
    }
    return g_queues[idx];
}

int ipc_send(process_t *dest, const void *data, size_t len) {
    if (!dest || !data)
        return -1;
    struct ipc_queue *q = ensure_queue(dest);
    if (!q || q->count >= IPC_QUEUE_SIZE)
        return -1;
    ipc_msg_t *m = &q->msgs[q->tail];
    m->data = kernel_malloc(len);
    if (!m->data)
        return -1;
    memcpy(m->data, data, len);
    m->len = len;
    q->tail = (q->tail + 1) % IPC_QUEUE_SIZE;
    q->count++;
    return 0;
}

int ipc_receive(process_t *self, void *buf, size_t bufsize) {
    if (!self || !buf)
        return -1;
    struct ipc_queue *q = ensure_queue(self);
    if (!q || q->count == 0)
        return -1;
    ipc_msg_t *m = &q->msgs[q->head];
    size_t tocopy = m->len < bufsize ? m->len : bufsize;
    memcpy(buf, m->data, tocopy);
    kernel_free(m->data);
    q->head = (q->head + 1) % IPC_QUEUE_SIZE;
    q->count--;
    return (int)tocopy;
}
