#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>  /* memcpy/memset helpers */

#include <kernel/scheduler.h>
#include <kernel/process.h>
#include <kernel/kernel.h>    /* for panic, if needed */

/* prototype for the low‑level context switch routine (implemented in assembly) */
extern void context_switch(void *old_task, void *new_task);

/* simple singly‑linked list of processes */
static process_t *g_head = NULL;
static process_t *g_current = NULL;

void scheduler_init(void) {
    g_head = NULL;
    g_current = NULL;
}

void scheduler_add_task(process_t *proc) {
    if (proc == NULL) {
        return;
    }
    proc->next = NULL;
    proc->state = PROCESS_READY;
    if (g_head == NULL) {
        g_head = proc;
    } else {
        process_t *p = g_head;
        while (p->next) {
            p = p->next;
        }
        p->next = proc;
    }
}

static process_t *find_next(void) {
    if (g_head == NULL) {
        return NULL;
    }
    if (g_current == NULL) {
        return g_head;
    }
    process_t *p = g_current->next ? g_current->next : g_head;
    while (p && p->state != PROCESS_READY) {
        p = p->next ? p->next : g_head;
        if (p == g_current) {
            /* we looped around */
            break;
        }
    }
    if (p->state == PROCESS_READY) {
        return p;
    }
    return NULL;
}

void schedule(void) {
    process_t *next = find_next();
    if (next == NULL) {
        return; /* nothing to do */
    }

    if (g_current == NULL) {
        /* first switch into user/task context */
        g_current = next;
        context_switch(NULL, g_current);
    } else if (next != g_current) {
        process_t *old = g_current;
        g_current = next;
        context_switch(old, next);
    }
}

void yield(void) {
    schedule();
}

process_t *scheduler_current(void) {
    return g_current;
}
