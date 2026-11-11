#include <aio.h>
#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <compat/libc/string.h>
#include <stdlib.h>
#include <include/userland_api.h>

#define MAX_AIO_OPS 16

struct aio_op {
    struct aiocb *cb;
    int used;
    int completed;
};

static struct aio_op g_aio_ops[MAX_AIO_OPS];
static int g_aio_init = 0;

static void aio_yield(void) {
    __asm__ volatile("int $0x80" : : "a"(10) : "memory");
}

static void aio_ensure_init(void) {
    if (!g_aio_init) {
        memset(g_aio_ops, 0, sizeof(g_aio_ops));
        g_aio_init = 1;
    }
}

static struct aio_op *aio_find_slot(struct aiocb *cb) {
    unsigned int i;
    aio_ensure_init();
    for (i = 0; i < MAX_AIO_OPS; i++) {
        if (!g_aio_ops[i].used) {
            g_aio_ops[i].used = 1;
            g_aio_ops[i].cb = cb;
            g_aio_ops[i].completed = 0;
            return &g_aio_ops[i];
        }
    }
    return 0;
}

static struct aio_op *aio_find_by_cb(const struct aiocb *cb) {
    unsigned int i;
    for (i = 0; i < MAX_AIO_OPS; i++) {
        if (g_aio_ops[i].used && g_aio_ops[i].cb == cb) {
            return &g_aio_ops[i];
        }
    }
    return 0;
}

int aio_read(struct aiocb *aiocbp) {
    struct aio_op *op;
    ssize_t nread;

    if (!aiocbp || !aiocbp->aio_buf) {
        errno = EINVAL;
        return -1;
    }

    op = aio_find_slot(aiocbp);
    if (!op) {
        aiocbp->aio_error = EAGAIN;
        return -1;
    }

    nread = (ssize_t)lseek(aiocbp->aio_fildes, aiocbp->aio_offset, 0);
    if (nread < 0) nread = 0;

    nread = read(aiocbp->aio_fildes, aiocbp->aio_buf, aiocbp->aio_nbytes);
    if (nread < 0) {
        aiocbp->aio_error = errno;
        aiocbp->aio_return = -1;
    } else {
        aiocbp->aio_error = 0;
        aiocbp->aio_return = nread;
    }
    op->completed = 1;
    return 0;
}

int aio_write(struct aiocb *aiocbp) {
    struct aio_op *op;
    ssize_t nwritten;

    if (!aiocbp || !aiocbp->aio_buf) {
        errno = EINVAL;
        return -1;
    }

    op = aio_find_slot(aiocbp);
    if (!op) {
        aiocbp->aio_error = EAGAIN;
        return -1;
    }

    nwritten = (ssize_t)lseek(aiocbp->aio_fildes, aiocbp->aio_offset, 0);
    if (nwritten < 0) nwritten = 0;

    nwritten = write(aiocbp->aio_fildes, aiocbp->aio_buf, aiocbp->aio_nbytes);
    if (nwritten < 0) {
        aiocbp->aio_error = errno;
        aiocbp->aio_return = -1;
    } else {
        aiocbp->aio_error = 0;
        aiocbp->aio_return = nwritten;
    }
    op->completed = 1;
    return 0;
}

int aio_error(const struct aiocb *aiocbp) {
    if (!aiocbp) return -1;
    return aiocbp->aio_error;
}

ssize_t aio_return(struct aiocb *aiocbp) {
    if (!aiocbp) return -1;
    return aiocbp->aio_return;
}

int aio_suspend(const struct aiocb *const list[], int nent, const struct timespec *timeout) {
    int i;
    (void)timeout;
    if (!list) return -1;
    for (i = 0; i < nent; i++) {
        if (list[i]) {
            struct aio_op *op = aio_find_by_cb(list[i]);
            if (op) {
                while (!op->completed) {
                    aio_yield();
                }
            }
        }
    }
    return 0;
}

int aio_cancel(int fd, struct aiocb *aiocbp) {
    (void)fd;
    (void)aiocbp;
    return AIO_CANCELED;
}

int lio_listio(int mode, struct aiocb *const list[], int nent, struct sigevent *sig) {
    int i;
    (void)sig;
    if (!list) return -1;
    for (i = 0; i < nent; i++) {
        if (!list[i]) continue;
        if (list[i]->aio_lio_opcode == LIO_READ) {
            aio_read(list[i]);
        } else if (list[i]->aio_lio_opcode == LIO_WRITE) {
            aio_write(list[i]);
        }
    }
    if (mode == LIO_WAIT) {
        for (i = 0; i < nent; i++) {
            if (list[i]) {
                while (list[i]->aio_error == EINPROGRESS) {
                    aio_yield();
                }
            }
        }
    }
    return 0;
}
