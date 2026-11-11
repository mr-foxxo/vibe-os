#include <include/userland_api.h>
#include <kernel/event.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/video/drm/drm.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/microkernel/video.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

#define MK_VIDEO_UPLOAD_CACHE_SLOTS 4u
#define MK_VIDEO_PALETTE_CACHE_SLOTS 4u
#define MK_VIDEO_EVENT_SUBSCRIBERS 8u
#define MK_VIDEO_EVENT_QUEUE_SIZE 128u
#define MK_VIDEO_PRESENT_QUEUE_SIZE 16u
#define MK_VIDEO_CONTROL_QUEUE_SIZE 8u

struct mk_video_upload_cache {
    uint32_t owner_pid;
    uint32_t transfer_id;
    uint32_t capacity;
};

struct mk_video_palette_cache {
    uint32_t owner_pid;
    uint32_t transfer_id;
};

struct mk_video_event_subscription {
    int pid;
    process_t *process;
    kernel_mailbox_t mailbox;
    struct mk_video_event events[MK_VIDEO_EVENT_QUEUE_SIZE];
};

struct mk_video_present_job {
    uint32_t sequence;
    uint32_t present_mode;
};

enum mk_video_control_job_type {
    MK_VIDEO_CONTROL_JOB_NONE = 0,
    MK_VIDEO_CONTROL_JOB_LEAVE = 1,
    MK_VIDEO_CONTROL_JOB_MODE_SET = 2
};

struct mk_video_control_job {
    uint32_t job_type;
    uint32_t width;
    uint32_t height;
    int *result_out;
    kernel_completion_t *completion;
};

static struct mk_video_upload_cache g_video_upload_cache[MK_VIDEO_UPLOAD_CACHE_SLOTS];
static struct mk_video_palette_cache g_video_palette_cache[MK_VIDEO_PALETTE_CACHE_SLOTS];
static struct mk_video_event_subscription g_video_event_subscribers[MK_VIDEO_EVENT_SUBSCRIBERS];
static kernel_mailbox_t g_video_present_mailbox;
static struct mk_video_present_job g_video_present_jobs[MK_VIDEO_PRESENT_QUEUE_SIZE];
static kernel_mailbox_t g_video_control_mailbox;
static struct mk_video_control_job g_video_control_jobs[MK_VIDEO_CONTROL_QUEUE_SIZE];
static uint32_t g_video_event_sequence = 0u;
static uint32_t g_video_present_next_sequence = 0u;
static uint32_t g_video_present_last_submitted_sequence = 0u;
static uint32_t g_video_present_last_completed_sequence = 0u;
static uint32_t g_video_present_pending_depth = 0u;
static int g_video_present_worker_pid = 0;
static int g_video_control_worker_pid = 0;
static int g_video_backend_faulted = 0;

static int mk_video_current_process_is_service_worker(void);
static int mk_video_current_prefers_kernel_hot_path(void);
static void mk_video_event_init_subscribers(void);
static uint32_t mk_video_next_event_sequence(void);
static uint32_t mk_video_publish_event_with_sequence(uint32_t event_type,
                                                     uint32_t present_mode,
                                                     uint32_t sequence);
static uint32_t mk_video_publish_event(uint32_t event_type, uint32_t present_mode);
static struct mk_video_event_subscription *mk_video_find_subscription(const process_t *subscriber);
static struct mk_video_event_subscription *mk_video_alloc_subscription(process_t *subscriber);
static uint32_t mk_video_subscription_pending_drops(struct mk_video_event_subscription *subscription);
static void mk_video_enqueue_event(struct mk_video_event_subscription *subscription,
                                   uint32_t event_type,
                                   uint32_t present_mode,
                                   uint32_t sequence);
static void mk_video_present_worker_entry(void);
static int mk_video_present_worker_online(void);
static int mk_video_ensure_present_worker(void);
static int mk_video_present_prefers_inline_submit(void);
static int mk_video_submit_present_job(uint32_t present_mode, uint32_t *sequence_out);
static uint32_t mk_video_complete_present_job(uint32_t present_mode, uint32_t sequence);
static void mk_video_mark_backend_failed(void);
static void mk_video_mark_backend_recovered(void);
static void mk_video_control_worker_entry(void);
static int mk_video_control_worker_online(void);
static int mk_video_ensure_control_worker(void);
static int mk_video_execute_control_job(uint32_t job_type, uint32_t width, uint32_t height);
static int mk_video_packed_blit8_byte_count(int src_w, int src_h, uint32_t *byte_count_out);
static int mk_video_pack_blit8_source_to_buffer(const uint8_t *src, int src_w, int src_h,
                                                uint8_t *dst, uint32_t byte_count);
static int mk_video_upload_packed_blit8_source(uint32_t transfer_id, const uint8_t *src,
                                               int src_w, int src_h, uint32_t byte_count);

static int mk_video_packed_blit8_byte_count(int src_w, int src_h, uint32_t *byte_count_out) {
    size_t packed_bytes;

    if (byte_count_out == 0 || src_w <= 0 || src_h <= 0) {
        return -1;
    }

    packed_bytes = (size_t)src_w * (size_t)src_h;
    if (packed_bytes == 0u || packed_bytes > UINT32_MAX) {
        return -1;
    }

    *byte_count_out = (uint32_t)packed_bytes;
    return 0;
}

static int mk_video_pack_blit8_source_to_buffer(const uint8_t *src, int src_w, int src_h,
                                                uint8_t *dst, uint32_t byte_count) {
    return kernel_video_pack_blit8_source(src, src_w, src_h, dst, (size_t)byte_count);
}

static int mk_video_upload_packed_blit8_source(uint32_t transfer_id, const uint8_t *src,
                                               int src_w, int src_h, uint32_t byte_count) {
    uint8_t *dst;

    if (transfer_id == 0u || byte_count == 0u) {
        return -1;
    }

    dst = (uint8_t *)mk_transfer_data_write(transfer_id);
    if (dst == 0) {
        return -1;
    }
    if (mk_transfer_size(transfer_id) < byte_count) {
        mk_transfer_release(transfer_id);
        return -1;
    }

    {
        int rc = mk_video_pack_blit8_source_to_buffer(src, src_w, src_h, dst, byte_count);
        mk_transfer_release(transfer_id);
        return rc;
    }
}

static void mk_video_event_init_subscribers(void) {
    uint32_t index;

    g_video_event_sequence = 0u;
    g_video_present_next_sequence = 0u;
    g_video_present_last_submitted_sequence = 0u;
    g_video_present_last_completed_sequence = 0u;
    g_video_present_pending_depth = 0u;
    g_video_present_worker_pid = 0;
    g_video_control_worker_pid = 0;
    g_video_backend_faulted = 0;
    kernel_mailbox_init(&g_video_present_mailbox,
                        g_video_present_jobs,
                        sizeof(g_video_present_jobs[0]),
                        MK_VIDEO_PRESENT_QUEUE_SIZE,
                        KERNEL_MAILBOX_DROP_NEWEST,
                        TASK_WAIT_CLASS_VIDEO,
                        MK_SERVICE_VIDEO);
    kernel_mailbox_init(&g_video_control_mailbox,
                        g_video_control_jobs,
                        sizeof(g_video_control_jobs[0]),
                        MK_VIDEO_CONTROL_QUEUE_SIZE,
                        KERNEL_MAILBOX_DROP_NEWEST,
                        TASK_WAIT_CLASS_VIDEO,
                        MK_SERVICE_VIDEO);
    for (index = 0; index < MK_VIDEO_EVENT_SUBSCRIBERS; ++index) {
        struct mk_video_event_subscription *subscription = &g_video_event_subscribers[index];

        memset(subscription, 0, sizeof(*subscription));
        kernel_mailbox_init(&subscription->mailbox,
                            subscription->events,
                            sizeof(subscription->events[0]),
                            MK_VIDEO_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_OLDEST,
                            TASK_WAIT_CLASS_VIDEO,
                            MK_SERVICE_VIDEO);
    }
}

