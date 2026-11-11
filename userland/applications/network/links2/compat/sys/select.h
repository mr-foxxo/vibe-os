#ifndef SYS_SELECT_H
#define SYS_SELECT_H
#include <sys/types.h>
#include <sys/time.h>
typedef struct { unsigned long fds_bits[16]; } fd_set;
extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif
