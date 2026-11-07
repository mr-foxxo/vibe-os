#include <kernel/drivers/input/input.h>

#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/usb/usb_host.h>
#include <kernel/event.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/service.h>
#include <kernel/smp.h>

#define KERNEL_INPUT_EVENT_QUEUE_CAPACITY 256u
#define KERNEL_INPUT_KEY_QUEUE_CAPACITY 128u
#define KERNEL_INPUT_MOUSE_QUEUE_CAPACITY 128u

/*
 * Input-path stall diagnostics (KNOWN_BUGS "Sessao interativa trava no meio",
 * plano 0.6 probe). The counters bracket the three sides that can stop:
 * scancode bytes arriving from the 8042 (g_input_scancodes_read, defined here
 * and incremented by kernel_keyboard_poll in keyboard.c), keys produced into
 * the mailbox, and consumer polls/dequeues. The trace runs on a small budget
 * from the dequeue path itself — the path every keystroke poll takes — so it
 * stays alive exactly while the interactive session is alive.
 */
volatile unsigned int g_input_scancodes_read;
static volatile unsigned int g_input_keys_produced;
static volatile unsigned int g_input_polls;
static volatile unsigned int g_input_consumed;
static volatile unsigned int g_input_diag_trace_budget = 8u;
static volatile uint32_t g_input_diag_last_trace_tick;

static struct input_event g_input_event_storage[KERNEL_INPUT_EVENT_QUEUE_CAPACITY];
static kernel_mailbox_t g_input_event_mailbox;
static int g_input_key_storage[KERNEL_INPUT_KEY_QUEUE_CAPACITY];
static kernel_mailbox_t g_input_key_mailbox;
static struct mouse_state g_input_mouse_storage[KERNEL_INPUT_MOUSE_QUEUE_CAPACITY];
static kernel_mailbox_t g_input_mouse_mailbox;

void kernel_input_event_init(void) {
    memset(g_input_event_storage, 0, sizeof(g_input_event_storage));
    memset(g_input_key_storage, 0, sizeof(g_input_key_storage));
    memset(g_input_mouse_storage, 0, sizeof(g_input_mouse_storage));
    kernel_mailbox_init(&g_input_event_mailbox,
                        g_input_event_storage,
                        sizeof(g_input_event_storage[0]),
                        KERNEL_INPUT_EVENT_QUEUE_CAPACITY,
                        KERNEL_MAILBOX_DROP_OLDEST,
                        TASK_WAIT_CLASS_INPUT,
                        MK_SERVICE_INPUT);
    kernel_mailbox_init(&g_input_key_mailbox,
                        g_input_key_storage,
                        sizeof(g_input_key_storage[0]),
                        KERNEL_INPUT_KEY_QUEUE_CAPACITY,
                        KERNEL_MAILBOX_DROP_OLDEST,
                        TASK_WAIT_CLASS_INPUT,
                        MK_SERVICE_INPUT);
    kernel_mailbox_init(&g_input_mouse_mailbox,
                        g_input_mouse_storage,
                        sizeof(g_input_mouse_storage[0]),
                        KERNEL_INPUT_MOUSE_QUEUE_CAPACITY,
                        KERNEL_MAILBOX_DROP_OLDEST,
                        TASK_WAIT_CLASS_INPUT,
                        MK_SERVICE_INPUT);
}

int kernel_input_event_has_data(void) {
    return kernel_mailbox_count(&g_input_event_mailbox) != 0u;
}

int kernel_input_event_dequeue(struct input_event *event) {
    if (event == 0) {
        return 0;
    }

    if (kernel_mailbox_try_receive(&g_input_event_mailbox, event) != 0) {
        kernel_keyboard_poll();
        kernel_mouse_poll();
        kernel_usb_hid_poll();
        if (kernel_mailbox_try_receive(&g_input_event_mailbox, event) != 0) {
            memset(event, 0, sizeof(*event));
            return 0;
        }
    }
    return 1;
}

int kernel_input_event_wait(struct input_event *event) {
    int wait_rc;

    if (event == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_event_dequeue(event) != 0) {
            return 1;
        }
        wait_rc = kernel_mailbox_wait(&g_input_event_mailbox, 0u);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return 0;
        }
    }
}

void kernel_input_event_enqueue_key(int key) {
    struct input_event event;

    if (key == 0) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = INPUT_EVENT_KEY;
    event.value = key;
    g_input_keys_produced++;
    (void)kernel_mailbox_try_send(&g_input_event_mailbox, &event);
    (void)kernel_mailbox_try_send(&g_input_key_mailbox, &key);
    smp_wake_sleeping_cpus();
}

