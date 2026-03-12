#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>
#include <stddef.h>

/* process states for scheduler bookkeeping */
enum process_state {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED,
};

/* saved register set for context switching.  order is important; the
   assembly context_switch routine assumes this layout. */
typedef struct {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebp;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
} regs_t;

/* process descriptor.  regs_t is placed first so that a pointer to the
   structure is also a pointer to the saved register block. */
typedef struct process {
    regs_t regs;            /* processor state (must be first field) */
    int pid;                /* process identifier */
    void *stack;            /* base pointer of allocated stack memory */
    enum process_state state;
    struct process *next;   /* linked‑list pointer for scheduler */
} process_t;

/* create / destroy a process object.  the entry point will be installed
   in the register state and the stack allocated automatically. */
process_t *process_create(void (*entry)(void));
void process_destroy(process_t *proc);

#endif /* KERNEL_PROCESS_H */
