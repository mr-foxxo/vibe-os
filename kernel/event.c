#include <kernel/event.h>

#include <kernel/kernel_string.h>
#include <kernel/interrupt.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/memory/heap.h>
#include <kernel/drivers/debug/debug.h>

/*
 * Waitable-chain hardening (COMPLETION_ACTION_PLAN P4, class #5). The
 * scheduler task list was hardened this way (node-sane + step cap +
 * budgeted diag) and that eliminated its corruption class; the waitable
 * wait_next chain is walked/written in four places with NO cap and NO
 * node validation. A single stale/garbage wait_next (from a task ending
 * up on two chains, or a recycled block) makes enqueue write
 * `tail->wait_next = task` into a FOREIGN heap block — the observed
 * class #5 signature of heap pointers landing in task pid/kind/state
 * fields. These guards are additive: a well-formed chain behaves exactly
 * as before (is_block true, count small); only a corrupted chain trips
 * them, where they stop the runaway write and name the victim instead of
 * silently corrupting memory.
 */
#define KERNEL_WAITABLE_WALK_CAP 512u

static volatile uint32_t g_waitable_chain_budget = 8u;

static int kernel_waitable_task_node_ok(const process_t *task) {
    return task != 0 && task != (const process_t *)~0u &&
           kernel_heap_is_block((const void *)task);
}

static void kernel_waitable_chain_diag(const char *what,
                                       const kernel_waitable_t *waitable,
                                       const process_t *node) {
    if (g_waitable_chain_budget == 0u) {
        return;
    }
    g_waitable_chain_budget--;
    kernel_debug_printf("waitable: CHAIN-BAD %s waitable=0x%x head=0x%x node=0x%x nodepid=%d\n",
                        what, (unsigned int)(uintptr_t)waitable,
                        waitable ? (unsigned int)(uintptr_t)waitable->wait_head : 0u,
                        (unsigned int)(uintptr_t)node,
                        (node != 0 && kernel_waitable_task_node_ok(node)) ? node->pid : -9999);
}

static void kernel_waitable_enqueue(kernel_waitable_t *waitable, process_t *task) {
    process_t *tail;
    uint32_t steps = 0u;

    if (waitable == 0 || task == 0) {
        return;
    }
    if (!kernel_waitable_task_node_ok(task)) {
        kernel_waitable_chain_diag("enqueue-bad-task", waitable, task);
        return;
    }
    if (task->wait_next != 0) {
        /* Already linked into a chain (possibly another waitable's).
         * Relinking would silently splice it out of the old chain and hand
         * it to this one — cross-chain corruption. The wait_channel guard in
         * the caller should make this unreachable; if it fires, it names a
         * real bug instead of corrupting memory. */
        kernel_waitable_chain_diag("enqueue-already-linked", waitable, task);
        return;
    }

    task->wait_next = 0;
    if (waitable->wait_head == 0) {
        waitable->wait_head = task;
        return;
    }

    tail = waitable->wait_head;
    while (tail->wait_next != 0) {
        if (++steps > KERNEL_WAITABLE_WALK_CAP ||
            !kernel_waitable_task_node_ok(tail->wait_next)) {
            /* Do NOT write through a garbage tail->wait_next: truncating the
             * chain here is strictly safer than appending into a foreign
             * block. */
            kernel_waitable_chain_diag("enqueue-walk", waitable, tail->wait_next);
            return;
        }
        tail = tail->wait_next;
    }
    tail->wait_next = task;
}

static process_t *kernel_waitable_dequeue(kernel_waitable_t *waitable) {
    process_t *task;

    if (waitable == 0 || waitable->wait_head == 0) {
        return 0;
    }

    task = waitable->wait_head;
    waitable->wait_head = task->wait_next;
    task->wait_next = 0;
    return task;
}

