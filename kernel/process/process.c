#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/kernel.h>    /* for panic if allocation fails */
#include <kernel/memory/heap.h>   /* kernel_malloc, kernel_free */

/* arbitrary stack size for each process */
#define PROCESS_STACK_SIZE 4096

static int g_next_pid = 1;

process_t *process_create(void (*entry)(void)) {
    if (entry == NULL) {
        return NULL;
    }

    process_t *p = kernel_malloc(sizeof(process_t));
    if (!p) {
        return NULL;
    }
    memset(p, 0, sizeof(*p));

    p->pid = g_next_pid++;
    p->state = PROCESS_READY;
    p->next = NULL;

    /* allocate stack memory and set initial register state */
    p->stack = kernel_malloc(PROCESS_STACK_SIZE);
    if (!p->stack) {
        kernel_free(p);
        return NULL;
    }

    uintptr_t stack_top = (uintptr_t)p->stack + PROCESS_STACK_SIZE;
    p->regs.eip = (uint32_t)entry;
    p->regs.esp = (uint32_t)stack_top;
    p->regs.ebp = (uint32_t)stack_top;
    p->regs.eax = p->regs.ebx = p->regs.ecx = p->regs.edx = 0;
    p->regs.esi = p->regs.edi = 0;

    return p;
}

void process_destroy(process_t *proc) {
    if (!proc) {
        return;
    }
    if (proc->stack) {
        kernel_free(proc->stack);
    }
    kernel_free(proc);
}
