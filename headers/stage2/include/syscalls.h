#ifndef STAGE2_SYSCALLS_H_
#define STAGE2_SYSCALLS_H_

#include <stage2/include/types.h>

/* Dispatch syscall */
void syscall_dispatch(struct syscall_regs *regs);

#endif