void kernel_waitable_detach_task(kernel_waitable_t *waitable, process_t *task) {
    process_t *prev = 0;
    process_t *cursor;
    uint32_t flags;
    uint32_t steps = 0u;

    if (waitable == 0 || task == 0) {
        return;
    }

    flags = spinlock_lock_irqsave(&waitable->lock);
    cursor = waitable->wait_head;
    while (cursor != 0) {
        if (!kernel_waitable_task_node_ok(cursor)) {
            kernel_waitable_chain_diag("detach-walk", waitable, cursor);
            break;
        }
        if (cursor == task) {
            if (prev == 0) {
                waitable->wait_head = cursor->wait_next;
            } else {
                prev->wait_next = cursor->wait_next;
            }
            cursor->wait_next = 0;
            spinlock_unlock_irqrestore(&waitable->lock, flags);
            return;
        }
        prev = cursor;
        if (++steps > KERNEL_WAITABLE_WALK_CAP) {
            kernel_waitable_chain_diag("detach-cap", waitable, cursor->wait_next);
            break;
        }
        cursor = cursor->wait_next;
    }
    spinlock_unlock_irqrestore(&waitable->lock, flags);
}

void kernel_waitable_init_ex(kernel_waitable_t *waitable,
                             uint32_t event_kind,
                             uint32_t event_class,
                             uint32_t owner_service) {
    if (waitable == 0) {
        return;
    }

    spinlock_init(&waitable->lock);
    waitable->event_kind = event_kind;
    waitable->event_class = event_class;
    waitable->owner_service = owner_service;
    waitable->pending_signals = 0u;
    waitable->signal_count = 0u;
    waitable->wake_count = 0u;
    waitable->cancel_count = 0u;
    waitable->timeout_count = 0u;
    waitable->wait_head = 0;
}

void kernel_waitable_init(kernel_waitable_t *waitable) {
    kernel_waitable_init_ex(waitable,
                            TASK_WAIT_EVENT_WAITABLE,
                            TASK_WAIT_CLASS_GENERIC,
                            0u);
}

int kernel_waitable_try_wait(kernel_waitable_t *waitable) {
    uint32_t flags;
    int ready = -1;

    if (waitable == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&waitable->lock);
    if (waitable->pending_signals != 0u) {
        waitable->pending_signals -= 1u;
        waitable->signal_count += 1u;
        ready = 0;
    }
    spinlock_unlock_irqrestore(&waitable->lock, flags);
    return ready;
}

