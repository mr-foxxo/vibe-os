#include <kernel/bootinfo.h>
#include <kernel/telemetry.h>
#include <kernel/kernel_string.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/service.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>

struct mk_launch_record {
    int pid;
    struct process *process;
    struct mk_launch_context context;
};

static struct mk_launch_record g_launch_records[MK_LAUNCH_SLOTS];

#define MK_LAUNCH_FLAG_USER_MASK \
    (MK_LAUNCH_FLAG_USER_SHELL | MK_LAUNCH_FLAG_USER_DESKTOP | MK_LAUNCH_FLAG_USER_APP)

static int mk_launch_validate_argv_data(const struct mk_launch_descriptor *descriptor) {
    uint32_t arg_index;
    uint32_t offset = 0u;

    if (descriptor->argc > MK_LAUNCH_ARGC_MAX) {
        return -1;
    }
    if (descriptor->argc == 0u) {
        return 0;
    }

    for (arg_index = 0u; arg_index < descriptor->argc; ++arg_index) {
        uint32_t start = offset;

        while (offset < MK_LAUNCH_ARGV_BYTES &&
               descriptor->argv_data[offset] != '\0') {
            offset += 1u;
        }
        if (offset >= MK_LAUNCH_ARGV_BYTES) {
            return -1;
        }
        if (offset == start) {
            return -1;
        }
        offset += 1u;
    }

    return 0;
}

void mk_launch_init(void) {
    memset(g_launch_records, 0, sizeof(g_launch_records));
}

int mk_launch_validate_descriptor(const struct mk_launch_descriptor *descriptor) {
    uint32_t i;

    if (descriptor == 0) {
        return -1;
    }
    if (descriptor->abi_version != MK_LAUNCH_ABI_VERSION) {
        return -1;
    }
    if (descriptor->entry == 0) {
        return -1;
    }
    if (descriptor->kind != MK_LAUNCH_KIND_DRIVER &&
        descriptor->kind != MK_LAUNCH_KIND_SERVICE &&
        descriptor->kind != MK_LAUNCH_KIND_USER) {
        return -1;
    }
    if (descriptor->kind == MK_LAUNCH_KIND_SERVICE &&
        descriptor->service_type == MK_SERVICE_NONE) {
        return -1;
    }
    if (descriptor->kind != MK_LAUNCH_KIND_SERVICE &&
        descriptor->service_type != MK_SERVICE_NONE) {
        return -1;
    }
    if (descriptor->task_class > MK_TASK_CLASS_VIDEO_CONTROL) {
        return -1;
    }
    if (descriptor->stack_size != 0u && descriptor->stack_size < 1024u) {
        return -1;
    }
    if (mk_launch_validate_argv_data(descriptor) != 0) {
        return -1;
    }
    if (descriptor->kind != MK_LAUNCH_KIND_USER &&
        (descriptor->flags & MK_LAUNCH_FLAG_USER_MASK) != 0u) {
        return -1;
    }

    for (i = 0; i < MK_LAUNCH_NAME_MAX; ++i) {
        if (descriptor->name[i] == '\0') {
            return i == 0u ? -1 : 0;
        }
    }

    return -1;
}

static struct mk_launch_record *mk_launch_alloc_record(void) {
    uint32_t i;

    for (i = 0; i < MK_LAUNCH_SLOTS; ++i) {
        if (g_launch_records[i].pid == 0) {
            return &g_launch_records[i];
        }
    }

    return 0;
}

/*
 * Failure-naming for the launch bootstrap (contract: an unsupported/failed
 * path must emit an explicit diagnostic, never a silent -1 that userland can
 * only report as "launch failed"). Budgeted so it cannot flood the serial.
 */
static volatile uint32_t g_launch_fail_budget = VIBE_TRACE_BUDGET(16u);

static void mk_launch_fail(const char *why, const struct mk_launch_descriptor *d) {
    if (g_launch_fail_budget == 0u) {
        return;
    }
    g_launch_fail_budget--;
    if (d == 0) {
        kernel_debug_printf("launch: FAIL %s descriptor=0\n", why);
        return;
    }
    kernel_debug_printf("launch: FAIL %s kind=%d svc=%d flags=%x abi=%d entry=%x stack=%u class=%d name0=%c\n",
                        why, (int)d->kind, (int)d->service_type,
                        (unsigned int)d->flags, (int)d->abi_version,
                        (unsigned int)(uintptr_t)d->entry,
                        (unsigned int)d->stack_size, (int)d->task_class,
                        d->name[0] != '\0' ? d->name[0] : '0');
}

