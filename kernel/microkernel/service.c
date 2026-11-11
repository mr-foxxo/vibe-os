#include <kernel/bootinfo.h>
#include <kernel/telemetry.h>
#include <kernel/microkernel/service.h>
#include <kernel/kernel_string.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/ipc.h>
#include <kernel/lock.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/message.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <include/userland_api.h>

static struct mk_service_record g_services[MK_SERVICE_SLOTS];
static mk_service_deferred_launcher_fn g_service_deferred_launchers[MK_SERVICE_SLOTS];
static int g_console_request_trace_emitted = 0;
static int g_console_worker_entry_trace_emitted = 0;
static int g_console_worker_trace_emitted = 0;
static int g_console_reply_trace_emitted = 0;
static int g_transport_fallback_budget = VIBE_TRACE_BUDGET(16u);
static int g_request_send_fail_budget = VIBE_TRACE_BUDGET(16u);
static int g_unexpected_reply_budget = VIBE_TRACE_BUDGET(16u);
static int g_request_timeout_budget = VIBE_TRACE_BUDGET(16u);
static int g_storage_request_trace_budget = VIBE_TRACE_BUDGET(8u);
static int g_storage_waitloop_trace_budget = VIBE_TRACE_BUDGET(16u);
/* P4 adendo 20: the hang is a USERLAND task (pid5, entry=userland_entry)
 * hammering ~20 requests/tick. Name WHICH message type per source. */
static int g_request_rate_trace_budget = VIBE_TRACE_BUDGET(8u);
static int g_storage_write_request_trace_emitted = 0;
static int g_storage_worker_trace_budget = VIBE_TRACE_BUDGET(8u);
static int g_storage_reply_trace_budget = 8;
static int g_interactive_request_trace_budget = 24;
static spinlock_t g_service_event_lock;
static kernel_waitable_t g_service_retry_waitable;
static process_t *g_service_retry_worker = 0;

/*
 * Re-bound 31/10 (bug #19, run 19_run10): 32 ticks is smaller than the
 * measured worst-case handshake of a synchronous request — client dispatch
 * (send) + host dispatch (process) + client dispatch (read reply), each leg
 * paying the up-to-timeslice preemption latency of the incumbent task even
 * after the IPC wake boost. A budget below that latency does not fail
 * gracefully: every overshoot schedules a retry (MK_SERVICE_BACKGROUND_
 * RETRY_BASE_TICKS=25) that RE-SENDS the in-flight request, the host drains
 * duplicates into replies the client no longer waits for, and the reply
 * path floods the client queue (measured: cliQ 1->16, cliW pend 1->31,
 * tmo 13 with wake=1 while the shell polled at 32-tick cadence) until the
 * deferred-reply cap (8) overflows and fails unrelated requests. 128 ticks
 * = the value MK_SERVICE_FILESYSTEM_TIMEOUT_TICKS already uses for the same
 * round-trip shape; it lets each poll block until the host actually answers,
 * so the loop self-paces at the host's rate without duplications. This is a
 * kernel-internal budget, not ABI; if the maintainer prefers another
 * magnitude it is this one constant.
 */
#define MK_SERVICE_REQUEST_REPLY_TIMEOUT_TICKS 128u
#define MK_SERVICE_VIDEO_CONTROL_TIMEOUT_TICKS 192u
#define MK_SERVICE_STORAGE_TIMEOUT_TICKS 8192u
#define MK_SERVICE_FILESYSTEM_TIMEOUT_TICKS 128u
#define MK_SERVICE_DEFERRED_REPLIES_MAX 8u
#define MK_SERVICE_RECOVERY_RETRY_LIMIT 1u
#define MK_SERVICE_BACKGROUND_RETRY_LIMIT 10u
#define MK_SERVICE_BACKGROUND_RETRY_BASE_TICKS 25u

static void mk_service_worker_entry(void);
static void mk_service_retry_worker_entry(void);
static void mk_service_publish_event(struct mk_service_record *service, uint32_t event_type);
static void mk_service_enqueue_event(struct mk_service_record *service,
                                     struct mk_service_event_subscription *subscription,
                                     uint32_t event_type);
static uint32_t mk_service_request_timeout_ticks(const struct mk_service_record *service,
                                                 const struct mk_message *request);
static void mk_service_restore_deferred_messages(process_t *destination,
                                                 const struct mk_message *messages,
                                                 uint32_t count);
static int mk_service_background_retryable(const struct mk_service_record *service);
static void mk_service_retry_reset(struct mk_service_record *service);
static int mk_service_schedule_async_recovery(struct mk_service_record *service,
                                              const struct mk_message *request);
static uint32_t mk_service_retry_delay_ticks(uint32_t retry_attempts);
static void mk_service_schedule_retry(struct mk_service_record *service, uint32_t now_ticks);
static void mk_service_note_retry_failure(struct mk_service_record *service, uint32_t now_ticks);
static uint32_t mk_service_process_background_retries(uint32_t now_ticks);

static int mk_service_type_allows_rescue_local_fallback(uint32_t service_type) {
    switch (service_type) {
    case MK_SERVICE_STORAGE:
    case MK_SERVICE_FILESYSTEM:
    case MK_SERVICE_CONSOLE:
    case MK_SERVICE_NETWORK:
        return 1;
    default:
        return 0;
    }
}

static uint32_t mk_service_task_class(uint32_t service_type) {
    switch (service_type) {
    case MK_SERVICE_STORAGE:
        return MK_TASK_CLASS_STORAGE_IO;
    case MK_SERVICE_FILESYSTEM:
        return MK_TASK_CLASS_FILESYSTEM_IO;
    case MK_SERVICE_VIDEO:
        return MK_TASK_CLASS_VIDEO_CONTROL;
    case MK_SERVICE_INPUT:
        return MK_TASK_CLASS_INPUT;
    case MK_SERVICE_CONSOLE:
        return MK_TASK_CLASS_CONSOLE_IO;
    case MK_SERVICE_NETWORK:
        return MK_TASK_CLASS_NETWORK_IO;
    case MK_SERVICE_AUDIO:
        return MK_TASK_CLASS_AUDIO_IO;
    case MK_SERVICE_INIT:
        return MK_TASK_CLASS_SUPERVISION;
    default:
        return MK_TASK_CLASS_SUPERVISION;
    }
}

static void mk_service_mark_transport_degraded(struct mk_service_record *service) {
    if (service == 0) {
        return;
    }
    if (service->transport_degraded == 0u) {
        service->transport_degraded = 1u;
        mk_service_publish_event(service, MK_SERVICE_EVENT_DEGRADED);
    }
    if (mk_service_background_retryable(service)) {
        kernel_waitable_signal(&g_service_retry_waitable, 1u);
    }
}

int mk_service_backend_bridge_allowed_current(uint32_t service_type) {
    process_t *current;
    const struct mk_launch_context *context;
    uint32_t boot_flags;

    current = scheduler_current();
    context = mk_launch_context_current();
    if (current == 0 || context == 0) {
        return 0;
    }

    if (current->kind == PROCESS_KIND_SERVICE &&
        (service_type == MK_SERVICE_NONE || current->service_type == service_type)) {
        return 1;
    }

    boot_flags = context->boot_flags;
    if ((boot_flags & (BOOTINFO_FLAG_BOOT_SAFE_MODE |
                       BOOTINFO_FLAG_BOOT_RESCUE_SHELL)) != 0u) {
        return 1;
    }

    if ((context->flags & (MK_LAUNCH_FLAG_BOOTSTRAP |
                           MK_LAUNCH_FLAG_CRITICAL)) != 0u) {
        return 1;
    }

    return 0;
}