static struct mk_video_event_subscription *mk_video_find_subscription(const process_t *subscriber) {
    uint32_t index;

    if (subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0; index < MK_VIDEO_EVENT_SUBSCRIBERS; ++index) {
        struct mk_video_event_subscription *subscription = &g_video_event_subscribers[index];

        if (subscription->pid == subscriber->pid && subscription->process == subscriber) {
            return subscription;
        }
    }
    return 0;
}

static struct mk_video_event_subscription *mk_video_alloc_subscription(process_t *subscriber) {
    uint32_t index;

    if (subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0; index < MK_VIDEO_EVENT_SUBSCRIBERS; ++index) {
        struct mk_video_event_subscription *subscription = &g_video_event_subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0 ||
            scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            subscription->pid = subscriber->pid;
            subscription->process = subscriber;
            kernel_mailbox_init(&subscription->mailbox,
                                subscription->events,
                                sizeof(subscription->events[0]),
                                MK_VIDEO_EVENT_QUEUE_SIZE,
                                KERNEL_MAILBOX_DROP_OLDEST,
                                TASK_WAIT_CLASS_VIDEO,
                                MK_SERVICE_VIDEO);
            return subscription;
        }
    }
    return 0;
}

static uint32_t mk_video_subscription_pending_drops(struct mk_video_event_subscription *subscription) {
    uint32_t dropped_events;

    if (subscription == 0) {
        return 0u;
    }

    dropped_events = kernel_mailbox_dropped(&subscription->mailbox);
    if (kernel_mailbox_count(&subscription->mailbox) >= MK_VIDEO_EVENT_QUEUE_SIZE) {
        dropped_events += 1u;
    }
    return dropped_events;
}

static void mk_video_enqueue_event(struct mk_video_event_subscription *subscription,
                                   uint32_t event_type,
                                   uint32_t present_mode,
                                   uint32_t sequence) {
    struct mk_video_event event;
    struct video_mode *mode;

    if (subscription == 0 || event_type == MK_VIDEO_EVENT_NONE) {
        return;
    }

    memset(&event, 0, sizeof(event));
    mode = kernel_video_get_mode();
    event.abi_version = 1u;
    event.event_type = event_type;
    event.present_mode = present_mode;
    event.sequence = sequence;
    event.completed_sequence = g_video_present_last_completed_sequence;
    event.pending_depth = g_video_present_pending_depth;
    if (mode != 0) {
        event.active_width = mode->width;
        event.active_height = mode->height;
    }
    event.dropped_events = mk_video_subscription_pending_drops(subscription);
    event.tick = kernel_timer_get_ticks();
    if (kernel_mailbox_try_send(&subscription->mailbox, &event) == 0) {
        kernel_mailbox_clear_dropped(&subscription->mailbox);
    }
}

static uint32_t mk_video_next_event_sequence(void) {
    g_video_event_sequence += 1u;
    return g_video_event_sequence;
}

static uint32_t mk_video_publish_event_with_sequence(uint32_t event_type,
                                                     uint32_t present_mode,
                                                     uint32_t sequence) {
    uint32_t index;

    if (event_type == MK_VIDEO_EVENT_NONE) {
        return 0u;
    }
    if (sequence == 0u) {
        sequence = mk_video_next_event_sequence();
    }

    for (index = 0; index < MK_VIDEO_EVENT_SUBSCRIBERS; ++index) {
        struct mk_video_event_subscription *subscription = &g_video_event_subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0) {
            continue;
        }
        if (scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            continue;
        }
        mk_video_enqueue_event(subscription, event_type, present_mode, sequence);
    }
    return sequence;
}

static uint32_t mk_video_publish_event(uint32_t event_type, uint32_t present_mode) {
    return mk_video_publish_event_with_sequence(event_type, present_mode, 0u);
}

static int mk_video_present_worker_online(void) {
    if (g_video_present_worker_pid <= 0) {
        return 0;
    }
    if (scheduler_find_task_by_pid(g_video_present_worker_pid) == 0) {
        mk_launch_release_pid(g_video_present_worker_pid);
        g_video_present_worker_pid = 0;
        return 0;
    }
    return 1;
}

static int mk_video_ensure_present_worker(void) {
    struct mk_launch_descriptor descriptor;
    int pid;

    if (mk_video_present_worker_online()) {
        return 0;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_DRIVER;
    descriptor.flags = MK_LAUNCH_FLAG_BOOTSTRAP |
                       MK_LAUNCH_FLAG_BUILTIN |
                       MK_LAUNCH_FLAG_CRITICAL;
    descriptor.task_class = MK_TASK_CLASS_VIDEO_PRESENT;
    descriptor.stack_size = 32768u;
    strncpy(descriptor.name, "video-present", MK_LAUNCH_NAME_MAX - 1u);
    descriptor.name[MK_LAUNCH_NAME_MAX - 1u] = '\0';
    descriptor.entry = mk_video_present_worker_entry;
    pid = mk_launch_bootstrap(&descriptor);
    if (pid <= 0) {
        return -1;
    }
    g_video_present_worker_pid = pid;
    return 0;
}

static int mk_video_present_prefers_inline_submit(void) {
    enum kernel_drm_backend_kind active_kind = kernel_drm_active_backend_kind();
    enum kernel_drm_backend_kind detected_kind = kernel_drm_detected_backend_kind();
    enum kernel_drm_backend_kind effective_kind =
        active_kind != KERNEL_DRM_BACKEND_NONE ? active_kind : detected_kind;

    return effective_kind == KERNEL_DRM_BACKEND_BGA ||
           effective_kind == KERNEL_DRM_BACKEND_NONE ||
           effective_kind == KERNEL_DRM_BACKEND_UNKNOWN;
}

static int mk_video_submit_present_job(uint32_t present_mode, uint32_t *sequence_out) {
    struct mk_video_present_job job;
    uint32_t sequence;

    if (!mk_video_present_prefers_inline_submit() &&
        mk_video_ensure_present_worker() != 0) {
        return -1;
    }

    sequence = ++g_video_present_next_sequence;
    if (mk_video_present_prefers_inline_submit()) {
        g_video_present_pending_depth += 1u;
        g_video_present_last_submitted_sequence = sequence;
        (void)mk_video_publish_event_with_sequence(MK_VIDEO_EVENT_PRESENT_SUBMITTED,
                                                   present_mode,
                                                   sequence);
        if (sequence_out != 0) {
            *sequence_out = sequence;
        }
        kernel_video_flip_mode(present_mode);
        (void)mk_video_complete_present_job(present_mode, sequence);
        return 0;
    }

    job.sequence = sequence;
    job.present_mode = present_mode;
    if (kernel_mailbox_try_send(&g_video_present_mailbox, &job) != 0) {
        (void)mk_video_publish_event(MK_VIDEO_EVENT_OVERFLOW, present_mode);
        return -1;
    }

    g_video_present_pending_depth += 1u;
    g_video_present_last_submitted_sequence = sequence;
    (void)mk_video_publish_event_with_sequence(MK_VIDEO_EVENT_PRESENT_SUBMITTED,
                                               present_mode,
                                               sequence);
    if (sequence_out != 0) {
        *sequence_out = sequence;
    }
    return 0;
}

static uint32_t mk_video_complete_present_job(uint32_t present_mode, uint32_t sequence) {
    if (g_video_present_pending_depth != 0u) {
        g_video_present_pending_depth -= 1u;
    }
    g_video_present_last_completed_sequence = sequence;
    return mk_video_publish_event_with_sequence(MK_VIDEO_EVENT_PRESENT,
                                                present_mode,
                                                sequence);
}

static void mk_video_mark_backend_failed(void) {
    if (g_video_backend_faulted) {
        return;
    }

    g_video_backend_faulted = 1;
    (void)mk_video_publish_event(MK_VIDEO_EVENT_BACKEND_FAILED, VIDEO_PRESENT_AUTO);
}

static void mk_video_mark_backend_recovered(void) {
    if (!g_video_backend_faulted) {
        return;
    }

    g_video_backend_faulted = 0;
    (void)mk_video_publish_event(MK_VIDEO_EVENT_BACKEND_RECOVERED, VIDEO_PRESENT_AUTO);
}

static void mk_video_present_worker_entry(void) {
    struct mk_video_present_job job;

    for (;;) {
        while (kernel_mailbox_try_receive(&g_video_present_mailbox, &job) == 0) {
            kernel_video_flip_mode(job.present_mode);
            (void)mk_video_complete_present_job(job.present_mode, job.sequence);
        }
        if (kernel_mailbox_wait(&g_video_present_mailbox, 0u) != TASK_WAIT_RESULT_SIGNALED) {
            yield();
        }
    }
}

static int mk_video_control_worker_online(void) {
    if (g_video_control_worker_pid <= 0) {
        return 0;
    }
    if (scheduler_find_task_by_pid(g_video_control_worker_pid) == 0) {
        mk_launch_release_pid(g_video_control_worker_pid);
        g_video_control_worker_pid = 0;
        return 0;
    }
    return 1;
}

static int mk_video_ensure_control_worker(void) {
    struct mk_launch_descriptor descriptor;
    int pid;

    if (mk_video_control_worker_online()) {
        return 0;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_DRIVER;
    descriptor.flags = MK_LAUNCH_FLAG_BOOTSTRAP |
                       MK_LAUNCH_FLAG_BUILTIN |
                       MK_LAUNCH_FLAG_CRITICAL;
    descriptor.task_class = MK_TASK_CLASS_VIDEO_CONTROL;
    descriptor.stack_size = 32768u;
    strncpy(descriptor.name, "video-control", MK_LAUNCH_NAME_MAX - 1u);
    descriptor.name[MK_LAUNCH_NAME_MAX - 1u] = '\0';
    descriptor.entry = mk_video_control_worker_entry;
    pid = mk_launch_bootstrap(&descriptor);
    if (pid <= 0) {
        return -1;
    }
    g_video_control_worker_pid = pid;
    return 0;
}

static void mk_video_control_complete_job(const struct mk_video_control_job *job, int rc) {
    if (job == 0) {
        return;
    }
    if (job->result_out != 0) {
        *job->result_out = rc;
    }
    if (job->completion != 0) {
        kernel_completion_complete(job->completion);
    }
}

static int mk_video_execute_control_job_inline(uint32_t job_type, uint32_t width, uint32_t height) {
    int rc;

    switch (job_type) {
    case MK_VIDEO_CONTROL_JOB_LEAVE:
        kernel_video_leave_graphics();
        (void)mk_video_publish_event(MK_VIDEO_EVENT_LEAVE, VIDEO_PRESENT_AUTO);
        return 0;
    case MK_VIDEO_CONTROL_JOB_MODE_SET:
        (void)mk_video_publish_event(MK_VIDEO_EVENT_MODE_SET_BEGIN, VIDEO_PRESENT_AUTO);
        rc = kernel_video_set_mode(width, height);
        if (rc == 0) {
            mk_video_mark_backend_recovered();
            (void)mk_video_publish_event(MK_VIDEO_EVENT_MODE_SET_DONE, VIDEO_PRESENT_AUTO);
            (void)mk_video_publish_event(MK_VIDEO_EVENT_MODE_SET, VIDEO_PRESENT_AUTO);
        } else {
            mk_video_mark_backend_failed();
        }
        return rc;
    default:
        return -1;
    }
}

static void mk_video_control_worker_entry(void) {
    struct mk_video_control_job job;

    for (;;) {
        while (kernel_mailbox_try_receive(&g_video_control_mailbox, &job) == 0) {
            mk_video_control_complete_job(&job,
                                          mk_video_execute_control_job_inline(job.job_type,
                                                                              job.width,
                                                                              job.height));
        }
        if (kernel_mailbox_wait(&g_video_control_mailbox, 0u) != TASK_WAIT_RESULT_SIGNALED) {
            yield();
        }
    }
}

static int mk_video_execute_control_job(uint32_t job_type, uint32_t width, uint32_t height) {
    struct mk_video_control_job job;
    kernel_completion_t completion;
    int result = -1;
    int wait_rc;

    if ((int)scheduler_current_pid() == g_video_control_worker_pid ||
        mk_video_current_process_is_service_worker()) {
        return mk_video_execute_control_job_inline(job_type, width, height);
    }

    if (mk_video_ensure_control_worker() != 0) {
        return -1;
    }

    memset(&job, 0, sizeof(job));
    kernel_completion_init(&completion, TASK_WAIT_CLASS_VIDEO, MK_SERVICE_VIDEO);
    job.job_type = job_type;
    job.width = width;
    job.height = height;
    job.result_out = &result;
    job.completion = &completion;
    if (kernel_mailbox_try_send(&g_video_control_mailbox, &job) != 0) {
        return -1;
    }

    wait_rc = kernel_completion_wait(&completion, 0u);
    if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
        return -1;
    }
    return result;
}

static uint32_t mk_video_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_video_current_process_is_service_worker(void) {
    process_t *current = scheduler_current();

    return current != 0 && current->service_type == MK_SERVICE_VIDEO;
}

static int mk_video_current_prefers_kernel_hot_path(void) {
    process_t *current = scheduler_current();
    const struct mk_launch_context *context;

    if (current == 0) {
        return 0;
    }
    if (current->service_type == MK_SERVICE_VIDEO) {
        return 1;
    }

    context = mk_launch_context_current();
    if (context == 0) {
        return 1;
    }

    if ((context->flags & (MK_LAUNCH_FLAG_BOOTSTRAP |
                           MK_LAUNCH_FLAG_CRITICAL |
                           MK_LAUNCH_FLAG_USER_DESKTOP)) != 0u) {
        return 1;
    }

    return context->task_class == MK_TASK_CLASS_DESKTOP ||
           context->task_class == MK_TASK_CLASS_APP_RUNTIME;
}

static int mk_video_share_transfer(uint32_t transfer_id, uint32_t permissions) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_VIDEO);

    if (service == 0 || service->pid <= 0) {
        return 0;
    }
    return mk_transfer_share(transfer_id, (uint32_t)service->pid, permissions);
}

static struct mk_video_upload_cache *mk_video_find_upload_cache(uint32_t owner_pid) {
    uint32_t i;

    for (i = 0; i < MK_VIDEO_UPLOAD_CACHE_SLOTS; ++i) {
        if (g_video_upload_cache[i].transfer_id != 0u &&
            g_video_upload_cache[i].owner_pid == owner_pid) {
            return &g_video_upload_cache[i];
        }
    }
    for (i = 0; i < MK_VIDEO_UPLOAD_CACHE_SLOTS; ++i) {
        if (g_video_upload_cache[i].transfer_id == 0u) {
            return &g_video_upload_cache[i];
        }
    }
    return 0;
}

static struct mk_video_palette_cache *mk_video_find_palette_cache(uint32_t owner_pid) {
    uint32_t i;

    for (i = 0; i < MK_VIDEO_PALETTE_CACHE_SLOTS; ++i) {
        if (g_video_palette_cache[i].transfer_id != 0u &&
            g_video_palette_cache[i].owner_pid == owner_pid) {
            return &g_video_palette_cache[i];
        }
    }
    for (i = 0; i < MK_VIDEO_PALETTE_CACHE_SLOTS; ++i) {
        if (g_video_palette_cache[i].transfer_id == 0u) {
            return &g_video_palette_cache[i];
        }
    }
    return 0;
}

static int mk_video_get_upload_transfer(uint32_t byte_count, uint32_t *transfer_id_out) {
    struct mk_video_upload_cache *cache;
    uint32_t owner_pid;

    if (transfer_id_out == 0 || byte_count == 0u) {
        return -1;
    }

    owner_pid = mk_video_current_pid();
    cache = mk_video_find_upload_cache(owner_pid);
    if (cache == 0) {
        return -1;
    }

    if (cache->transfer_id != 0u && cache->owner_pid == owner_pid && cache->capacity >= byte_count) {
        *transfer_id_out = cache->transfer_id;
        return 0;
    }

    if (cache->transfer_id != 0u && cache->owner_pid == owner_pid) {
        if (mk_transfer_destroy(cache->transfer_id) != 0) {
            return -1;
        }
        cache->transfer_id = 0u;
        cache->capacity = 0u;
    }

    if (cache->transfer_id == 0u) {
        if (mk_transfer_create(owner_pid, byte_count, &cache->transfer_id) != 0) {
            return -1;
        }
        cache->owner_pid = owner_pid;
        cache->capacity = byte_count;
    }

    *transfer_id_out = cache->transfer_id;
    return 0;
}

static int mk_video_get_palette_transfer(uint32_t *transfer_id_out) {
    struct mk_video_palette_cache *cache;
    uint32_t owner_pid;

    if (transfer_id_out == 0) {
        return -1;
    }

    owner_pid = mk_video_current_pid();
    cache = mk_video_find_palette_cache(owner_pid);
    if (cache == 0) {
        return -1;
    }

    if (cache->transfer_id != 0u) {
        if (cache->owner_pid == owner_pid) {
            *transfer_id_out = cache->transfer_id;
            return 0;
        }
        if (mk_transfer_destroy(cache->transfer_id) != 0) {
            return -1;
        }
        cache->transfer_id = 0u;
    }

    if (cache->transfer_id == 0u) {
        if (mk_transfer_create(owner_pid, MK_VIDEO_PALETTE_BYTES, &cache->transfer_id) != 0) {
            return -1;
        }
        cache->owner_pid = owner_pid;
    }

    *transfer_id_out = cache->transfer_id;
    return 0;
}

static int mk_video_prepare_request(struct mk_message *message,
                                    uint32_t type,
                                    const void *payload,
                                    size_t payload_size) {
    const struct mk_service_record *service;
    process_t *current;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_VIDEO);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    current = scheduler_current();
    message->source_pid = current != 0 ? (uint32_t)current->pid : 0u;
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_video_reply_result(struct mk_message *reply, int value) {
    struct mk_video_result result;

    result.value = value;
    return mk_message_set_payload(reply, &result, sizeof(result));
}