int kernel_waitable_wait_timeout(kernel_waitable_t *waitable, uint32_t timeout_ticks) {
    process_t *current;
    uint32_t flags;
    uint32_t deadline = 0u;

    if (waitable == 0) {
        return -1;
    }

    current = scheduler_current();
    if (current == 0) {
        return -1;
    }
    if (timeout_ticks != 0u) {
        deadline = kernel_timer_get_ticks() + timeout_ticks;
    }

    /*
     * wait_result is sticky from the previous completed wait (the consume
     * path leaves it SIGNALED). Clear it once here so the block-abort check
     * in scheduler_block_current_ex() only reacts to a signal racing THIS
     * wait, and legacy callers that block without clearing cannot livelock
     * on a stale SIGNALED.
     */
    current->wait_result = TASK_WAIT_RESULT_NONE;

    for (;;) {
        flags = spinlock_lock_irqsave(&waitable->lock);
        if (waitable->pending_signals != 0u) {
            waitable->pending_signals -= 1u;
            waitable->signal_count += 1u;
            current->wait_result = TASK_WAIT_RESULT_SIGNALED;
            spinlock_unlock_irqrestore(&waitable->lock, flags);
            return TASK_WAIT_RESULT_SIGNALED;
        }

        /*
         * Bug #19 (01/11, root cause of the storage-host jam): the
         * enqueue guard must ask REAL chain membership (wait_next), not
         * the `wait_channel` hint. The inline-wake path in
         * scheduler_complete_wait() leaves `wait_channel` pointing at this
         * waitable while the signal() pop ALREADY removed the task from
         * the chain; on the next wait the old guard believed the task was
         * still linked, skipped enqueue(), and the loop-top check tests
         * only `pending_signals` — never queue depth. A message posted
         * after the signal consumed the credit therefore left the task
         * asleep OUTSIDE the chain with depth>0: no future push can ever
         * wake it (head=0), no timeout sweep can reach it (not linked),
         * census shows waiters=1 only when a LATER push re-links via the
         * peek-break path. Measured: `ipc-pos head=0` right after a
         * successful pop-wake, then queue depth grows 1→16 with
         * `waiters=1 wake=0` and 8192-tick reply timeouts on EVERY
         * request. wait_channel is still maintained as the hint the
         * cancel/detach/timeout paths use.
         */
        if (current->wait_next == 0) {
            current->wait_channel = waitable;
            kernel_waitable_enqueue(waitable, current);
        } else if (current->wait_channel != waitable) {
            current->wait_channel = waitable;
        }
        spinlock_unlock_irqrestore(&waitable->lock, flags);

        if (scheduler_block_current_ex(waitable,
                                       deadline,
                                       waitable->event_kind,
                                       waitable->event_class,
                                       waitable->owner_service) != 0) {
            if (current->wait_result == TASK_WAIT_RESULT_SIGNALED) {
                /*
                 * The block aborted because a signal raced the enqueue->block
                 * window. The waitable still holds the pending count, so the
                 * next pass consumes it and returns SIGNALED.
                 */
                continue;
            }
            return -1;
        }

        /*
         * The wait can complete synchronously before we actually yield. This
         * happens when a lifecycle/event publication wakes the current task as
         * part of the block transition itself. If we yield after that wakeup,
         * the scheduler may resume an older saved context instead of the live
         * frame we are about to block from.
         */
        if (current->wait_result == TASK_WAIT_RESULT_SIGNALED) {
            waitable->wake_count += 1u;
            return TASK_WAIT_RESULT_SIGNALED;
        }
        if (current->wait_result == TASK_WAIT_RESULT_TIMED_OUT) {
            waitable->timeout_count += 1u;
            return TASK_WAIT_RESULT_TIMED_OUT;
        }
        if (current->wait_result == TASK_WAIT_RESULT_CANCELED) {
            waitable->cancel_count += 1u;
            return TASK_WAIT_RESULT_CANCELED;
        }

        /*
         * Bug #19 (31/10): a signal that landed in the enqueue->block window
         * is now left pending by kernel_waitable_signal() instead of
         * detaching this not-yet-BLOCKED head (measured: `cliQ=1
         * cliW{pend=1 wake=0}` — reply queued, wake never happened, client
         * slept to the 8192-tick deadline; run 19_run6). Now that the block
         * succeeded, consume such a stranded signal before yielding: the
         * message is already in the channel.
         */
        flags = spinlock_lock_irqsave(&waitable->lock);
        if (waitable->pending_signals != 0u &&
            waitable->wait_head == current) {
            waitable->pending_signals -= 1u;
            waitable->signal_count += 1u;
            waitable->wait_head = current->wait_next;
            spinlock_unlock_irqrestore(&waitable->lock, flags);
            waitable->wake_count += 1u;
            if (scheduler_complete_wait(current,
                                        TASK_WAIT_RESULT_SIGNALED) == 0) {
                return TASK_WAIT_RESULT_SIGNALED;
            }
            /* State flipped under us; the loop-top pending check
             * re-consumes on the next pass. */
            continue;
        }
        spinlock_unlock_irqrestore(&waitable->lock, flags);

        yield();

        if (current->wait_result == TASK_WAIT_RESULT_SIGNALED) {
            waitable->wake_count += 1u;
            return TASK_WAIT_RESULT_SIGNALED;
        }
        if (current->wait_result == TASK_WAIT_RESULT_TIMED_OUT) {
            waitable->timeout_count += 1u;
            return TASK_WAIT_RESULT_TIMED_OUT;
        }
        if (current->wait_result == TASK_WAIT_RESULT_CANCELED) {
            waitable->cancel_count += 1u;
            return TASK_WAIT_RESULT_CANCELED;
        }
    }
}