static int mk_service_current_prefers_local_handler(const struct mk_service_record *service,
                                                    const struct mk_message *request) {
    const struct mk_launch_context *context;

    if (service == 0 || request == 0) {
        return 0;
    }

    if (service->type == MK_SERVICE_AUDIO &&
        request->type >= MK_MSG_AUDIO_GETINFO &&
        request->type <= MK_MSG_AUDIO_PLAY_ASSET) {
        context = mk_launch_context_current();
        if (context == 0) {
            return 1;
        }

        /*
         * Keep the compat-backed audio datapath in the kernel hot path so
         * startup/session playback doesn't pend on an extra service hop while
         * the userland control daemon is still bootstrapping.
         */
        if ((context->flags & (MK_LAUNCH_FLAG_BOOTSTRAP |
                               MK_LAUNCH_FLAG_CRITICAL |
                               MK_LAUNCH_FLAG_USER_DESKTOP)) != 0u) {
            return 1;
        }
        if (context->task_class == MK_TASK_CLASS_AUDIO_IO ||
            context->task_class == MK_TASK_CLASS_DESKTOP ||
            context->task_class == MK_TASK_CLASS_APP_RUNTIME) {
            return 1;
        }
    }

    return 0;
}

static int mk_service_current_allows_local_fallback(const struct mk_service_record *service,
                                                    const struct mk_message *request) {
    const struct mk_launch_context *context;

    if (service == 0) {
        return 0;
    }

    context = mk_launch_context_current();
    if (context == 0) {
        return 1;
    }
    if (!mk_service_backend_bridge_allowed_current(service->type)) {
        return 0;
    }

    /*
     * Keep the local handler bridge restricted to the minimal rescue/bootstrap
     * surface still required to bring up diagnostics and storage-backed
     * recovery flows. Interactive domains must exercise the explicit service
     * boundary even while degraded so restart/containment behavior stays real.
     */
    if (!mk_service_type_allows_rescue_local_fallback(service->type)) {
        return 0;
    }

    /*
     * The desktop session must keep using the explicit input service boundary
     * during steady-state restart/degradation handling so input resets and
     * re-subscribe logic are exercised instead of silently tunneling back to
     * the legacy kernel handler.
     */
    if (service->type == MK_SERVICE_INPUT &&
        request != 0 &&
        (context->flags & MK_LAUNCH_FLAG_USER_DESKTOP) != 0u) {
        return 0;
    }

    return 1;
}

static uint32_t mk_service_request_timeout_ticks(const struct mk_service_record *service,
                                                 const struct mk_message *request) {
    if (service == 0 || request == 0) {
        return MK_SERVICE_REQUEST_REPLY_TIMEOUT_TICKS;
    }

    if (service->type == MK_SERVICE_VIDEO &&
        (request->type == MK_MSG_VIDEO_MODE_SET || request->type == MK_MSG_VIDEO_LEAVE)) {
        /*
         * Real mode transitions remap framebuffers and re-arm the backend; they
         * legitimately take longer than the lightweight IPC/control messages
         * that use the default budget.
         */
        return MK_SERVICE_VIDEO_CONTROL_TIMEOUT_TICKS;
    }

    if (service->type == MK_SERVICE_STORAGE) {
        /*
         * Persist image loads/saves and chunked AppFS reads legitimately move
         * hundreds of kilobytes through transfers. Give the extracted storage
         * host enough budget to complete without falling back to the legacy
         * local path under normal desktop startup.
         */
        return MK_SERVICE_STORAGE_TIMEOUT_TICKS;
    }

    if (service->type == MK_SERVICE_FILESYSTEM &&
        (request->type == MK_MSG_FS_OPEN ||
         request->type == MK_MSG_FS_READ ||
         request->type == MK_MSG_FS_WRITE ||
         request->type == MK_MSG_FS_STAT ||
         request->type == MK_MSG_FS_FSTAT)) {
        return MK_SERVICE_FILESYSTEM_TIMEOUT_TICKS;
    }

    return MK_SERVICE_REQUEST_REPLY_TIMEOUT_TICKS;
}

static void mk_service_restore_deferred_messages(process_t *destination,
                                                 const struct mk_message *messages,
                                                 uint32_t count) {
    uint32_t index;

    if (destination == 0 || messages == 0) {
        return;
    }

    for (index = 0; index < count; ++index) {
        if (ipc_send(destination, &messages[index], sizeof(messages[index])) != 0) {
            break;
        }
    }
}

void mk_service_init(void) {
    process_t *retry_worker;

    memset(g_services, 0, sizeof(g_services));
    memset(g_service_deferred_launchers, 0, sizeof(g_service_deferred_launchers));
    spinlock_init(&g_service_event_lock);
    kernel_waitable_init_ex(&g_service_retry_waitable,
                            TASK_WAIT_EVENT_WAITABLE,
                            TASK_WAIT_CLASS_SUPERVISION,
                            0u);
    retry_worker = process_create_with_stack(mk_service_retry_worker_entry,
                                             PROCESS_KIND_KERNEL_TASK,
                                             MK_SERVICE_NONE,
                                             0u,
                                             MK_TASK_CLASS_SUPERVISION,
                                             4096u);
    if (retry_worker != 0) {
        g_service_retry_worker = retry_worker;
        scheduler_add_task(retry_worker);
    }
}

static int mk_service_name_equals(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    return strcmp(lhs, rhs) == 0;
}

static struct mk_service_record *mk_service_find_mutable_by_type(uint32_t type) {
    uint32_t slot;

    if (type == MK_SERVICE_NONE) {
        return 0;
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (g_services[slot].type == type) {
            return &g_services[slot];
        }
    }

    return 0;
}

static void mk_service_init_subscriptions(struct mk_service_record *service) {
    uint32_t index;

    if (service == 0) {
        return;
    }

    for (index = 0; index < MK_SERVICE_EVENT_SUBSCRIBERS; ++index) {
        struct mk_service_event_subscription *subscription = &service->subscribers[index];

        memset(subscription, 0, sizeof(*subscription));
        subscription->pid = 0;
        kernel_mailbox_init(&subscription->mailbox,
                            subscription->events,
                            sizeof(subscription->events[0]),
                            MK_SERVICE_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_NEWEST,
                            TASK_WAIT_CLASS_SUPERVISION,
                            service->type);
    }
    service->subscriptions_ready = 1u;
}

static int mk_service_register_impl(uint32_t type,
                                    const char *name,
                                    int pid,
                                    struct process *process,
                                    mk_service_local_handler_fn local_handler,
                                    void *context) {
    uint32_t slot;

    if (type == MK_SERVICE_NONE || name == 0) {
        return -1;
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (g_services[slot].type == type || mk_service_name_equals(g_services[slot].name, name)) {
            g_services[slot].type = type;
            if (pid != 0 || process != 0) {
                g_services[slot].pid = pid;
                g_services[slot].process = process;
                g_services[slot].transport_degraded = 0u;
                g_services[slot].last_error = 0u;
                g_services[slot].start_tick = kernel_timer_get_ticks();
                g_services[slot].pending_request_count = 0u;
                mk_service_retry_reset(&g_services[slot]);
                if (g_services[slot].subscriptions_ready == 0u) {
                    mk_service_init_subscriptions(&g_services[slot]);
                }
            }
            if (local_handler != 0) {
                g_services[slot].local_handler = local_handler;
                g_services[slot].context = context;
            }
            strncpy(g_services[slot].name, name, MK_SERVICE_NAME_MAX - 1u);
            g_services[slot].name[MK_SERVICE_NAME_MAX - 1u] = '\0';
            return 0;
        }
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (g_services[slot].type == MK_SERVICE_NONE) {
            g_services[slot].type = type;
            g_services[slot].pid = pid;
            g_services[slot].process = process;
            g_services[slot].local_handler = local_handler;
            g_services[slot].context = context;
            g_services[slot].transport_degraded = 0u;
            g_services[slot].last_error = 0u;
            g_services[slot].start_tick = kernel_timer_get_ticks();
            g_services[slot].pending_request_count = 0u;
            mk_service_retry_reset(&g_services[slot]);
            if (pid != 0 || process != 0) {
                mk_service_init_subscriptions(&g_services[slot]);
            } else {
                g_services[slot].subscriptions_ready = 0u;
            }
            strncpy(g_services[slot].name, name, MK_SERVICE_NAME_MAX - 1u);
            g_services[slot].name[MK_SERVICE_NAME_MAX - 1u] = '\0';
            return 0;
        }
    }

    return -1;
}

