#ifndef _SYS_TIMEOUT_H_
#define _SYS_TIMEOUT_H_

#include <stdint.h>
#include <stddef.h>

struct timeout {
    void (*func)(void *);
    void *arg;
    int flags;
    volatile int pending;
};

#define TIMEOUT_PROC 0
#define TIMEOUT_INITIALIZER_FLAGS(f, a, fl) { .func = (f), .arg = (a), .flags = (fl) }

void timeout_set(struct timeout *t, void (*func)(void *), void *arg);
void timeout_add(struct timeout *t, int ticks);
void timeout_add_msec(struct timeout *t, int msec);
void timeout_add_sec(struct timeout *t, int sec);
void timeout_add_usec(struct timeout *t, int usec);
int timeout_del(struct timeout *t);
void timeout_del_barrier(struct timeout *t);

#endif
