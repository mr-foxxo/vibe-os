#ifndef KERNEL_MICROKERNEL_SERVICE_H
#define KERNEL_MICROKERNEL_SERVICE_H

#include <kernel/event.h>
#include <kernel/microkernel/launch.h>
#include <stdint.h>

struct process;
struct mk_message;
struct task_snapshot_entry;

#define MK_SERVICE_NAME_MAX 16u
#define MK_SERVICE_SLOTS 32u
#define MK_SERVICE_EVENT_SUBSCRIBERS 8u
#define MK_SERVICE_EVENT_QUEUE_SIZE 16u

typedef int (*mk_service_local_handler_fn)(const struct mk_message *request,
                                           struct mk_message *reply,
                                           void *context);
typedef void (*mk_service_entry_fn)(void);
typedef int (*mk_service_deferred_launcher_fn)(void);

enum mk_service_type {
    MK_SERVICE_NONE = 0,
    MK_SERVICE_INIT = 1,
    MK_SERVICE_STORAGE = 2,
    MK_SERVICE_FILESYSTEM = 3,
    MK_SERVICE_VIDEO = 4,
    MK_SERVICE_INPUT = 5,
    MK_SERVICE_CONSOLE = 6,
    MK_SERVICE_NETWORK = 7,
    MK_SERVICE_AUDIO = 8
};

struct mk_service_event_subscription {
    int pid;
    struct process *process;
    kernel_mailbox_t mailbox;
    struct mk_service_event events[MK_SERVICE_EVENT_QUEUE_SIZE];
};

struct mk_service_record {
    uint32_t type;
    int pid;
    char name[MK_SERVICE_NAME_MAX];
    struct process *process;
    mk_service_local_handler_fn local_handler;
    void *context;
    mk_service_entry_fn entry;
    uint32_t launch_flags;
    uint32_t stack_size;
    uint32_t restart_count;
    uint32_t transport_degraded;
    uint32_t retry_attempts;
    uint32_t retry_next_tick;
    uint32_t retry_scheduled;
    uint32_t retry_exhausted;
    uint32_t subscriptions_ready;
    uint32_t last_error;
    uint32_t start_tick;
    uint32_t pending_request_count;
    struct mk_service_event_subscription subscribers[MK_SERVICE_EVENT_SUBSCRIBERS];
};

void mk_service_init(void);
int mk_service_register(uint32_t type, const char *name, struct process *process);
int mk_service_register_local(uint32_t type, const char *name);
int mk_service_register_local_handler(uint32_t type,
                                      const char *name,
                                      mk_service_local_handler_fn handler,
                                      void *context);
int mk_service_register_deferred_launcher(uint32_t type,
                                          mk_service_deferred_launcher_fn launcher);
int mk_service_launch_worker(uint32_t type,
                             const char *name,
                             mk_service_local_handler_fn handler,
                             void *context,
                             uint32_t stack_size);
int mk_service_launch_task(uint32_t type,
                           const char *name,
                           mk_service_local_handler_fn handler,
                           void *context,
                           mk_service_entry_fn entry,
                           uint32_t stack_size,
                           uint32_t launch_flags);
int mk_service_prepare_task(uint32_t type,
                            const char *name,
                            mk_service_local_handler_fn handler,
                            void *context,
                            mk_service_entry_fn entry,
                            uint32_t stack_size,
                            uint32_t launch_flags);
int mk_service_is_online(uint32_t type);
int mk_service_ensure(uint32_t type);
int mk_service_restart(uint32_t type);
int mk_service_backend_bridge_allowed_current(uint32_t service_type);
int mk_service_request(uint32_t type, const struct mk_message *request, struct mk_message *reply);
int mk_service_subscribe(uint32_t type, struct process *subscriber);
int mk_service_unsubscribe(uint32_t type, struct process *subscriber);
int mk_service_event_receive(uint32_t type,
                             struct process *subscriber,
                             struct mk_service_event *event,
                             uint32_t timeout_ticks);
void mk_service_fill_task_snapshot(struct task_snapshot_entry *entry);
const struct mk_service_record *mk_service_find_by_type(uint32_t type);
const struct mk_service_record *mk_service_find_by_name(const char *name);
int mk_service_query_health(uint32_t type, struct mk_service_health_stats *stats);

#endif
