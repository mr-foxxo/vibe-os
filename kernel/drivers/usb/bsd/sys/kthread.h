#ifndef _SYS_KTHREAD_H_
#define _SYS_KTHREAD_H_

struct proc;

int kthread_create(void (*func)(void *), void *arg, struct proc **procp,
    const char *name);
void kthread_create_deferred(void (*func)(void *), void *arg);
void kthread_exit(void *retval) __attribute__((noreturn));

#endif
