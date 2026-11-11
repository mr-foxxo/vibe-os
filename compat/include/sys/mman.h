#ifndef _COMPAT_SYS_MMAN_H
#define _COMPAT_SYS_MMAN_H

#include <sys/types.h>

#define PROT_NONE   0x00
#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define PROT_EXEC   0x04

#define MAP_SHARED  0x0001
#define MAP_PRIVATE 0x0002
#define MAP_FIXED   0x0010
#define MAP_ANON    0x1000
#define MAP_ANONYMOUS MAP_ANON
#define MAP_POPULATE 0x0020
#define MAP_FAILED  ((void *)-1)

#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4

#define MCL_CURRENT 1
#define MCL_FUTURE  2

#define MS_ASYNC        1
#define MS_SYNC         2
#define MS_INVALIDATE   4

#define POSIX_MADV_NORMAL     MADV_NORMAL
#define POSIX_MADV_RANDOM     MADV_RANDOM
#define POSIX_MADV_SEQUENTIAL MADV_SEQUENTIAL
#define POSIX_MADV_WILLNEED   MADV_WILLNEED
#define POSIX_MADV_DONTNEED   MADV_DONTNEED

#include <stddef.h>
#include <sys/cdefs.h>

__BEGIN_DECLS
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t len, int prot);
int mlock(const void *addr, size_t len);
int munlock(const void *addr, size_t len);
int mlockall(int flags);
int munlockall(void);
int madvise(void *addr, size_t length, int advice);
int shm_open(const char *name, int oflag, mode_t mode);
int shm_unlink(const char *name);
__END_DECLS

#endif