int mk_service_register(uint32_t type, const char *name, struct process *process) {
    if (process == 0) {
        return -1;
    }
    return mk_service_register_impl(type, name, process->pid, process, 0, 0);
}

int mk_service_register_local(uint32_t type, const char *name) {
    return mk_service_register_impl(type, name, 0, 0, 0, 0);
}

int mk_service_register_local_handler(uint32_t type,
                                      const char *name,
                                      mk_service_local_handler_fn handler,
                                      void *context) {
    if (handler == 0) {
        return -1;
    }
    return mk_service_register_impl(type, name, 0, 0, handler, context);
}

int mk_service_register_deferred_launcher(uint32_t type,
                                          mk_service_deferred_launcher_fn launcher) {
    if (type == MK_SERVICE_NONE || type >= MK_SERVICE_SLOTS || launcher == 0) {
        return -1;
    }
    g_service_deferred_launchers[type] = launcher;
    return 0;
}

static int mk_service_process_online(const struct mk_service_record *service) {
    if (service == 0) {
        return 0;
    }
    if (service->process == 0 || service->pid <= 0) {
        return 1;
    }
    return scheduler_find_task_by_pid(service->pid) != 0;
}

static void mk_service_discard_worker_record(struct mk_service_record *service) {
    if (service == 0) {
        return;
    }

    if (service->pid > 0 || service->process != 0) {
        mk_service_publish_event(service, MK_SERVICE_EVENT_OFFLINE);
    }

    if (service->process != 0) {
        scheduler_terminate_task(service->process);
    }
    if (service->pid > 0) {
        mk_launch_release_pid(service->pid);
    }

    service->pid = 0;
    service->process = 0;
}

static volatile uint32_t g_service_restart_fail_budget = 12u;

static void mk_service_restart_fail(const char *why, const struct mk_service_record *service) {
    if (g_service_restart_fail_budget == 0u || service == 0) {
        return;
    }
    g_service_restart_fail_budget--;
    kernel_debug_printf("service: restart FAIL %s type=%d name0=%c process=%x pid=%d handler=%x\n",
                        why, (int)service->type,
                        service->name[0] != '\0' ? service->name[0] : '0',
                        (unsigned int)(uintptr_t)service->process,
                        service->pid,
                        (unsigned int)(uintptr_t)service->local_handler);
}

static int mk_service_restart_worker_record(struct mk_service_record *service) {
    struct mk_launch_descriptor descriptor;
    int force_restart;
    int pid;

    if (service == 0 || service->type == MK_SERVICE_NONE) {
        return -1;
    }
    if (service->local_handler == 0) {
        mk_service_restart_fail("no-local-handler", service);
        return -1;
    }
    if (service->name[0] == '\0') {
        mk_service_restart_fail("empty-name", service);
        return -1;
    }
    if (service->process == 0 && service->pid == 0 && service->launch_flags == 0u) {
        return 0;
    }
    force_restart = service->transport_degraded != 0u;
    if (!force_restart && mk_service_process_online(service)) {
        return 0;
    }

    if (service->pid > 0) {
        kernel_debug_printf("service: restarting type=%d old_pid=%d restarts=%d\n",
                            (int)service->type,
                            service->pid,
                            (int)service->restart_count);
    }
    mk_service_discard_worker_record(service);

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_SERVICE;
    descriptor.service_type = service->type;
    descriptor.flags = service->launch_flags;
    descriptor.stack_size = service->stack_size;
    strncpy(descriptor.name, service->name, MK_LAUNCH_NAME_MAX - 1u);
    descriptor.name[MK_LAUNCH_NAME_MAX - 1u] = '\0';
    descriptor.entry = service->entry != 0 ? service->entry : mk_service_worker_entry;

    pid = mk_launch_bootstrap(&descriptor);
    if (pid < 0) {
        mk_service_note_retry_failure(service, kernel_timer_get_ticks());
        return -1;
    }

    service->transport_degraded = 0u;
    service->restart_count += 1u;
    mk_service_retry_reset(service);
    mk_service_publish_event(service, force_restart ? MK_SERVICE_EVENT_RESTARTED : MK_SERVICE_EVENT_ONLINE);
    return 0;
}

static void mk_service_publish_event(struct mk_service_record *service, uint32_t event_type) {
    uint32_t index;
    uint32_t flags;

    if (service == 0 || service->type == MK_SERVICE_NONE || event_type == MK_SERVICE_EVENT_NONE) {
        return;
    }

    flags = spinlock_lock_irqsave(&g_service_event_lock);
    for (index = 0; index < MK_SERVICE_EVENT_SUBSCRIBERS; ++index) {
        struct mk_service_event_subscription *subscription = &service->subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0) {
            continue;
        }
        if (scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            kernel_mailbox_init(&subscription->mailbox,
                                subscription->events,
                                sizeof(subscription->events[0]),
                                MK_SERVICE_EVENT_QUEUE_SIZE,
                                KERNEL_MAILBOX_DROP_NEWEST,
                                TASK_WAIT_CLASS_SUPERVISION,
                                service->type);
            continue;
        }
        mk_service_enqueue_event(service, subscription, event_type);
    }
    spinlock_unlock_irqrestore(&g_service_event_lock, flags);
}

static void mk_service_enqueue_event(struct mk_service_record *service,
                                     struct mk_service_event_subscription *subscription,
                                     uint32_t event_type) {
    struct mk_service_event event;

    if (service == 0 || subscription == 0 || event_type == MK_SERVICE_EVENT_NONE) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.abi_version = 1u;
    event.service_type = service->type;
    event.event_type = event_type;
    event.pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    event.restart_count = service->restart_count;
    event.transport_degraded = service->transport_degraded;
    event.tick = kernel_timer_get_ticks();

    (void)kernel_mailbox_try_send(&subscription->mailbox, &event);
}

static struct mk_service_event_subscription *mk_service_find_subscription(
    struct mk_service_record *service,
    const struct process *subscriber) {
    uint32_t index;

    if (service == 0 || subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0; index < MK_SERVICE_EVENT_SUBSCRIBERS; ++index) {
        struct mk_service_event_subscription *subscription = &service->subscribers[index];

        if (subscription->pid == subscriber->pid && subscription->process == subscriber) {
            return subscription;
        }
    }

    return 0;
}

