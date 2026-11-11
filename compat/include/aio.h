#ifndef _AIO_H_
#define _AIO_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <time.h>

union sigval {
    int sival_int;
    void *sival_ptr;
};

struct sigevent {
    int sigev_notify;
    int sigev_signo;
    union sigval sigev_value;
    void (*sigev_notify_function)(union sigval);
    void *sigev_notify_attributes;
};

struct aiocb {
    int aio_fildes;
    off_t aio_offset;
    void *aio_buf;
    size_t aio_nbytes;
    int aio_reqprio;
    struct sigevent aio_sigevent;
    int aio_lio_opcode;
    int aio_error;
    ssize_t aio_return;
};

#define AIO_CANCELED    0
#define AIO_NOTCANCELED 1
#define AIO_ALLDONE     2
#define AIO_LISTIO_INVALID -1

#define LIO_WAIT    0
#define LIO_NOWAIT  1
#define LIO_NOP     0
#define LIO_READ    1
#define LIO_WRITE   2

__BEGIN_DECLS
int aio_read(struct aiocb *aiocbp);
int aio_write(struct aiocb *aiocbp);
int aio_error(const struct aiocb *aiocbp);
ssize_t aio_return(struct aiocb *aiocbp);
int aio_suspend(const struct aiocb *const list[], int nent, const struct timespec *timeout);
int aio_cancel(int fd, struct aiocb *aiocbp);
int lio_listio(int mode, struct aiocb *const list[], int nent, struct sigevent *sig);
__END_DECLS

#endif