static int mk_video_reply_present(struct mk_message *reply, int value, uint32_t sequence) {
    struct mk_video_present_reply payload;

    payload.value = value;
    payload.sequence = sequence;
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_video_decode_result(const struct mk_message *reply) {
    struct mk_video_result result;

    if (reply == 0 || reply->payload_size != sizeof(result)) {
        return -1;
    }

    memcpy(&result, reply->payload, sizeof(result));
    return result.value;
}

static int mk_video_decode_present(const struct mk_message *reply, uint32_t *sequence_out) {
    struct mk_video_present_reply payload;

    if (reply == 0 || reply->payload_size != sizeof(payload)) {
        return -1;
    }
    memcpy(&payload, reply->payload, sizeof(payload));
    if (sequence_out != 0) {
        *sequence_out = payload.sequence;
    }
    return payload.value;
}

static int mk_video_validate_text_transfer(uint32_t transfer_id,
                                           uint32_t text_length,
                                           char **text_out) {
    char *text;

    if (text_out == 0 || text_length == 0u) {
        return -1;
    }

    text = (char *)mk_transfer_data_read(transfer_id);
    if (text == 0) {
        return -1;
    }
    /* data_read pinned the transfer; release on failure, caller owns on success. */
    if (mk_transfer_size(transfer_id) < text_length + 1u) {
        mk_transfer_release(transfer_id);
        return -1;
    }
    if (text[text_length] != '\0') {
        mk_transfer_release(transfer_id);
        return -1;
    }

    *text_out = text;
    return 0;
}

static int mk_video_reply_mode(struct mk_message *reply,
                               int value,
                               const struct video_mode *mode) {
    struct mk_video_mode_reply payload;

    memset(&payload, 0, sizeof(payload));
    payload.value = value;
    if (mode != 0) {
        payload.mode = *mode;
    }
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_video_decode_mode(const struct mk_message *reply, struct video_mode *mode) {
    struct mk_video_mode_reply payload;

    if (reply == 0 || mode == 0 || reply->payload_size != sizeof(payload)) {
        return -1;
    }
    memcpy(&payload, reply->payload, sizeof(payload));
    *mode = payload.mode;
    return payload.value;
}

static int mk_video_reply_caps(struct mk_message *reply,
                               int value,
                               const struct video_capabilities *caps) {
    struct mk_video_caps_reply payload;

    memset(&payload, 0, sizeof(payload));
    payload.value = value;
    if (caps != 0) {
        payload.caps = *caps;
    }
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_video_decode_caps(const struct mk_message *reply, struct video_capabilities *caps) {
    struct mk_video_caps_reply payload;

    if (reply == 0 || caps == 0 || reply->payload_size != sizeof(payload)) {
        return -1;
    }
    memcpy(&payload, reply->payload, sizeof(payload));
    *caps = payload.caps;
    return payload.value;
}

static int mk_video_local_handler(const struct mk_message *request,
                                  struct mk_message *reply,
                                  void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_VIDEO_CLEAR: {
        const struct mk_video_color_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_color_request *)request->payload;
        kernel_video_clear((uint8_t)(payload->color & 0xFFu));
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_RECT: {
        const struct mk_video_rect_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_rect_request *)request->payload;
        kernel_gfx_rect(payload->x, payload->y, payload->width, payload->height,
                        (uint8_t)(payload->color & 0xFFu));
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_TEXT: {
        const struct mk_video_text_request *payload;
        char *text;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_text_request *)request->payload;
        if (mk_video_validate_text_transfer(payload->transfer_id, payload->text_length, &text) != 0) {
            return -1;
        }
        kernel_gfx_draw_text(payload->x, payload->y, text, (uint8_t)(payload->color & 0xFFu));
        mk_transfer_release(payload->transfer_id);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_TEXT_INLINE: {
        const struct mk_video_text_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_text_inline_request *)request->payload;
        if (payload->text_length == 0u ||
            payload->text_length >= MK_VIDEO_INLINE_TEXT_MAX ||
            payload->text[payload->text_length] != '\0') {
            return -1;
        }
        kernel_gfx_draw_text(payload->x,
                             payload->y,
                             payload->text,
                             (uint8_t)(payload->color & 0xFFu));
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_FLIP:
        if (request->payload_size == 0u) {
            return mk_video_reply_result(reply,
                                         mk_video_submit_present_job(VIDEO_PRESENT_AUTO, 0));
        }
        if (request->payload_size == sizeof(struct mk_video_present_request)) {
            const struct mk_video_present_request *payload =
                (const struct mk_video_present_request *)request->payload;
            return mk_video_reply_result(reply,
                                         mk_video_submit_present_job(payload->mode, 0));
        }
        return -1;
    case MK_MSG_VIDEO_PRESENT_SUBMIT: {
        const struct mk_video_present_request *payload;
        uint32_t sequence;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_present_request *)request->payload;
        rc = mk_video_submit_present_job(payload->mode, &sequence);
        return mk_video_reply_present(reply, rc, sequence);
    }
    case MK_MSG_VIDEO_SET_PRESENT_POLICY: {
        const struct mk_video_u32_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_u32_request *)request->payload;
        kernel_video_set_present_policy(payload->value);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_SET_PRESENT_COPY_OVERRIDE: {
        const struct mk_video_u32_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_u32_request *)request->payload;
        kernel_video_set_present_copy_override(payload->value);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_LEAVE:
        if (request->payload_size != 0u) {
            return -1;
        }
        return mk_video_reply_result(reply,
                                     mk_video_execute_control_job(MK_VIDEO_CONTROL_JOB_LEAVE,
                                                                  0u,
                                                                  0u));
    case MK_MSG_VIDEO_BLIT8: {
        const struct mk_video_blit8_request *payload;
        const uint8_t *src;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_request *)request->payload;
        src = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (src == 0) {
            return -1;
        }
        if (mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            mk_transfer_release(payload->transfer_id);
            return -1;
        }
        kernel_gfx_blit8(src,
                         payload->src_width,
                         payload->src_height,
                         payload->dst_x,
                         payload->dst_y,
                         payload->scale);
        mk_transfer_release(payload->transfer_id);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_PRESENT: {
        const struct mk_video_blit8_request *payload;
        const uint8_t *src;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_request *)request->payload;
        src = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (src == 0) {
            return -1;
        }
        if (mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            mk_transfer_release(payload->transfer_id);
            return -1;
        }
        kernel_gfx_blit8(src,
                         payload->src_width,
                         payload->src_height,
                         payload->dst_x,
                         payload->dst_y,
                         payload->scale);
        mk_transfer_release(payload->transfer_id);
        return mk_video_reply_result(reply,
                                     mk_video_submit_present_job(VIDEO_PRESENT_FULL, 0));
    }
    case MK_MSG_VIDEO_BLIT8_INLINE: {
        const struct mk_video_blit8_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->scale <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        kernel_gfx_blit8(payload->pixels,
                         payload->src_width,
                         payload->src_height,
                         payload->dst_x,
                         payload->dst_y,
                         payload->scale);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_PRESENT_INLINE: {
        const struct mk_video_blit8_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->scale <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        kernel_gfx_blit8(payload->pixels,
                         payload->src_width,
                         payload->src_height,
                         payload->dst_x,
                         payload->dst_y,
                         payload->scale);
        return mk_video_reply_result(reply,
                                     mk_video_submit_present_job(VIDEO_PRESENT_FULL, 0));
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT: {
        const struct mk_video_blit8_stretch_present_request *payload;
        const uint8_t *src;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_present_request *)request->payload;
        src = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (src == 0) {
            return -1;
        }
        if (mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            mk_transfer_release(payload->transfer_id);
            return -1;
        }
        kernel_gfx_blit8_stretch(src,
                                 payload->src_width,
                                 payload->src_height,
                                 payload->dst_x,
                                 payload->dst_y,
                                 payload->dst_width,
                                 payload->dst_height);
        mk_transfer_release(payload->transfer_id);
        return mk_video_reply_result(reply,
                                     mk_video_submit_present_job(VIDEO_PRESENT_FULL, 0));
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH: {
        const struct mk_video_blit8_stretch_request *payload;
        const uint8_t *src;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_request *)request->payload;
        src = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (src == 0) {
            return -1;
        }
        if (mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            mk_transfer_release(payload->transfer_id);
            return -1;
        }
        kernel_gfx_blit8_stretch(src,
                                 payload->src_width,
                                 payload->src_height,
                                 payload->dst_x,
                                 payload->dst_y,
                                 payload->dst_width,
                                 payload->dst_height);
        mk_transfer_release(payload->transfer_id);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_INLINE: {
        const struct mk_video_blit8_stretch_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->dst_width <= 0 ||
            payload->dst_height <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        kernel_gfx_blit8_stretch(payload->pixels,
                                 payload->src_width,
                                 payload->src_height,
                                 payload->dst_x,
                                 payload->dst_y,
                                 payload->dst_width,
                                 payload->dst_height);
        return mk_video_reply_result(reply, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT_INLINE: {
        const struct mk_video_blit8_stretch_present_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_present_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->dst_width <= 0 ||
            payload->dst_height <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        kernel_gfx_blit8_stretch(payload->pixels,
                                 payload->src_width,
                                 payload->src_height,
                                 payload->dst_x,
                                 payload->dst_y,
                                 payload->dst_width,
                                 payload->dst_height);
        return mk_video_reply_result(reply,
                                     mk_video_submit_present_job(VIDEO_PRESENT_FULL, 0));
    }
    case MK_MSG_VIDEO_MODE_SET: {
        const struct mk_video_mode_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_mode_request *)request->payload;
        return mk_video_reply_result(reply,
                                     mk_video_execute_control_job(MK_VIDEO_CONTROL_JOB_MODE_SET,
                                                                  payload->width,
                                                                  payload->height));
    }
    case MK_MSG_VIDEO_SET_PALETTE: {
        const struct mk_video_palette_request *payload;
        const uint8_t *palette;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_palette_request *)request->payload;
        palette = (const uint8_t *)mk_transfer_data_read(payload->transfer_id);
        if (palette == 0) {
            return -1;
        }
        if (payload->byte_count != MK_VIDEO_PALETTE_BYTES ||
            mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            mk_transfer_release(payload->transfer_id);
            return -1;
        }
        {
            int rc = kernel_video_set_palette(palette);
            mk_transfer_release(payload->transfer_id);
            return mk_video_reply_result(reply, rc);
        }
    }
    case MK_MSG_VIDEO_GET_PALETTE: {
        const struct mk_video_palette_request *payload;
        uint8_t *palette;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_palette_request *)request->payload;
        palette = (uint8_t *)mk_transfer_data_write(payload->transfer_id);
        if (palette == 0) {
            return -1;
        }
        if (payload->byte_count != MK_VIDEO_PALETTE_BYTES ||
            mk_transfer_size(payload->transfer_id) < payload->byte_count) {
            mk_transfer_release(payload->transfer_id);
            return -1;
        }
        rc = kernel_video_get_palette(palette);
        mk_transfer_release(payload->transfer_id);
        return mk_video_reply_result(reply, rc);
    }
    case MK_MSG_VIDEO_GET_INFO: {
        struct video_mode *mode;

        if (request->payload_size != 0u) {
            return -1;
        }
        mode = kernel_video_get_mode();
        return mk_video_reply_mode(reply, 0, mode);
    }
    case MK_MSG_VIDEO_GET_CAPS: {
        struct video_capabilities caps;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&caps, 0, sizeof(caps));
        kernel_video_get_capabilities(&caps);
        return mk_video_reply_caps(reply, 0, &caps);
    }
    default:
        return -1;
    }
}