int kernel_waitable_wait(kernel_waitable_t *waitable) {
    return kernel_waitable_wait_timeout(waitable, 0u);
}

void kernel_waitable_signal(kernel_waitable_t *waitable, uint32_t count) {
    uint32_t flags;
    uint32_t woke = 0u;

    if (waitable == 0 || count == 0u) {
        return;
    }

    flags = spinlock_lock_irqsave(&waitable->lock);
    waitable->pending_signals += count;
    spinlock_unlock_irqrestore(&waitable->lock, flags);

    while (count != 0u) {
        process_t *task;

        flags = spinlock_lock_irqsave(&waitable->lock);
        task = waitable->wait_head;
        if (task != 0 && task->state != PROCESS_BLOCKED) {
            /*
             * Bug #19 (19_run6 fingerprint: `cliQ=1 cliW{pend=1 wake=0}` on
             * a blocked IPC client with the reply already queued): the chain
             * head is inside the enqueue->block window —
             * kernel_waitable_wait_timeout() linked it but
             * scheduler_block_current_ex() has not run yet, so
             * scheduler_complete_wait() would refuse it (-1) and the pop
             * would strand the pending signal: the task blocks OUTSIDE the
             * chain with its channel cleared and sleeps to the deadline.
             * Leave the chain and the pending count alone: the head's
             * post-block re-check consumes the signal itself.
             */
            spinlock_unlock_irqrestore(&waitable->lock, flags);
            break;
        }
        task = kernel_waitable_dequeue(waitable);
        if (task != 0) {
            task->wait_channel = 0;
            /*
             * Bug #19 item 0.10 (01/11): a wake delivered through the chain
             * is CONSUMED by that wake — the woken task returns SIGNALED
             * from wait_result (post-block check or after yield), never
             * through the loop-top pending check — so the pop decrements
             * the count added above (guarded against the multi-waiter
             * put-back/refusal path that can already have returned this
             * signal's unit to the semaphore). Without this, every
             * delivered signal leaked +1 pending forever (measured
             * `cliW{pend=3 wake=0}` on the shell's reply queue, 19_run12):
             * each leaked unit makes the NEXT wait return one phantom
             * SIGNALED with nothing behind it, and the counters that
             * localize real lost-wakes were permanently dishonest. If the
             * wake is then refused, the unit is put back below.
             */
            if (waitable->pending_signals != 0u) {
                waitable->pending_signals -= 1u;
            }
        }
        spinlock_unlock_irqrestore(&waitable->lock, flags);

        if (task == 0) {
            break;
        }
        if (scheduler_complete_wait(task, TASK_WAIT_RESULT_SIGNALED) == 0) {
            count -= 1u;
            woke += 1u;
        } else {
            /* The task could not accept the wake (its state flipped under
             * us, e.g. the timeout sweep published TIMED_OUT first): return
             * the consumed unit so a future wait still sees the queued
             * message through the loop-top consume. */
            flags = spinlock_lock_irqsave(&waitable->lock);
            waitable->pending_signals += 1u;
            spinlock_unlock_irqrestore(&waitable->lock, flags);
        }
    }

    if (woke != 0u) {
        smp_wake_sleeping_cpus();
    }
}

