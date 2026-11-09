#ifndef KERNEL_EVENT_H
#define KERNEL_EVENT_H

#include <stdint.h>
#include <include/userland_api.h>
#include <kernel/lock.h>

struct process;

typedef struct kernel_waitable {
    spinlock_t lock;
    uint32_t event_kind;
    uint32_t event_class;
    uint32_t owner_service;
    uint32_t pending_signals;
    uint32_t signal_count;
    uint32_t wake_count;
    uint32_t cancel_count;
    uint32_t timeout_count;
    struct process *wait_head;
} kernel_waitable_t;

enum kernel_mailbox_overflow_policy {
    KERNEL_MAILBOX_DROP_OLDEST = 0,
    KERNEL_MAILBOX_DROP_NEWEST = 1
};

void kernel_waitable_init(kernel_waitable_t *waitable);
void kernel_waitable_init_ex(kernel_waitable_t *waitable,
                             uint32_t event_kind,
                             uint32_t event_class,
                             uint32_t owner_service);
int kernel_waitable_try_wait(kernel_waitable_t *waitable);
int kernel_waitable_wait(kernel_waitable_t *waitable);
int kernel_waitable_wait_timeout(kernel_waitable_t *waitable, uint32_t timeout_ticks);
void kernel_waitable_signal(kernel_waitable_t *waitable, uint32_t count);
void kernel_waitable_cancel(kernel_waitable_t *waitable, uint32_t count);
void kernel_waitable_detach_task(kernel_waitable_t *waitable, struct process *task);

typedef struct kernel_mailbox {
    spinlock_t lock;
    kernel_waitable_t waitable;
    void *storage;
    uint32_t item_size;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t dropped_count;
    uint32_t overflow_policy;
} kernel_mailbox_t;

void kernel_mailbox_init(kernel_mailbox_t *mailbox,
                         void *storage,
                         uint32_t item_size,
                         uint32_t capacity,
                         uint32_t overflow_policy,
                         uint32_t event_class,
                         uint32_t owner_service);
void kernel_mailbox_reset(kernel_mailbox_t *mailbox);
int kernel_mailbox_try_send(kernel_mailbox_t *mailbox, const void *item);
int kernel_mailbox_try_receive(kernel_mailbox_t *mailbox, void *item);
int kernel_mailbox_wait(kernel_mailbox_t *mailbox, uint32_t timeout_ticks);
int kernel_mailbox_receive_timeout(kernel_mailbox_t *mailbox,
                                   void *item,
                                   uint32_t timeout_ticks);
uint32_t kernel_mailbox_count(kernel_mailbox_t *mailbox);
uint32_t kernel_mailbox_dropped(kernel_mailbox_t *mailbox);
uint32_t kernel_mailbox_pending(kernel_mailbox_t *mailbox);
void kernel_mailbox_clear_dropped(kernel_mailbox_t *mailbox);

typedef struct kernel_signal {
    kernel_waitable_t waitable;
} kernel_signal_t;

typedef struct kernel_completion {
    kernel_waitable_t waitable;
} kernel_completion_t;

void kernel_signal_init(kernel_signal_t *signal, uint32_t event_class, uint32_t owner_service);
void kernel_signal_raise(kernel_signal_t *signal, uint32_t count);
void kernel_signal_cancel(kernel_signal_t *signal, uint32_t count);
int kernel_signal_wait(kernel_signal_t *signal, uint32_t timeout_ticks);

void kernel_completion_init(kernel_completion_t *completion, uint32_t event_class, uint32_t owner_service);
void kernel_completion_complete(kernel_completion_t *completion);
void kernel_completion_cancel(kernel_completion_t *completion);
int kernel_completion_wait(kernel_completion_t *completion, uint32_t timeout_ticks);

#endif /* KERNEL_EVENT_H */