static struct mk_service_event_subscription *mk_service_alloc_subscription(
    struct mk_service_record *service,
    struct process *subscriber) {
    uint32_t index;

    if (service == 0 || subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0; index < MK_SERVICE_EVENT_SUBSCRIBERS; ++index) {
        struct mk_service_event_subscription *subscription = &service->subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0 ||
            scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            subscription->pid = subscriber->pid;
            subscription->process = subscriber;
            kernel_mailbox_init(&subscription->mailbox,
                                subscription->events,
                                sizeof(subscription->events[0]),
                                MK_SERVICE_EVENT_QUEUE_SIZE,
                                KERNEL_MAILBOX_DROP_NEWEST,
                                TASK_WAIT_CLASS_SUPERVISION,
                                service->type);
            return subscription;
        }
    }

    return 0;
}

static int mk_service_reply_process(const struct mk_message *reply) {
    process_t *destination;

    if (reply == 0 || reply->target_pid == 0u) {
        return -1;
    }

    destination = scheduler_find_task_by_pid((int)reply->target_pid);
    if (destination == 0) {
        return -1;
    }

    if (reply->source_pid != 0u && g_storage_reply_trace_budget > 0 &&
        scheduler_find_task_by_pid((int)reply->source_pid) != 0) {
        process_t *source = scheduler_find_task_by_pid((int)reply->source_pid);
        if (source != 0 && source->service_type == MK_SERVICE_STORAGE) {
            g_storage_reply_trace_budget -= 1;
            kernel_debug_printf("service: storage reply src=%d dst=%d type=%d\n",
                                (int)reply->source_pid,
                                (int)reply->target_pid,
                                (int)reply->type);
        }
    }

    if (ipc_send(destination, reply, sizeof(*reply)) != 0) {
        kernel_debug_printf("service: reply send failed src=%d dst=%d type=%d\n",
                            (int)reply->source_pid,
                            (int)reply->target_pid,
                            (int)reply->type);
        return -1;
    }

    return 0;
}

static int mk_service_requeue_message(process_t *destination, const struct mk_message *message) {
    if (destination == 0 || message == 0) {
        return -1;
    }

    return ipc_send(destination, message, sizeof(*message));
}

static void mk_service_worker_step(const struct mk_service_record *service) {
    process_t *current;
    struct mk_message request;
    struct mk_message reply;

    current = scheduler_current();
    if (current == 0 || service == 0 || service->local_handler == 0) {
        yield();
        return;
    }

    if (ipc_receive_wait(current, &request, sizeof(request)) != (int)sizeof(request)) {
        return;
    }

    if (service->type == MK_SERVICE_CONSOLE && !g_console_worker_trace_emitted) {
        g_console_worker_trace_emitted = 1;
        kernel_debug_printf("service: console worker pid=%d received request type=%d from pid=%d\n",
                            current->pid,
                            (int)request.type,
                            (int)request.source_pid);
    }
    if (service->type == MK_SERVICE_STORAGE && g_storage_worker_trace_budget > 0) {
        g_storage_worker_trace_budget -= 1;
        kernel_debug_printf("service: storage worker pid=%d received type=%d from pid=%d\n",
                            current->pid,
                            (int)request.type,
                            (int)request.source_pid);
    }
    if ((service->type == MK_SERVICE_VIDEO ||
         service->type == MK_SERVICE_AUDIO ||
         service->type == MK_SERVICE_NETWORK ||
         service->type == MK_SERVICE_INPUT) &&
        g_interactive_request_trace_budget > 0) {
        g_interactive_request_trace_budget -= 1;
        kernel_debug_printf("service: worker svc=%d pid=%d type=%d src=%d\n",
                            (int)service->type,
                            current->pid,
                            (int)request.type,
                            (int)request.source_pid);
    }

    if (service->local_handler(&request, &reply, service->context) != 0) {
        mk_message_init(&reply, request.type);
        reply.source_pid = (uint32_t)current->pid;
        reply.target_pid = request.source_pid;
    }

    if (reply.source_pid == 0u) {
        reply.source_pid = (uint32_t)current->pid;
    }
    if (reply.target_pid == 0u) {
        reply.target_pid = request.source_pid;
    }

    (void)mk_service_reply_process(&reply);
}

static void mk_service_worker_entry(void) {
    for (;;) {
        const struct mk_launch_context *launch_context = mk_launch_context_current();
        const struct mk_service_record *service = 0;

        if (launch_context != 0) {
            service = mk_service_find_by_type(launch_context->service_type);
            if (launch_context->service_type == MK_SERVICE_CONSOLE &&
                !g_console_worker_entry_trace_emitted) {
                process_t *current = scheduler_current();

                g_console_worker_entry_trace_emitted = 1;
                kernel_debug_printf("service: console worker entry pid=%d launch_service=%d\n",
                                    current != 0 ? current->pid : -1,
                                    (int)launch_context->service_type);
            }
        }

        if (service == 0 || service->process == 0 || service->local_handler == 0) {
            yield();
            continue;
        }

        mk_service_worker_step(service);
    }
}

int mk_service_launch_worker(uint32_t type,
                             const char *name,
                             mk_service_local_handler_fn handler,
                             void *context,
                             uint32_t stack_size) {
    return mk_service_launch_task(type,
                                  name,
                                  handler,
                                  context,
                                  mk_service_worker_entry,
                                  stack_size,
                                  MK_LAUNCH_FLAG_BOOTSTRAP | MK_LAUNCH_FLAG_BUILTIN);
}

static volatile uint32_t g_service_prepare_fail_budget = 12u;

static void mk_service_prepare_fail(const char *why, uint32_t type) {
    if (g_service_prepare_fail_budget == 0u) {
        return;
    }
    g_service_prepare_fail_budget--;
    kernel_debug_printf("service: prepare FAIL %s type=%u\n", why, (unsigned int)type);
}

int mk_service_prepare_task(uint32_t type,
                            const char *name,
                            mk_service_local_handler_fn handler,
                            void *context,
                            mk_service_entry_fn entry,
                            uint32_t stack_size,
                            uint32_t launch_flags) {
    struct mk_service_record *service;

    if (type == MK_SERVICE_NONE || name == 0 || handler == 0 || entry == 0) {
        mk_service_prepare_fail("bad-args", type);
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service != 0 && service->process != 0) {
        /* Already launched: return the live pid as a POSITIVE signal.
         * Callers must treat >0 as success (see mk_service_launch_task). */
        return service->pid;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        uint32_t slot;

        for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
            if (g_services[slot].type == MK_SERVICE_NONE) {
                service = &g_services[slot];
                memset(service, 0, sizeof(*service));
                service->type = type;
                strncpy(service->name, name, MK_SERVICE_NAME_MAX - 1u);
                service->name[MK_SERVICE_NAME_MAX - 1u] = '\0';
                break;
            }
        }
        if (service == 0) {
            mk_service_prepare_fail("no-slot", type);
            return -1;
        }
    }

    service->local_handler = handler;
    service->context = context;
    service->entry = entry;
    service->launch_flags = launch_flags;
    service->stack_size = stack_size;
    return 0;
}

