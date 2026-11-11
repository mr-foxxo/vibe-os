#include <mqueue.h>
#include <compat/posix/errno.h>
#include <compat/libc/string.h>
#include <compat/posix/stat.h>
#include <compat/posix/fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <include/userland_api.h>

#define MAX_MQ 8
#define MQ_NAME_MAX 32
#define MQ_MAX_MSG 16
#define MQ_MSG_SIZE 4096

#define SYSCALL_WAITABLE_INIT 188
#define SYSCALL_WAITABLE_WAIT 189
#define SYSCALL_WAITABLE_WAKE 200

struct mq_msg {
    char data[MQ_MSG_SIZE];
    size_t len;
    unsigned int prio;
};

struct mq_internal {
    char name[MQ_NAME_MAX];
    mqd_t id;
    int used;
    int refcount;
    struct mq_attr attr;
    struct mq_msg msgs[MQ_MAX_MSG];
    int head;
    int count;
    unsigned long wait_send[10];
    unsigned long wait_recv[10];
    struct sigevent notify;
    int notify_registered;
};

static struct mq_internal g_mqueues[MAX_MQ];
static int g_mq_next_id = 1;

static long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "memory");
    return ret;
}

static struct mq_internal *mq_find_by_name(const char *name) {
    int i;
    for (i = 0; i < MAX_MQ; i++) {
        if (g_mqueues[i].used && strcmp(g_mqueues[i].name, name) == 0) {
            return &g_mqueues[i];
        }
    }
    return 0;
}

static struct mq_internal *mq_find_by_id(mqd_t id) {
    int i;
    for (i = 0; i < MAX_MQ; i++) {
        if (g_mqueues[i].used && g_mqueues[i].id == id) {
            return &g_mqueues[i];
        }
    }
    return 0;
}

static struct mq_internal *mq_alloc(void) {
    int i;
    for (i = 0; i < MAX_MQ; i++) {
        if (!g_mqueues[i].used) {
            memset(&g_mqueues[i], 0, sizeof(g_mqueues[i]));
            g_mqueues[i].used = 1;
            g_mqueues[i].id = g_mq_next_id++;
            g_mqueues[i].refcount = 1;
            g_mqueues[i].attr.mq_flags = 0;
            g_mqueues[i].attr.mq_maxmsg = MQ_MAX_MSG;
            g_mqueues[i].attr.mq_msgsize = MQ_MSG_SIZE;
            g_mqueues[i].attr.mq_curmsgs = 0;
            syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)g_mqueues[i].wait_send, 0, 0, 0, 0, 0);
            syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)g_mqueues[i].wait_recv, 0, 0, 0, 0, 0);
            return &g_mqueues[i];
        }
    }
    return 0;
}

mqd_t mq_open(const char *name, int oflag, ...) {
    struct mq_internal *mq;
    mode_t mode = 0;
    struct mq_attr *attr = 0;
    va_list ap;

    if (!name || name[0] != '/') {
        errno = EINVAL;
        return (mqd_t)-1;
    }

    mq = mq_find_by_name(name);
    if (!mq && (oflag & O_CREAT) != 0) {
        va_start(ap, oflag);
        mode = va_arg(ap, mode_t);
        attr = va_arg(ap, struct mq_attr *);
        va_end(ap);
        (void)mode;
        mq = mq_alloc();
        if (!mq) {
            errno = ENOSPC;
            return (mqd_t)-1;
        }
        strncpy(mq->name, name, MQ_NAME_MAX - 1);
        mq->name[MQ_NAME_MAX - 1] = '\0';
        if (attr) {
            mq->attr.mq_maxmsg = attr->mq_maxmsg > 0 && attr->mq_maxmsg <= MQ_MAX_MSG ? attr->mq_maxmsg : MQ_MAX_MSG;
            mq->attr.mq_msgsize = attr->mq_msgsize > 0 && attr->mq_msgsize <= MQ_MSG_SIZE ? attr->mq_msgsize : MQ_MSG_SIZE;
        }
    }
    if (!mq) {
        errno = ENOENT;
        return (mqd_t)-1;
    }
    mq->refcount++;
    return mq->id;
}

int mq_close(mqd_t mqdes) {
    struct mq_internal *mq = mq_find_by_id(mqdes);
    if (!mq) {
        errno = EBADF;
        return -1;
    }
    mq->refcount--;
    if (mq->refcount <= 0) {
        memset(mq, 0, sizeof(*mq));
    }
    return 0;
}