void mk_video_service_init(void) {
    mk_video_event_init_subscribers();
    (void)mk_video_ensure_control_worker();
    (void)mk_video_ensure_present_worker();
    (void)mk_service_launch_task(MK_SERVICE_VIDEO,
                                 "video",
                                 mk_video_local_handler,
                                 0,
                                 userland_video_service_entry,
                                 32768u,
                                 MK_LAUNCH_FLAG_BOOTSTRAP |
                                 MK_LAUNCH_FLAG_BUILTIN |
                                 MK_LAUNCH_FLAG_CRITICAL);
}

int mk_video_service_clear(uint8_t color) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_color_request payload;

    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        kernel_video_clear(color);
        return 0;
    }

    payload.color = color;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_CLEAR, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_rect(int x, int y, int w, int h, uint8_t color) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_rect_request payload;

    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        kernel_gfx_rect(x, y, w, h, color);
        return 0;
    }

    payload.x = x;
    payload.y = y;
    payload.width = w;
    payload.height = h;
    payload.color = color;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_RECT, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_text(int x, int y, uint8_t color, const char *text) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_text_request payload;
    struct mk_video_text_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t text_length;

    if (text == 0) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        kernel_gfx_draw_text(x, y, text, color);
        return 0;
    }
    text_length = (uint32_t)strlen(text);
    if (text_length != 0u && text_length < MK_VIDEO_INLINE_TEXT_MAX) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.x = x;
        inline_payload.y = y;
        inline_payload.color = color;
        inline_payload.text_length = text_length;
        memcpy(inline_payload.text, text, text_length + 1u);
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_TEXT_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }

    if (mk_video_get_upload_transfer(text_length + 1u, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, text, text_length + 1u) != 0) {
        return -1;
    }

    payload.x = x;
    payload.y = y;
    payload.color = color;
    payload.text_length = text_length;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_TEXT, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_flip(void) {
    return mk_video_service_flip_mode(VIDEO_PRESENT_AUTO);
}

int mk_video_service_flip_mode(uint32_t mode) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_present_request payload;

    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        return mk_video_submit_present_job(mode, 0);
    }

    payload.mode = mode;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_FLIP, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_present_submit(uint32_t mode, uint32_t *sequence_out) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_present_request payload;

    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        return mk_video_submit_present_job(mode, sequence_out);
    }

    payload.mode = mode;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_PRESENT_SUBMIT, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_present(&reply, sequence_out);
}