void kernel_waitable_cancel(kernel_waitable_t *waitable, uint32_t count) {
    uint32_t flags;
    uint32_t woke = 0u;

    if (waitable == 0 || count == 0u) {
        return;
    }

    while (count != 0u) {
        process_t *task;

        flags = spinlock_lock_irqsave(&waitable->lock);
        task = waitable->wait_head;
        if (task != 0 && task->state != PROCESS_BLOCKED) {
            /* Same enqueue->block window as kernel_waitable_signal():
             * never pop a head that cannot consume the cancel yet. */
            spinlock_unlock_irqrestore(&waitable->lock, flags);
            break;
        }
        task = kernel_waitable_dequeue(waitable);
        if (task != 0) {
            task->wait_channel = 0;
        }
        spinlock_unlock_irqrestore(&waitable->lock, flags);
        if (task == 0) {
            break;
        }
        if (scheduler_complete_wait(task, TASK_WAIT_RESULT_CANCELED) == 0) {
            count -= 1u;
            woke += 1u;
        }
    }

    if (woke != 0u) {
        smp_wake_sleeping_cpus();
    }
}

void kernel_mailbox_init(kernel_mailbox_t *mailbox,
                         void *storage,
                         uint32_t item_size,
                         uint32_t capacity,
                         uint32_t overflow_policy,
                         uint32_t event_class,
                         uint32_t owner_service) {
    if (mailbox == 0) {
        return;
    }

    spinlock_init(&mailbox->lock);
    kernel_waitable_init_ex(&mailbox->waitable,
                            TASK_WAIT_EVENT_QUEUE,
                            event_class,
                            owner_service);
    mailbox->storage = storage;
    mailbox->item_size = item_size;
    mailbox->capacity = capacity;
    mailbox->head = 0u;
    mailbox->tail = 0u;
    mailbox->count = 0u;
    mailbox->dropped_count = 0u;
    mailbox->overflow_policy = overflow_policy;
}

void kernel_mailbox_reset(kernel_mailbox_t *mailbox) {
    uint32_t event_class;
    uint32_t owner_service;

    if (mailbox == 0) {
        return;
    }

    event_class = mailbox->waitable.event_class;
    owner_service = mailbox->waitable.owner_service;
    kernel_mailbox_init(mailbox,
                        mailbox->storage,
                        mailbox->item_size,
                        mailbox->capacity,
                        mailbox->overflow_policy,
                        event_class,
                        owner_service);
}

int kernel_mailbox_try_send(kernel_mailbox_t *mailbox, const void *item) {
    uint8_t *storage;
    uint32_t flags;

    if (mailbox == 0 || item == 0 || mailbox->storage == 0 ||
        mailbox->item_size == 0u || mailbox->capacity == 0u) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&mailbox->lock);
    if (mailbox->count >= mailbox->capacity) {
        if (mailbox->overflow_policy == KERNEL_MAILBOX_DROP_OLDEST) {
            mailbox->head = (mailbox->head + 1u) % mailbox->capacity;
            mailbox->count -= 1u;
            mailbox->dropped_count += 1u;
        } else {
            mailbox->dropped_count += 1u;
            spinlock_unlock_irqrestore(&mailbox->lock, flags);
            return -1;
        }
    }

    storage = (uint8_t *)mailbox->storage;
    memcpy(&storage[mailbox->tail * mailbox->item_size], item, mailbox->item_size);
    mailbox->tail = (mailbox->tail + 1u) % mailbox->capacity;
    mailbox->count += 1u;
    spinlock_unlock_irqrestore(&mailbox->lock, flags);
    kernel_waitable_signal(&mailbox->waitable, 1u);
    return 0;
}

int kernel_mailbox_try_receive(kernel_mailbox_t *mailbox, void *item) {
    uint8_t *storage;
    uint32_t flags;

    if (mailbox == 0 || item == 0 || mailbox->storage == 0 ||
        mailbox->item_size == 0u || mailbox->capacity == 0u) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&mailbox->lock);
    if (mailbox->count == 0u) {
        spinlock_unlock_irqrestore(&mailbox->lock, flags);
        return -1;
    }

    storage = (uint8_t *)mailbox->storage;
    memcpy(item, &storage[mailbox->head * mailbox->item_size], mailbox->item_size);
    mailbox->head = (mailbox->head + 1u) % mailbox->capacity;
    mailbox->count -= 1u;
    spinlock_unlock_irqrestore(&mailbox->lock, flags);
    return 0;
}

