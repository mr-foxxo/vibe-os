#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <kernel/process.h>
#include <stdint.h>

/* scheduler: cooperative round‑robin over a linked list of processes */

/* initialize scheduler data structures - call once during boot */
void scheduler_init(void);

/* add a newly created process to the run queue */
void scheduler_add_task(process_t *proc);

/* perform a context switch to the next ready task */
void schedule(void);

/* voluntarily relinquish the CPU (syscall/yield) */
void yield(void);

/* return currently executing process (may be NULL) */
process_t *scheduler_current(void);

#endif /* KERNEL_SCHEDULER_H */