static int mk_video_service_u32_request(uint32_t type, uint32_t value) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_u32_request payload;

    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        if (type == MK_MSG_VIDEO_SET_PRESENT_POLICY) {
            kernel_video_set_present_policy(value);
            return 0;
        }
        if (type == MK_MSG_VIDEO_SET_PRESENT_COPY_OVERRIDE) {
            kernel_video_set_present_copy_override(value);
            return 0;
        }
        return -1;
    }

    payload.value = value;
    if (mk_video_prepare_request(&request, type, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_set_present_policy(uint32_t policy) {
    return mk_video_service_u32_request(MK_MSG_VIDEO_SET_PRESENT_POLICY, policy);
}

int mk_video_service_set_present_copy_override(uint32_t kind) {
    return mk_video_service_u32_request(MK_MSG_VIDEO_SET_PRESENT_COPY_OVERRIDE, kind);
}

int mk_video_service_leave_graphics(void) {
    struct mk_message request;
    struct mk_message reply;

    if (mk_video_current_process_is_service_worker()) {
        return mk_video_execute_control_job(MK_VIDEO_CONTROL_JOB_LEAVE, 0u, 0u);
    }

    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_LEAVE, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_set_mode(uint32_t width, uint32_t height) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_mode_request payload;

    if (mk_video_current_process_is_service_worker()) {
        return mk_video_execute_control_job(MK_VIDEO_CONTROL_JOB_MODE_SET, width, height);
    }

    payload.width = width;
    payload.height = height;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_MODE_SET, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

static int mk_video_palette_request_common(uint32_t type, uint8_t *palette) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_palette_request payload;
    uint32_t transfer_id;
    int rc;

    if (palette == 0) {
        return -1;
    }
    /* Palette flips happen in the desktop/bootstrap critical path; avoid
       transfer + service hops there and hit the kernel video backend directly. */
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        if (type == MK_MSG_VIDEO_SET_PALETTE) {
            return kernel_video_set_palette(palette);
        }
        if (type == MK_MSG_VIDEO_GET_PALETTE) {
            return kernel_video_get_palette(palette);
        }
        return -1;
    }
    if (mk_video_get_palette_transfer(&transfer_id) != 0) {
        return -1;
    }
    if (mk_video_share_transfer(transfer_id,
                                type == MK_MSG_VIDEO_SET_PALETTE
                                    ? MK_TRANSFER_PERM_READ
                                    : MK_TRANSFER_PERM_WRITE) != 0) {
        return -1;
    }
    if (type == MK_MSG_VIDEO_SET_PALETTE &&
        mk_transfer_copy_from(transfer_id, palette, MK_VIDEO_PALETTE_BYTES) != 0) {
        return -1;
    }

    payload.transfer_id = transfer_id;
    payload.byte_count = MK_VIDEO_PALETTE_BYTES;
    if (mk_video_prepare_request(&request, type, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }

    rc = mk_video_decode_result(&reply);
    if (type == MK_MSG_VIDEO_GET_PALETTE && rc == 0 &&
        mk_transfer_copy_to(transfer_id, palette, MK_VIDEO_PALETTE_BYTES) != 0) {
        return -1;
    }
    return rc;
}

int mk_video_service_set_palette(const uint8_t *rgb_triplets) {
    return mk_video_palette_request_common(MK_MSG_VIDEO_SET_PALETTE, (uint8_t *)(uintptr_t)rgb_triplets);
}

int mk_video_service_get_palette(uint8_t *rgb_triplets) {
    return mk_video_palette_request_common(MK_MSG_VIDEO_GET_PALETTE, rgb_triplets);
}

int mk_video_service_blit8_transfer(uint32_t transfer_id, uint32_t byte_count,
                                    int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_request payload;

    const uint8_t *src;

    if (transfer_id == 0u || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return -1;
    }
    if (byte_count < (uint32_t)(src_w * src_h)) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        src = (const uint8_t *)mk_transfer_data_read(transfer_id);
        if (src == 0) {
            return -1;
        }
        if (mk_transfer_size(transfer_id) < byte_count) {
            mk_transfer_release(transfer_id);
            return -1;
        }
        kernel_gfx_blit8(src, src_w, src_h, dst_x, dst_y, scale);
        mk_transfer_release(transfer_id);
        return 0;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }

    payload.src_width = src_w;
    payload.src_height = src_h;
    payload.dst_x = dst_x;
    payload.dst_y = dst_y;
    payload.scale = scale;
    payload.byte_count = byte_count;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_BLIT8, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_blit8_present_transfer(uint32_t transfer_id, uint32_t byte_count,
                                            int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_request payload;

    const uint8_t *src;

    if (transfer_id == 0u || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return -1;
    }
    if (byte_count < (uint32_t)(src_w * src_h)) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        src = (const uint8_t *)mk_transfer_data_read(transfer_id);
        if (src == 0) {
            return -1;
        }
        if (mk_transfer_size(transfer_id) < byte_count) {
            mk_transfer_release(transfer_id);
            return -1;
        }
        kernel_gfx_blit8(src, src_w, src_h, dst_x, dst_y, scale);
        mk_transfer_release(transfer_id);
        return mk_video_submit_present_job(VIDEO_PRESENT_FULL, 0);
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }

    payload.src_width = src_w;
    payload.src_height = src_h;
    payload.dst_x = dst_x;
    payload.dst_y = dst_y;
    payload.scale = scale;
    payload.byte_count = byte_count;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_BLIT8_PRESENT, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        if (scale <= 0) {
            return -1;
        }
        kernel_gfx_blit8(src, src_w, src_h, dst_x, dst_y, scale);
        return 0;
    }
    if (mk_video_packed_blit8_byte_count(src_w, src_h, &byte_count) != 0) {
        return -1;
    }
    if (byte_count != 0u && byte_count <= MK_VIDEO_INLINE_BLIT8_MAX && scale > 0) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.src_width = src_w;
        inline_payload.src_height = src_h;
        inline_payload.dst_x = dst_x;
        inline_payload.dst_y = dst_y;
        inline_payload.scale = scale;
        inline_payload.byte_count = byte_count;
        if (mk_video_pack_blit8_source_to_buffer(src,
                                                 src_w,
                                                 src_h,
                                                 inline_payload.pixels,
                                                 byte_count) != 0) {
            return -1;
        }
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_BLIT8_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }

    if (mk_video_get_upload_transfer(byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_upload_packed_blit8_source(transfer_id,
                                            src,
                                            src_w,
                                            src_h,
                                            byte_count) != 0) {
        return -1;
    }
    return mk_video_service_blit8_transfer(transfer_id, byte_count, src_w, src_h, dst_x, dst_y, scale);
}

int mk_video_service_blit8_present(const uint8_t *src, int src_w, int src_h,
                                   int dst_x, int dst_y, int scale) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        if (scale <= 0) {
            return -1;
        }
        kernel_gfx_blit8(src, src_w, src_h, dst_x, dst_y, scale);
        return mk_video_submit_present_job(VIDEO_PRESENT_FULL, 0);
    }
    if (mk_video_packed_blit8_byte_count(src_w, src_h, &byte_count) != 0) {
        return -1;
    }
    if (byte_count != 0u && byte_count <= MK_VIDEO_INLINE_BLIT8_MAX && scale > 0) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.src_width = src_w;
        inline_payload.src_height = src_h;
        inline_payload.dst_x = dst_x;
        inline_payload.dst_y = dst_y;
        inline_payload.scale = scale;
        inline_payload.byte_count = byte_count;
        if (mk_video_pack_blit8_source_to_buffer(src,
                                                 src_w,
                                                 src_h,
                                                 inline_payload.pixels,
                                                 byte_count) != 0) {
            return -1;
        }
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_BLIT8_PRESENT_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }

    if (mk_video_get_upload_transfer(byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_upload_packed_blit8_source(transfer_id,
                                            src,
                                            src_w,
                                            src_h,
                                            byte_count) != 0) {
        return -1;
    }
    return mk_video_service_blit8_present_transfer(transfer_id,
                                                   byte_count,
                                                   src_w,
                                                   src_h,
                                                   dst_x,
                                                   dst_y,
                                                   scale);
}

int mk_video_service_blit8_stretch_transfer(uint32_t transfer_id, uint32_t byte_count,
                                            int src_w, int src_h,
                                            int dst_x, int dst_y, int dst_w, int dst_h) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_stretch_request payload;

    const uint8_t *src;

    if (transfer_id == 0u || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }
    if (byte_count < (uint32_t)(src_w * src_h)) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        src = (const uint8_t *)mk_transfer_data_read(transfer_id);
        if (src == 0) {
            return -1;
        }
        if (mk_transfer_size(transfer_id) < byte_count) {
            mk_transfer_release(transfer_id);
            return -1;
        }
        kernel_gfx_blit8_stretch(src, src_w, src_h, dst_x, dst_y, dst_w, dst_h);
        mk_transfer_release(transfer_id);
        return 0;
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }

    payload.src_width = src_w;
    payload.src_height = src_h;
    payload.dst_x = dst_x;
    payload.dst_y = dst_y;
    payload.dst_width = dst_w;
    payload.dst_height = dst_h;
    payload.byte_count = byte_count;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_BLIT8_STRETCH, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_blit8_stretch(const uint8_t *src, int src_w, int src_h,
                                   int dst_x, int dst_y, int dst_w, int dst_h) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_stretch_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        kernel_gfx_blit8_stretch(src, src_w, src_h, dst_x, dst_y, dst_w, dst_h);
        return 0;
    }
    if (mk_video_packed_blit8_byte_count(src_w, src_h, &byte_count) != 0) {
        return -1;
    }
    if (byte_count != 0u && byte_count <= MK_VIDEO_INLINE_BLIT8_MAX) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.src_width = src_w;
        inline_payload.src_height = src_h;
        inline_payload.dst_x = dst_x;
        inline_payload.dst_y = dst_y;
        inline_payload.dst_width = dst_w;
        inline_payload.dst_height = dst_h;
        inline_payload.byte_count = byte_count;
        if (mk_video_pack_blit8_source_to_buffer(src,
                                                 src_w,
                                                 src_h,
                                                 inline_payload.pixels,
                                                 byte_count) != 0) {
            return -1;
        }
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_BLIT8_STRETCH_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }
    if (mk_video_get_upload_transfer(byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_upload_packed_blit8_source(transfer_id,
                                            src,
                                            src_w,
                                            src_h,
                                            byte_count) != 0) {
        return -1;
    }
    return mk_video_service_blit8_stretch_transfer(transfer_id, byte_count,
                                                   src_w, src_h,
                                                   dst_x, dst_y, dst_w, dst_h);
}