int mk_service_launch_task(uint32_t type,
                           const char *name,
                           mk_service_local_handler_fn handler,
                           void *context,
                           mk_service_entry_fn entry,
                           uint32_t stack_size,
                           uint32_t launch_flags) {
    struct mk_service_record *service;
    struct mk_launch_descriptor descriptor;
    int prepared;

    prepared = mk_service_prepare_task(type,
                                name,
                                handler,
                                context,
                                entry,
                                stack_size,
                                launch_flags);
    if (prepared < 0) {
        return -1;
    }
    if (prepared > 0) {
        /*
         * prepare_task reports an ALREADY-LAUNCHED service by returning its
         * live pid. Contract of THIS function (and of every caller: the
         * deferred launchers feeding mk_service_restart/mk_service_ensure,
         * and audio.c) is 0 = success, negative = failure — so the pid is
         * normalized to success. The old `!= 0` test made both the
         * already-running case and the successful fresh launch below read
         * as failures: init's deferred inputsvc/consolesvc/networksvc
         * restarts on smp2 (no record yet -> launcher -> launch_task ->
         * positive pid) were reporting "launch failed" for launches that
         * had actually SUCCEEDED.
         */
        return 0;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        return -1;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_SERVICE;
    descriptor.service_type = type;
    descriptor.flags = service->launch_flags;
    descriptor.task_class = mk_service_task_class(type);
    descriptor.stack_size = service->stack_size;
    strncpy(descriptor.name, name, MK_LAUNCH_NAME_MAX - 1u);
    descriptor.name[MK_LAUNCH_NAME_MAX - 1u] = '\0';
    descriptor.entry = service->entry;
    {
        int pid = mk_launch_bootstrap(&descriptor);

        if (pid < 0) {
            mk_service_note_retry_failure(service, kernel_timer_get_ticks());
            return -1;
        }
        mk_service_retry_reset(service);
        /* success = 0 (see contract above; no caller consumes the pid) */
        return 0;
    }
}

int mk_service_is_online(uint32_t type) {
    const struct mk_service_record *service = mk_service_find_by_type(type);

    if (service == 0) {
        return 0;
    }
    return mk_service_process_online(service);
}

int mk_service_ensure(uint32_t type) {
    struct mk_service_record *service = mk_service_find_mutable_by_type(type);

    if (service == 0) {
        if (type != MK_SERVICE_NONE &&
            type < MK_SERVICE_SLOTS &&
            g_service_deferred_launchers[type] != 0) {
            return g_service_deferred_launchers[type]();
        }
        return -1;
    }
    return mk_service_restart_worker_record(service);
}

int mk_service_restart(uint32_t type) {
    struct mk_service_record *service = mk_service_find_mutable_by_type(type);

    if (service == 0) {
        if (type != MK_SERVICE_NONE &&
            type < MK_SERVICE_SLOTS &&
            g_service_deferred_launchers[type] != 0) {
            return g_service_deferred_launchers[type]();
        }
        mk_service_prepare_fail("no-record-no-launcher", type);
        return -1;
    }
    if (service->type == MK_SERVICE_INIT) {
        mk_service_prepare_fail("init-not-restartable", type);
        return -1;
    }
    if (service->process == 0 && service->pid == 0) {
        return mk_service_restart_worker_record(service);
    }

    if (service->process != 0) {
        scheduler_publish_lifecycle_event(MK_TASK_EVENT_RESTART_REQUESTED,
                                          service->process);
    }
    service->transport_degraded = 1u;
    return mk_service_restart_worker_record(service);
}

static int mk_service_request_process(const struct mk_service_record *service,
                                      const struct mk_message *request,
                                      struct mk_message *reply) {
    process_t *current;
    struct mk_message request_copy;
    struct mk_message response;
    struct mk_message deferred_replies[MK_SERVICE_DEFERRED_REPLIES_MAX];
    uint32_t request_start_tick;
    uint32_t request_timeout_ticks;
    uint32_t waiting_pid;
    uint32_t deferred_reply_count = 0u;
    uint32_t recovery_attempts = 0u;

    if (service == 0 || request == 0 || reply == 0) {
        return -1;
    }

    current = scheduler_current();
    if (current == 0) {
        return -1;
    }

    request_copy = *request;
    if (request_copy.abi_version == 0u) {
        request_copy.abi_version = MK_MESSAGE_ABI_VERSION;
    }
    if (mk_message_validate(&request_copy) != 0) {
        return -1;
    }
    request_copy.source_pid = (uint32_t)current->pid;
    if (service->pid > 0) {
        request_copy.target_pid = (uint32_t)service->pid;
    }

    if ((current == service->process || mk_service_current_prefers_local_handler(service, &request_copy)) &&
        service->local_handler != 0) {
        return service->local_handler(&request_copy, reply, service->context);
    }

    for (;;) {
        const struct mk_service_record *live_service = mk_service_find_by_type(service->type);

        if (live_service != 0) {
            service = live_service;
        }
        if (service == 0 || service->process == 0 || service->pid <= 0) {
            struct mk_service_record *mutable_service =
                service != 0 ? mk_service_find_mutable_by_type(service->type) : 0;

            if (mutable_service != 0) {
                if (mk_service_schedule_async_recovery(mutable_service, &request_copy)) {
                    return -1;
                }
                if (recovery_attempts < MK_SERVICE_RECOVERY_RETRY_LIMIT &&
                    mk_service_restart_worker_record(mutable_service) == 0) {
                    recovery_attempts += 1u;
                    service = mk_service_find_by_type(mutable_service->type);
                    continue;
                }
            }
            if (service != 0 &&
                service->local_handler != 0 &&
                mk_service_current_allows_local_fallback(service, &request_copy)) {
                return service->local_handler(&request_copy, reply, service->context);
            }
            return -1;
        }

        request_copy.target_pid = (uint32_t)service->pid;
        waiting_pid = request_copy.target_pid;
        request_start_tick = kernel_timer_get_ticks();
        request_timeout_ticks = mk_service_request_timeout_ticks(service, &request_copy);

        if (service->type == MK_SERVICE_CONSOLE && !g_console_request_trace_emitted) {
            g_console_request_trace_emitted = 1;
            kernel_debug_printf("service: send request type=%d from pid=%d to console pid=%d\n",
                                (int)request_copy.type,
                                current->pid,
                                service->pid);
        }
        if (service->type == MK_SERVICE_STORAGE) {
            int trace_storage_request = 0;

            if (request_copy.type == MK_MSG_BLOCK_WRITE && !g_storage_write_request_trace_emitted) {
                g_storage_write_request_trace_emitted = 1;
                trace_storage_request = 1;
            } else if (g_storage_request_trace_budget > 0) {
                g_storage_request_trace_budget -= 1;
                trace_storage_request = 1;
            }
            if (trace_storage_request) {
                kernel_debug_printf("service: storage request type=%d from pid=%d to pid=%d\n",
                                    (int)request_copy.type,
                                    current->pid,
                                    service->pid);
            }
        }
        /*
         * P4 adendo 20 rate trace: budgeted once-per-(src,svc,type) first
         * sighting. The measured hang has a USERLAND task (pid5,
         * userland_entry) generating ~20 schedule_frame calls/tick as a
         * request/reply loop against the extracted hosts. This names the
         * message type and target so the spin can be localized to a call
         * site in the app ABI (which syscall loops) instead of theory.
         */
        if (g_request_rate_trace_budget > 0) {
            static struct { int src; uint32_t type; uint32_t svc; int seen; }
                rate_seen[16];
            static uint32_t rate_slots;
            uint32_t si;
            int dup = 0;

            for (si = 0u; si < rate_slots && si < 16u; ++si) {
                if (rate_seen[si].src == current->pid &&
                    rate_seen[si].type == request_copy.type &&
                    rate_seen[si].svc == service->type) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                if (rate_slots < 16u) {
                    rate_seen[rate_slots].src = current->pid;
                    rate_seen[rate_slots].type = request_copy.type;
                    rate_seen[rate_slots].svc = service->type;
                    rate_seen[rate_slots].seen = 1;
                    rate_slots++;
                }
                g_request_rate_trace_budget -= 1;
                kernel_debug_printf("service: first-of-kind req svc=%d type=%d src=%d dst=%d kind=%d user=%d tick=%u\n",
                                    (int)service->type,
                                    (int)request_copy.type,
                                    (int)current->pid,
                                    (int)service->pid,
                                    (int)current->kind,
                                    (int)current->runs_in_user_mode,
                                    (unsigned int)kernel_timer_get_ticks());
            }
        }
        if ((service->type == MK_SERVICE_VIDEO ||
             service->type == MK_SERVICE_AUDIO ||
             service->type == MK_SERVICE_NETWORK ||
             service->type == MK_SERVICE_INPUT) &&
            g_interactive_request_trace_budget > 0) {
            g_interactive_request_trace_budget -= 1;
            kernel_debug_printf("service: request svc=%d type=%d src=%d dst=%d\n",
                                (int)service->type,
                                (int)request_copy.type,
                                current->pid,
                                service->pid);
        }

        {
            struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);
            if (mutable_service != 0) {
                mutable_service->pending_request_count++;
            }
        }

        if (ipc_send(service->process, &request_copy, sizeof(request_copy)) != 0) {
            struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);

            if (mutable_service != 0) {
                if (mk_service_schedule_async_recovery(mutable_service, &request_copy)) {
                    return -1;
                }
                if (recovery_attempts < MK_SERVICE_RECOVERY_RETRY_LIMIT &&
                    mk_service_restart_worker_record(mutable_service) == 0) {
                    recovery_attempts += 1u;
                    service = mk_service_find_by_type(mutable_service->type);
                    continue;
                }
            }
            if (service->local_handler != 0 &&
                mk_service_current_allows_local_fallback(service, &request_copy)) {
                if (g_transport_fallback_budget > 0) {
                    g_transport_fallback_budget -= 1;
                    kernel_debug_printf("service: transport fallback type=%d src=%d dst=%d service=%d\n",
                                        (int)request_copy.type,
                                        (int)request_copy.source_pid,
                                        (int)request_copy.target_pid,
                                        (int)service->type);
                }
                return service->local_handler(&request_copy, reply, service->context);
            }
            if (g_request_send_fail_budget > 0) {
                g_request_send_fail_budget -= 1;
                kernel_debug_printf("service: request send failed type=%d src=%d dst=%d\n",
                                    (int)request_copy.type,
                                    (int)request_copy.source_pid,
                                    (int)request_copy.target_pid);
            }
            return -1;
        }

        for (;;) {
            uint32_t elapsed_ticks = kernel_timer_get_ticks() - request_start_tick;
            uint32_t remaining_ticks = 0u;
            int received;

            if (elapsed_ticks < request_timeout_ticks) {
                remaining_ticks = request_timeout_ticks - elapsed_ticks;
            }
            if (remaining_ticks == 0u) {
                received = -1;
            } else {
                received = ipc_receive_wait_timeout(current,
                                                    &response,
                                                    sizeof(response),
                                                    remaining_ticks);
            }

            /*
             * P4 wake-loss trace (adendo 20): the client loop kept waiting
             * WITHOUT the timeout branch ever firing, so the wait wakes on
             * SOME signal but never yields a matching reply. Sample the
             * task's own channel stats each iteration (budgeted): depth=
             * messages queued here, pending=stray signals, wake/timeout=
             * counter movement. depth>0 && received<0 => a message is stuck
             * behind a size/match filter; pending>0 && depth==0 => someone
             * signals the wrong channel (the host replies into a queue the
             * client does not read); wake climbing while depth stays 0 =>
             * phantom wakes (signal without message).
             */
            if (service->type == MK_SERVICE_STORAGE &&
                g_storage_waitloop_trace_budget > 0) {
                int depth = -1, waiters = -1;
                uint32_t pend = 0u, wake = 0u, tmo = 0u;

                g_storage_waitloop_trace_budget -= 1;
                ipc_queue_stats(current, &depth, &pend, &wake, &tmo, &waiters);
                kernel_debug_printf("storage waitloop: src=%d rem=%u recv=%d r{src=%d dst=%d type=%d} depth=%d pend=%u wake=%u tmo=%u waiters=%d\n",
                                    (int)request_copy.source_pid,
                                    (unsigned int)remaining_ticks,
                                    received,
                                    (received == (int)sizeof(response)) ?
                                        (int)response.source_pid : -999,
                                    (received == (int)sizeof(response)) ?
                                        (int)response.target_pid : -999,
                                    (received == (int)sizeof(response)) ?
                                        (int)response.type : -999,
                                    depth,
                                    (unsigned int)pend, (unsigned int)wake,
                                    (unsigned int)tmo, waiters);
            }

                    if (received == (int)sizeof(response)) {
                if (response.source_pid == waiting_pid &&
                    response.target_pid == (uint32_t)current->pid) {
                    mk_service_restore_deferred_messages(current,
                                                         deferred_replies,
                                                         deferred_reply_count);
                    deferred_reply_count = 0u;
                    {
                        struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);
                        if (mutable_service != 0 && mutable_service->pending_request_count > 0u) {
                            mutable_service->pending_request_count--;
                        }
                    }
                    if (service->type == MK_SERVICE_CONSOLE && !g_console_reply_trace_emitted) {
                        g_console_reply_trace_emitted = 1;
                        kernel_debug_printf("service: console reply type=%d src=%d dst=%d\n",
                                            (int)response.type,
                                            (int)response.source_pid,
                                            (int)response.target_pid);
                    }
                    {
                        struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);

                        if (mutable_service != 0) {
                            if (mutable_service->transport_degraded != 0u) {
                                mutable_service->transport_degraded = 0u;
                                mk_service_publish_event(mutable_service, MK_SERVICE_EVENT_RECOVERED);
                            }
                        }
                    }
                    *reply = response;
                    return 0;
                }
                if (g_unexpected_reply_budget > 0) {
                    g_unexpected_reply_budget -= 1;
                    kernel_debug_printf("service: unexpected reply src=%d dst=%d waiting_src=%d waiting_dst=%d\n",
                                        (int)response.source_pid,
                                        (int)response.target_pid,
                                        (int)waiting_pid,
                                        current->pid);
                }
                if (deferred_reply_count >= MK_SERVICE_DEFERRED_REPLIES_MAX) {
                    mk_service_restore_deferred_messages(current,
                                                         deferred_replies,
                                                         deferred_reply_count);
                    deferred_reply_count = 0u;
                    (void)mk_service_requeue_message(current, &response);
                    return -1;
                }
                deferred_replies[deferred_reply_count++] = response;
                continue;
            }

            if ((uint32_t)(kernel_timer_get_ticks() - request_start_tick) >=
                request_timeout_ticks) {
                struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(service->type);

                if (g_request_timeout_budget > 0) {
                    g_request_timeout_budget -= 1;
                    /*
                     * Host-state peek (KNOWN_BUGS "Sessao interativa trava no
                     * meio" / plano 0.6): when a request times out, the two
                     * possible culprits are (a) the host task is alive but
                     * never dequeued this reply, or (b) the host task itself is
                     * stuck/starved. Print the host task's scheduler state so
                     * the stall localizes to a side by MEASUREMENT, not theory.
                     */
                    {
                        process_t *host = scheduler_find_task_by_pid(service->pid);
                        int host_depth = -1, host_waiters = -1;
                        uint32_t host_pending = 0u, host_wake = 0u, host_tmo = 0u;
                        int cli_depth = -1;
                        uint32_t cli_pending = 0u, cli_wake = 0u, cli_tmo = 0u;

                        ipc_queue_stats(host, &host_depth, &host_pending,
                                        &host_wake, &host_tmo, &host_waiters);
                        ipc_queue_stats(current, &cli_depth, &cli_pending,
                                        &cli_wake, &cli_tmo, NULL);
                        kernel_debug_printf("service: request timeout svc=%d type=%d src=%d dst=%d host-state=%d host-cpu=%d hostQ=%d hostW{pend=%u wake=%u tmo=%u waiters=%d} cliQ=%d cliW{pend=%u wake=%u tmo=%u}\n",
                                            (int)service->type,
                                            (int)request_copy.type,
                                            (int)request_copy.source_pid,
                                            (int)request_copy.target_pid,
                                            host ? (int)host->state : -999,
                                            host ? host->current_cpu : -999,
                                            host_depth,
                                            (unsigned int)host_pending,
                                            (unsigned int)host_wake,
                                            (unsigned int)host_tmo,
                                            host_waiters,
                                            cli_depth,
                                            (unsigned int)cli_pending,
                                            (unsigned int)cli_wake,
                                            (unsigned int)cli_tmo);
                    }
                }

                if (mutable_service != 0) {
                    if (mk_service_schedule_async_recovery(mutable_service, &request_copy)) {
                        mk_service_restore_deferred_messages(current,
                                                             deferred_replies,
                                                             deferred_reply_count);
                        return -1;
                    }
                    if (recovery_attempts < MK_SERVICE_RECOVERY_RETRY_LIMIT &&
                        mk_service_restart_worker_record(mutable_service) == 0) {
                        recovery_attempts += 1u;
                        service = mk_service_find_by_type(mutable_service->type);
                        break;
                    }
                    if (mutable_service->local_handler != 0 &&
                        mk_service_current_allows_local_fallback(mutable_service, &request_copy)) {
                        if (g_transport_fallback_budget > 0) {
                            g_transport_fallback_budget -= 1;
                            kernel_debug_printf("service: timeout fallback type=%d src=%d dst=%d service=%d\n",
                                                (int)request_copy.type,
                                                (int)request_copy.source_pid,
                                                (int)request_copy.target_pid,
                                                (int)service->type);
                        }
                        mk_service_restore_deferred_messages(current,
                                                             deferred_replies,
                                                             deferred_reply_count);
                        return mutable_service->local_handler(&request_copy, reply, mutable_service->context);
                    }
                }

                mk_service_restore_deferred_messages(current,
                                                     deferred_replies,
                                                     deferred_reply_count);
                return -1;
            }

            live_service = mk_service_find_by_type(service->type);
            if (live_service == 0) {
                mk_service_restore_deferred_messages(current,
                                                     deferred_replies,
                                                     deferred_reply_count);
                return -1;
            }
            if (live_service->pid != (int)waiting_pid ||
                !mk_service_process_online(live_service)) {
                service = live_service;
                break;
            }
        }
    }
}