static void mk_launch_fill_context(struct mk_launch_context *context,
                                   const struct mk_launch_descriptor *descriptor,
                                   const struct process *process) {
    const volatile struct bootinfo *bootinfo;

    memset(context, 0, sizeof(*context));
    context->abi_version = MK_LAUNCH_ABI_VERSION;
    context->pid = (uint32_t)process->pid;
    context->kind = descriptor->kind;
    context->service_type = descriptor->service_type;
    context->flags = descriptor->flags;
    context->task_class = process->task_class;
    context->argc = descriptor->argc;
    strncpy(context->name, descriptor->name, MK_LAUNCH_NAME_MAX - 1u);
    context->name[MK_LAUNCH_NAME_MAX - 1u] = '\0';
    memcpy(context->argv_data, descriptor->argv_data, sizeof(context->argv_data));

    bootinfo = (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return;
    }

    context->boot_flags = bootinfo->flags;
    context->boot_partition_lba = bootinfo->disk.boot_partition_lba;
    context->boot_partition_sectors = bootinfo->disk.boot_partition_sectors;
    context->data_partition_lba = bootinfo->disk.data_partition_lba;
    context->data_partition_sectors = bootinfo->disk.data_partition_sectors;
}

int mk_launch_bootstrap(const struct mk_launch_descriptor *descriptor) {
    struct mk_launch_record *record;
    process_t *process;
    enum process_kind process_kind;

    if (mk_launch_validate_descriptor(descriptor) != 0) {
        mk_launch_fail("validate", descriptor);
        return -1;
    }

    record = mk_launch_alloc_record();
    if (record == 0) {
        mk_launch_fail("no-record-slot", descriptor);
        return -1;
    }

    process_kind = descriptor->kind == MK_LAUNCH_KIND_SERVICE
                 ? PROCESS_KIND_SERVICE
                 : PROCESS_KIND_USER;
    process = process_create_with_stack(descriptor->entry,
                                        process_kind,
                                        descriptor->service_type,
                                        descriptor->flags,
                                        descriptor->task_class,
                                        descriptor->stack_size == 0u
                                            ? MK_LAUNCH_STACK_SIZE_DEFAULT
                                            : descriptor->stack_size);
    if (process == 0) {
        mk_launch_fail("create-failed", descriptor);
        return -1;
    }

    if (descriptor->kind == MK_LAUNCH_KIND_SERVICE &&
        mk_service_register(descriptor->service_type, descriptor->name, process) != 0) {
        mk_launch_fail("register-failed", descriptor);
        process_destroy(process);
        return -1;
    }

    record->pid = process->pid;
    record->process = process;
    process->launch_context_pid = (uint32_t)process->pid;
    mk_launch_fill_context(&record->context, descriptor, process);
    scheduler_add_task(process);
    return process->pid;
}

void mk_launch_release_pid(int pid) {
    uint32_t i;

    if (pid <= 0) {
        return;
    }

    for (i = 0; i < MK_LAUNCH_SLOTS; ++i) {
        if (g_launch_records[i].pid == pid) {
            memset(&g_launch_records[i], 0, sizeof(g_launch_records[i]));
            return;
        }
    }
}

const struct mk_launch_context *mk_launch_context_for_pid(int pid) {
    uint32_t i;

    if (pid <= 0) {
        return 0;
    }

    for (i = 0; i < MK_LAUNCH_SLOTS; ++i) {
        if (g_launch_records[i].pid == pid) {
            return &g_launch_records[i].context;
        }
    }

    return 0;
}

const struct mk_launch_context *mk_launch_context_current(void) {
    process_t *current = scheduler_current();
    uint32_t launch_pid;

    if (current == 0) {
        return 0;
    }

    launch_pid = current->launch_context_pid != 0u
                     ? current->launch_context_pid
                     : (uint32_t)current->pid;
    return mk_launch_context_for_pid((int)launch_pid);
}