int mk_video_service_blit8_stretch_present_transfer(uint32_t transfer_id, uint32_t byte_count,
                                                    int src_w, int src_h,
                                                    int dst_x, int dst_y, int dst_w, int dst_h) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_stretch_present_request payload;

    const uint8_t *src;

    if (transfer_id == 0u || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }
    if (byte_count < (uint32_t)(src_w * src_h)) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        src = (const uint8_t *)mk_transfer_data_read(transfer_id);
        if (src == 0) {
            return -1;
        }
        if (mk_transfer_size(transfer_id) < byte_count) {
            mk_transfer_release(transfer_id);
            return -1;
        }
        kernel_gfx_blit8_stretch(src, src_w, src_h, dst_x, dst_y, dst_w, dst_h);
        mk_transfer_release(transfer_id);
        return mk_video_submit_present_job(VIDEO_PRESENT_FULL, 0);
    }
    if (mk_video_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        return -1;
    }

    payload.src_width = src_w;
    payload.src_height = src_h;
    payload.dst_x = dst_x;
    payload.dst_y = dst_y;
    payload.dst_width = dst_w;
    payload.dst_height = dst_h;
    payload.byte_count = byte_count;
    payload.transfer_id = transfer_id;
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT,
                                 &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_result(&reply);
}

int mk_video_service_blit8_stretch_present(const uint8_t *src, int src_w, int src_h,
                                           int dst_x, int dst_y, int dst_w, int dst_h) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_video_blit8_stretch_present_inline_request inline_payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }
    if (mk_video_current_process_is_service_worker() ||
        mk_video_current_prefers_kernel_hot_path()) {
        kernel_gfx_blit8_stretch(src, src_w, src_h, dst_x, dst_y, dst_w, dst_h);
        return mk_video_submit_present_job(VIDEO_PRESENT_FULL, 0);
    }
    if (mk_video_packed_blit8_byte_count(src_w, src_h, &byte_count) != 0) {
        return -1;
    }
    if (byte_count != 0u && byte_count <= MK_VIDEO_INLINE_BLIT8_MAX) {
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.src_width = src_w;
        inline_payload.src_height = src_h;
        inline_payload.dst_x = dst_x;
        inline_payload.dst_y = dst_y;
        inline_payload.dst_width = dst_w;
        inline_payload.dst_height = dst_h;
        inline_payload.byte_count = byte_count;
        if (mk_video_pack_blit8_source_to_buffer(src,
                                                 src_w,
                                                 src_h,
                                                 inline_payload.pixels,
                                                 byte_count) != 0) {
            return -1;
        }
        if (mk_video_prepare_request(&request,
                                     MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT_INLINE,
                                     &inline_payload,
                                     sizeof(inline_payload)) != 0) {
            return -1;
        }
        if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
            return -1;
        }
        return mk_video_decode_result(&reply);
    }
    if (mk_video_get_upload_transfer(byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_video_upload_packed_blit8_source(transfer_id,
                                            src,
                                            src_w,
                                            src_h,
                                            byte_count) != 0) {
        return -1;
    }
    return mk_video_service_blit8_stretch_present_transfer(transfer_id, byte_count,
                                                           src_w, src_h,
                                                           dst_x, dst_y, dst_w, dst_h);
}

int mk_video_service_get_info(struct video_mode *mode) {
    struct mk_message request;
    struct mk_message reply;

    if (mode == 0) {
        return -1;
    }
    if (mk_video_current_prefers_kernel_hot_path()) {
        struct video_mode *current = kernel_video_get_mode();

        if (current == 0) {
            memset(mode, 0, sizeof(*mode));
            return -1;
        }
        *mode = *current;
        return 0;
    }
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_GET_INFO, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_mode(&reply, mode);
}

int mk_video_service_get_caps(struct video_capabilities *caps) {
    struct mk_message request;
    struct mk_message reply;

    if (caps == 0) {
        return -1;
    }
    if (mk_video_current_prefers_kernel_hot_path()) {
        kernel_video_get_capabilities(caps);
        return 0;
    }
    if (mk_video_prepare_request(&request, MK_MSG_VIDEO_GET_CAPS, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_VIDEO, &request, &reply) != 0) {
        return -1;
    }
    return mk_video_decode_caps(&reply, caps);
}

int mk_video_service_subscribe(struct process *subscriber) {
    struct mk_video_event_subscription *subscription;
    struct video_mode *mode;

    if (subscriber == 0) {
        return -1;
    }

    subscription = mk_video_find_subscription(subscriber);
    if (subscription != 0) {
        return 0;
    }

    subscription = mk_video_alloc_subscription(subscriber);
    if (subscription == 0) {
        return -1;
    }

    mode = kernel_video_get_mode();
    if (mode != 0 && mode->width != 0u && mode->height != 0u) {
        mk_video_enqueue_event(subscription,
                               MK_VIDEO_EVENT_MODE_SET_DONE,
                               VIDEO_PRESENT_AUTO,
                               mk_video_next_event_sequence());
        mk_video_enqueue_event(subscription,
                               MK_VIDEO_EVENT_MODE_SET,
                               VIDEO_PRESENT_AUTO,
                               mk_video_next_event_sequence());
    }
    if (g_video_backend_faulted) {
        mk_video_enqueue_event(subscription,
                               MK_VIDEO_EVENT_BACKEND_FAILED,
                               VIDEO_PRESENT_AUTO,
                               mk_video_next_event_sequence());
    }
    return 0;
}

int mk_video_service_event_receive(struct process *subscriber,
                                   struct mk_video_event *event,
                                   uint32_t timeout_ticks) {
    struct mk_video_event_subscription *subscription;
    uint32_t dropped_events;
    int wait_rc;

    if (subscriber == 0 || event == 0) {
        return -1;
    }

    subscription = mk_video_find_subscription(subscriber);
    if (subscription == 0) {
        return -1;
    }

    for (;;) {
        if (kernel_mailbox_try_receive(&subscription->mailbox, event) == 0) {
            return 0;
        }
        dropped_events = kernel_mailbox_dropped(&subscription->mailbox);
        if (dropped_events != 0u) {
            struct video_mode *mode;

            memset(event, 0, sizeof(*event));
            mode = kernel_video_get_mode();
            event->abi_version = 1u;
            event->event_type = MK_VIDEO_EVENT_OVERFLOW;
            event->sequence = ++g_video_event_sequence;
            event->completed_sequence = g_video_present_last_completed_sequence;
            event->pending_depth = g_video_present_pending_depth;
            if (mode != 0) {
                event->active_width = mode->width;
                event->active_height = mode->height;
            }
            event->dropped_events = dropped_events;
            kernel_mailbox_clear_dropped(&subscription->mailbox);
            event->tick = kernel_timer_get_ticks();
            return 0;
        }

        if (timeout_ticks == 0u) {
            return -1;
        }
        wait_rc = kernel_mailbox_wait(&subscription->mailbox, timeout_ticks);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}