void kernel_input_event_enqueue_mouse(const struct mouse_state *state) {
    struct input_event event;

    if (state == 0) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = INPUT_EVENT_MOUSE;
    event.mouse = *state;
    (void)kernel_mailbox_try_send(&g_input_event_mailbox, &event);
    (void)kernel_mailbox_try_send(&g_input_mouse_mailbox, state);
    smp_wake_sleeping_cpus();
}

int kernel_input_key_event_has_data(void) {
    return kernel_mailbox_count(&g_input_key_mailbox) != 0u;
}

int kernel_input_key_event_dequeue(int *key) {
    uint32_t now;

    if (key == 0) {
        return 0;
    }

    g_input_polls++;
    now = kernel_timer_get_ticks();
    if (g_input_diag_trace_budget != 0u &&
        (uint32_t)(now - g_input_diag_last_trace_tick) >= 400u) {
        g_input_diag_trace_budget--;
        g_input_diag_last_trace_tick = now;
        kernel_debug_printf("input-diag tick=%u scancodes=%u produced=%u polls=%u consumed=%u drops=%u\n",
                            (unsigned int)now,
                            g_input_scancodes_read,
                            g_input_keys_produced,
                            g_input_polls,
                            g_input_consumed,
                            (unsigned int)kernel_mailbox_dropped(&g_input_key_mailbox));
    }

    if (kernel_mailbox_try_receive(&g_input_key_mailbox, key) == 0) {
        g_input_consumed++;
    } else {
        kernel_keyboard_poll();
        kernel_usb_hid_poll();
        if (kernel_mailbox_try_receive(&g_input_key_mailbox, key) == 0) {
            g_input_consumed++;
        } else {
            *key = 0;
            return 0;
        }
    }
    return 1;
}

int kernel_input_key_event_wait(int *key) {
    int wait_rc;

    if (key == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_key_event_dequeue(key) != 0) {
            return 1;
        }
        wait_rc = kernel_mailbox_wait(&g_input_key_mailbox, 0u);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return 0;
        }
    }
}

/*
 * Bounded event-driven key wait (bug #19 item 0.12, 01/11). The
 * interactive readers (rescue shell read_line, app read_line) polled
 * KEY_READ over IPC; the handler is non-blocking (dequeue-or-reply-1),
 * so a no-key answer comes back immediately and the loop degenerates
 * into a busy poll — measured at `polls 105->579` inside one ~620-tick
 * window with `produced` frozen at 16 and `drops=0` (gate run 19_ns3):
 * ~1 IPC request every 1.07 guest ticks hammering the service path,
 * which is also how the typed line's remaining keys got lost in
 * retry/racing cycles. This variant waits on the mailbox's own
 * waitable, so an IRQ-produced key wakes the reader directly and an
 * idle terminal costs zero IPC. timeout_ticks==0 waits indefinitely.
 * Returns 1 with *key set, 0 on timeout/abort.
 */
int kernel_input_key_event_wait_timeout(int *key, uint32_t timeout_ticks) {
    uint32_t start;
    uint32_t now;
    uint32_t elapsed;

    if (key == 0) {
        return 0;
    }
    if (timeout_ticks == 0u) {
        return kernel_input_key_event_wait(key);
    }

    start = kernel_timer_get_ticks();
    for (;;) {
        if (kernel_input_key_event_dequeue(key) != 0) {
            return 1;
        }
        now = kernel_timer_get_ticks();
        elapsed = (uint32_t)(now - start);
        if (elapsed >= timeout_ticks) {
            return 0;
        }
        if (kernel_mailbox_wait(&g_input_key_mailbox, timeout_ticks - elapsed) !=
            TASK_WAIT_RESULT_SIGNALED) {
            return 0;
        }
    }
}

void kernel_input_key_event_enqueue(int key) {
    kernel_input_event_enqueue_key(key);
}

int kernel_input_mouse_event_has_data(void) {
    return kernel_mailbox_count(&g_input_mouse_mailbox) != 0u;
}

int kernel_input_mouse_event_dequeue(struct mouse_state *state) {
    if (state == 0) {
        return 0;
    }

    if (kernel_mailbox_try_receive(&g_input_mouse_mailbox, state) != 0) {
        kernel_mouse_poll();
        kernel_usb_hid_poll();
        if (kernel_mailbox_try_receive(&g_input_mouse_mailbox, state) != 0) {
            memset(state, 0, sizeof(*state));
            return 0;
        }
    }
    return 1;
}

int kernel_input_mouse_event_wait(struct mouse_state *state) {
    int wait_rc;

    if (state == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_mouse_event_dequeue(state) != 0) {
            return 1;
        }
        wait_rc = kernel_mailbox_wait(&g_input_mouse_mailbox, 0u);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return 0;
        }
    }
}

void kernel_input_mouse_event_enqueue(const struct mouse_state *state) {
    kernel_input_event_enqueue_mouse(state);
}