int kernel_mailbox_wait(kernel_mailbox_t *mailbox, uint32_t timeout_ticks) {
    if (mailbox == 0) {
        return -1;
    }
    if (timeout_ticks == 0u) {
        return kernel_waitable_wait(&mailbox->waitable);
    }
    return kernel_waitable_wait_timeout(&mailbox->waitable, timeout_ticks);
}

int kernel_mailbox_receive_timeout(kernel_mailbox_t *mailbox,
                                   void *item,
                                   uint32_t timeout_ticks) {
    int wait_rc;

    if (mailbox == 0 || item == 0) {
        return -1;
    }

    for (;;) {
        if (kernel_mailbox_try_receive(mailbox, item) == 0) {
            return 0;
        }
        if (timeout_ticks == 0u) {
            return -1;
        }
        wait_rc = kernel_mailbox_wait(mailbox, timeout_ticks);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}

uint32_t kernel_mailbox_count(kernel_mailbox_t *mailbox) {
    uint32_t count;
    uint32_t flags;

    if (mailbox == 0) {
        return 0u;
    }

    flags = spinlock_lock_irqsave(&mailbox->lock);
    count = mailbox->count;
    spinlock_unlock_irqrestore(&mailbox->lock, flags);
    return count;
}

uint32_t kernel_mailbox_dropped(kernel_mailbox_t *mailbox) {
    uint32_t dropped;
    uint32_t flags;

    if (mailbox == 0) {
        return 0u;
    }

    flags = spinlock_lock_irqsave(&mailbox->lock);
    dropped = mailbox->dropped_count;
    spinlock_unlock_irqrestore(&mailbox->lock, flags);
    return dropped;
}

void kernel_mailbox_clear_dropped(kernel_mailbox_t *mailbox) {
    uint32_t flags;

    if (mailbox == 0) {
        return;
    }

    flags = spinlock_lock_irqsave(&mailbox->lock);
    mailbox->dropped_count = 0u;
    spinlock_unlock_irqrestore(&mailbox->lock, flags);
}

void kernel_signal_init(kernel_signal_t *signal, uint32_t event_class, uint32_t owner_service) {
    if (signal == 0) {
        return;
    }
    kernel_waitable_init_ex(&signal->waitable,
                            TASK_WAIT_EVENT_SIGNAL,
                            event_class,
                            owner_service);
}

void kernel_signal_raise(kernel_signal_t *signal, uint32_t count) {
    if (signal == 0) {
        return;
    }
    kernel_waitable_signal(&signal->waitable, count);
}

void kernel_signal_cancel(kernel_signal_t *signal, uint32_t count) {
    if (signal == 0) {
        return;
    }
    kernel_waitable_cancel(&signal->waitable, count);
}

int kernel_signal_wait(kernel_signal_t *signal, uint32_t timeout_ticks) {
    if (signal == 0) {
        return -1;
    }
    return kernel_waitable_wait_timeout(&signal->waitable, timeout_ticks);
}

void kernel_completion_init(kernel_completion_t *completion, uint32_t event_class, uint32_t owner_service) {
    if (completion == 0) {
        return;
    }
    kernel_waitable_init_ex(&completion->waitable,
                            TASK_WAIT_EVENT_COMPLETION,
                            event_class,
                            owner_service);
}

void kernel_completion_complete(kernel_completion_t *completion) {
    if (completion == 0) {
        return;
    }
    kernel_waitable_signal(&completion->waitable, 1u);
}

void kernel_completion_cancel(kernel_completion_t *completion) {
    if (completion == 0) {
        return;
    }
    kernel_waitable_cancel(&completion->waitable, 1u);
}

int kernel_completion_wait(kernel_completion_t *completion, uint32_t timeout_ticks) {
    if (completion == 0) {
        return -1;
    }
    return kernel_waitable_wait_timeout(&completion->waitable, timeout_ticks);
}