int mk_service_request(uint32_t type, const struct mk_message *request, struct mk_message *reply) {
    const struct mk_service_record *service;

    if (request == 0 || reply == 0) {
        return -1;
    }

    service = mk_service_find_by_type(type);
    if (service == 0) {
        return -1;
    }

    if (service->transport_degraded != 0u &&
        service->local_handler != 0 &&
        mk_service_current_allows_local_fallback(service, request)) {
        return service->local_handler(request, reply, service->context);
    }

    if (service->process != 0) {
        if (!mk_service_process_online(service)) {
            struct mk_service_record *mutable_service = mk_service_find_mutable_by_type(type);

            if (mutable_service != 0 && mk_service_schedule_async_recovery(mutable_service, request)) {
                return -1;
            }
        }
        service = mk_service_find_by_type(type);
        if (service != 0 && service->process != 0 && mk_service_process_online(service)) {
            return mk_service_request_process(service, request, reply);
        }
    }

    if (service->local_handler != 0 &&
        mk_service_current_allows_local_fallback(service, request)) {
        return service->local_handler(request, reply, service->context);
    }

    return -1;
}

int mk_service_subscribe(uint32_t type, struct process *subscriber) {
    struct mk_service_record *service;
    struct mk_service_event_subscription *subscription;
    uint32_t flags;
    int rc = -1;

    if (subscriber == 0) {
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_service_event_lock);
    subscription = mk_service_find_subscription(service, subscriber);
    if (subscription != 0) {
        rc = 0;
        goto done;
    }

    subscription = mk_service_alloc_subscription(service, subscriber);
    if (subscription == 0) {
        goto done;
    }

    if (service->transport_degraded != 0u) {
        mk_service_enqueue_event(service, subscription, MK_SERVICE_EVENT_DEGRADED);
    } else if (mk_service_process_online(service)) {
        mk_service_enqueue_event(service, subscription, MK_SERVICE_EVENT_ONLINE);
    } else {
        mk_service_enqueue_event(service, subscription, MK_SERVICE_EVENT_OFFLINE);
    }
    rc = 0;

done:
    spinlock_unlock_irqrestore(&g_service_event_lock, flags);
    return rc;
}