int mq_unlink(const char *name) {
    struct mq_internal *mq = mq_find_by_name(name);
    if (!mq) {
        errno = ENOENT;
        return -1;
    }
    memset(mq, 0, sizeof(*mq));
    return 0;
}

int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio) {
    struct mq_internal *mq = mq_find_by_id(mqdes);
    int idx;

    if (!mq) {
        errno = EBADF;
        return -1;
    }
    if (!msg_ptr || msg_len > (size_t)mq->attr.mq_msgsize) {
        errno = EMSGSIZE;
        return -1;
    }

    while (mq->count >= mq->attr.mq_maxmsg) {
        if ((mq->attr.mq_flags & O_NONBLOCK) != 0) {
            errno = EAGAIN;
            return -1;
        }
        syscall6(SYSCALL_WAITABLE_WAIT, (long)(uintptr_t)mq->wait_send, 0, 0, 0, 0, 0);
    }

    idx = (mq->head + mq->count) % mq->attr.mq_maxmsg;
    memcpy(mq->msgs[idx].data, msg_ptr, msg_len);
    mq->msgs[idx].len = msg_len;
    mq->msgs[idx].prio = msg_prio;
    mq->count++;

    syscall6(SYSCALL_WAITABLE_WAKE, (long)(uintptr_t)mq->wait_recv, 1, 0, 0, 0, 0);
    return 0;
}

ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio) {
    struct mq_internal *mq = mq_find_by_id(mqdes);
    int best_idx;
    int i;

    if (!mq) {
        errno = EBADF;
        return -1;
    }
    if (!msg_ptr || msg_len < (size_t)mq->attr.mq_msgsize) {
        errno = EMSGSIZE;
        return -1;
    }

    while (mq->count <= 0) {
        if ((mq->attr.mq_flags & O_NONBLOCK) != 0) {
            errno = EAGAIN;
            return -1;
        }
        syscall6(SYSCALL_WAITABLE_WAIT, (long)(uintptr_t)mq->wait_recv, 0, 0, 0, 0, 0);
    }

    best_idx = mq->head;
    for (i = 0; i < mq->count; i++) {
        int idx = (mq->head + i) % mq->attr.mq_maxmsg;
        if (mq->msgs[idx].prio > mq->msgs[best_idx].prio) {
            best_idx = idx;
        }
    }

    memcpy(msg_ptr, mq->msgs[best_idx].data, mq->msgs[best_idx].len);
    if (msg_prio) *msg_prio = mq->msgs[best_idx].prio;

    for (i = best_idx; i != (mq->head + mq->count - 1) % mq->attr.mq_maxmsg; ) {
        int next = (i + 1) % mq->attr.mq_maxmsg;
        mq->msgs[i] = mq->msgs[next];
        i = next;
    }
    mq->count--;

    syscall6(SYSCALL_WAITABLE_WAKE, (long)(uintptr_t)mq->wait_send, 1, 0, 0, 0, 0);
    return (ssize_t)mq->msgs[best_idx].len;
}

int mq_getattr(mqd_t mqdes, struct mq_attr *attr) {
    struct mq_internal *mq = mq_find_by_id(mqdes);
    if (!mq || !attr) {
        errno = EBADF;
        return -1;
    }
    *attr = mq->attr;
    attr->mq_curmsgs = (long)mq->count;
    return 0;
}

int mq_setattr(mqd_t mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr) {
    struct mq_internal *mq = mq_find_by_id(mqdes);
    if (!mq) {
        errno = EBADF;
        return -1;
    }
    if (oldattr) {
        *oldattr = mq->attr;
        oldattr->mq_curmsgs = (long)mq->count;
    }
    if (newattr) {
        mq->attr.mq_flags = newattr->mq_flags;
    }
    return 0;
}

int mq_notify(mqd_t mqdes, const struct sigevent *notification) {
    struct mq_internal *mq = mq_find_by_id(mqdes);
    if (!mq) {
        errno = EBADF;
        return -1;
    }
    if (notification) {
        if (mq->notify_registered) {
            errno = EBUSY;
            return -1;
        }
        mq->notify = *notification;
        mq->notify_registered = 1;
    } else {
        mq->notify_registered = 0;
    }
    return 0;
}