int mk_service_unsubscribe(uint32_t type, struct process *subscriber) {
    struct mk_service_record *service;
    struct mk_service_event_subscription *subscription;
    uint32_t flags;
    int rc = -1;

    if (subscriber == 0) {
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_service_event_lock);
    subscription = mk_service_find_subscription(service, subscriber);
    if (subscription == 0) {
        goto done;
    }

    memset(subscription, 0, sizeof(*subscription));
    rc = 0;

done:
    spinlock_unlock_irqrestore(&g_service_event_lock, flags);
    return rc;
}

int mk_service_event_receive(uint32_t type,
                             struct process *subscriber,
                             struct mk_service_event *event,
                             uint32_t timeout_ticks) {
    struct mk_service_record *service;
    int wait_rc;
    uint32_t flags;

    if (subscriber == 0 || event == 0) {
        return -1;
    }

    service = mk_service_find_mutable_by_type(type);
    if (service == 0) {
        return -1;
    }

    for (;;) {
        struct mk_service_event_subscription *subscription;

        flags = spinlock_lock_irqsave(&g_service_event_lock);
        subscription = mk_service_find_subscription(service, subscriber);
        if (subscription == 0) {
            spinlock_unlock_irqrestore(&g_service_event_lock, flags);
            return -1;
        }
        if (kernel_mailbox_try_receive(&subscription->mailbox, event) == 0) {
            spinlock_unlock_irqrestore(&g_service_event_lock, flags);
            return 0;
        }
        spinlock_unlock_irqrestore(&g_service_event_lock, flags);
        if (timeout_ticks == 0u) {
            return -1;
        }
        wait_rc = kernel_mailbox_wait(&subscription->mailbox, timeout_ticks);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}

void mk_service_fill_task_snapshot(struct task_snapshot_entry *entry) {
    const struct mk_service_record *service;

    if (entry == 0 || entry->service_type == MK_SERVICE_NONE) {
        return;
    }

    service = mk_service_find_by_type(entry->service_type);
    if (service == 0) {
        return;
    }

    if (mk_service_process_online(service)) {
        entry->flags |= TASK_SNAPSHOT_FLAG_SERVICE_ONLINE;
    }
    if (service->transport_degraded != 0u) {
        entry->flags |= TASK_SNAPSHOT_FLAG_SERVICE_DEGRADED;
    }
    if (service->local_handler != 0 &&
        (service->process != 0 || service->pid != 0 || service->launch_flags != 0u)) {
        entry->flags |= TASK_SNAPSHOT_FLAG_SERVICE_RESTARTABLE;
    }
    entry->service_restart_count = service->restart_count;
}

const struct mk_service_record *mk_service_find_by_type(uint32_t type) {
    uint32_t slot;

    if (type == MK_SERVICE_NONE) {
        return 0;
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (g_services[slot].type == type) {
            return &g_services[slot];
        }
    }

    return 0;
}

const struct mk_service_record *mk_service_find_by_name(const char *name) {
    uint32_t slot;

    if (name == 0) {
        return 0;
    }

    for (slot = 0; slot < MK_SERVICE_SLOTS; ++slot) {
        if (mk_service_name_equals(g_services[slot].name, name)) {
            return &g_services[slot];
        }
    }

    return 0;
}

static int mk_service_background_retryable(const struct mk_service_record *service) {
    if (service == 0) {
        return 0;
    }
    if (service->type == MK_SERVICE_NONE || service->local_handler == 0) {
        return 0;
    }
    return service->entry != 0 && service->launch_flags != 0u;
}

static void mk_service_retry_reset(struct mk_service_record *service) {
    if (service == 0) {
        return;
    }
    service->retry_attempts = 0u;
    service->retry_next_tick = 0u;
    service->retry_scheduled = 0u;
    service->retry_exhausted = 0u;
}

static int mk_service_schedule_async_recovery(struct mk_service_record *service,
                                              const struct mk_message *request) {
    if (service == 0) {
        return 0;
    }

    mk_service_mark_transport_degraded(service);

    if (service->local_handler != 0 &&
        request != 0 &&
        mk_service_current_allows_local_fallback(service, request)) {
        return 0;
    }

    if (!mk_service_background_retryable(service)) {
        return 0;
    }
    if (service->retry_exhausted != 0u) {
        return 1;
    }
    if (service->retry_scheduled == 0u) {
        mk_service_note_retry_failure(service, kernel_timer_get_ticks());
    } else {
        kernel_waitable_signal(&g_service_retry_waitable, 1u);
    }
    return 1;
}

static uint32_t mk_service_retry_delay_ticks(uint32_t retry_attempts) {
    static const uint8_t fib[MK_SERVICE_BACKGROUND_RETRY_LIMIT] = {
        1u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u
    };
    uint32_t index = retry_attempts;

    if (index >= MK_SERVICE_BACKGROUND_RETRY_LIMIT) {
        index = MK_SERVICE_BACKGROUND_RETRY_LIMIT - 1u;
    }
    return (uint32_t)fib[index] * MK_SERVICE_BACKGROUND_RETRY_BASE_TICKS;
}

static void mk_service_schedule_retry(struct mk_service_record *service, uint32_t now_ticks) {
    uint32_t delay_ticks;

    if (!mk_service_background_retryable(service) || service->retry_exhausted != 0u) {
        return;
    }

    delay_ticks = mk_service_retry_delay_ticks(service->retry_attempts);
    if (delay_ticks == 0u) {
        delay_ticks = 1u;
    }
    service->retry_scheduled = 1u;
    service->retry_next_tick = now_ticks + delay_ticks;
    kernel_debug_printf("service: retry scheduled type=%d attempt=%u/%u in=%u ticks\n",
                        (int)service->type,
                        (unsigned int)(service->retry_attempts + 1u),
                        (unsigned int)MK_SERVICE_BACKGROUND_RETRY_LIMIT,
                        (unsigned int)delay_ticks);
    kernel_waitable_signal(&g_service_retry_waitable, 1u);
}

static void mk_service_note_retry_failure(struct mk_service_record *service, uint32_t now_ticks) {
    if (!mk_service_background_retryable(service)) {
        return;
    }
    if (service->retry_attempts >= MK_SERVICE_BACKGROUND_RETRY_LIMIT) {
        if (service->retry_exhausted == 0u) {
            service->retry_exhausted = 1u;
            service->retry_scheduled = 0u;
            service->retry_next_tick = 0u;
            kernel_text_puts("service retry exhausted\n");
            kernel_debug_printf("service: retry exhausted type=%d attempts=%u\n",
                                (int)service->type,
                                (unsigned int)service->retry_attempts);
        }
        return;
    }
    service->retry_attempts += 1u;
    mk_service_schedule_retry(service, now_ticks);
}

static uint32_t mk_service_process_background_retries(uint32_t now_ticks) {
    uint32_t next_wake_tick = 0u;

    for (uint32_t slot = 0u; slot < MK_SERVICE_SLOTS; ++slot) {
        struct mk_service_record *service = &g_services[slot];

        if (!mk_service_background_retryable(service) || service->retry_exhausted != 0u) {
            continue;
        }

        if (service->process != 0 &&
            service->pid > 0 &&
            !mk_service_process_online(service) &&
            service->retry_scheduled == 0u) {
            mk_service_mark_transport_degraded(service);
            mk_service_schedule_retry(service, now_ticks);
        } else if (service->transport_degraded != 0u &&
                   service->retry_scheduled == 0u &&
                   (service->process == 0 || !mk_service_process_online(service))) {
            mk_service_schedule_retry(service, now_ticks);
        }

        if (service->retry_scheduled != 0u) {
            if ((int32_t)(now_ticks - service->retry_next_tick) >= 0) {
                service->retry_scheduled = 0u;
                service->retry_next_tick = 0u;
                if (service->retry_attempts >= MK_SERVICE_BACKGROUND_RETRY_LIMIT) {
                    service->retry_exhausted = 1u;
                    kernel_text_puts("service retry exhausted\n");
                    kernel_debug_printf("service: retry exhausted type=%d attempts=%u\n",
                                        (int)service->type,
                                        (unsigned int)service->retry_attempts);
                    continue;
                }
                kernel_debug_printf("service: retry firing type=%d attempt=%u/%u\n",
                                    (int)service->type,
                                    (unsigned int)(service->retry_attempts + 1u),
                                    (unsigned int)MK_SERVICE_BACKGROUND_RETRY_LIMIT);
                if (mk_service_restart_worker_record(service) != 0) {
                    continue;
                }
            } else if (next_wake_tick == 0u ||
                       (int32_t)(service->retry_next_tick - next_wake_tick) < 0) {
                next_wake_tick = service->retry_next_tick;
            }
        }
    }

    return next_wake_tick;
}

int mk_service_query_health(uint32_t type, struct mk_service_health_stats *stats) {
    const struct mk_service_record *service;

    if (stats == 0) {
        return -1;
    }
    memset(stats, 0, sizeof(*stats));
    stats->abi_version = 1u;
    stats->service_type = type;

    service = mk_service_find_by_type(type);
    if (service == 0) {
        stats->online = 0u;
        stats->degraded = 0u;
        stats->restart_count = 0u;
        return 0;
    }

    stats->online = mk_service_process_online(service) ? 1u : 0u;
    stats->degraded = service->transport_degraded;
    stats->uptime_ticks = stats->online ? (kernel_timer_get_ticks() - service->start_tick) : 0u;
    stats->last_error = service->last_error;
    stats->pending_requests = service->pending_request_count;
    stats->restart_count = service->restart_count;
    stats->retry_attempts = service->retry_attempts;
    return 0;
}

static void mk_service_retry_worker_entry(void) {
    for (;;) {
        uint32_t now_ticks = kernel_timer_get_ticks();
        uint32_t next_wake_tick = mk_service_process_background_retries(now_ticks);
        uint32_t wait_ticks = 0u;

        if (next_wake_tick != 0u) {
            if ((int32_t)(next_wake_tick - now_ticks) <= 0) {
                continue;
            }
            wait_ticks = next_wake_tick - now_ticks;
        }
        (void)kernel_waitable_wait_timeout(&g_service_retry_waitable, wait_ticks);
    }
}
