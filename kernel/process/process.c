#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/microkernel/service.h>
#include <kernel/kernel.h>    /* for panic if allocation fails */
#include <kernel/memory/heap.h>   /* kernel_malloc, kernel_free */
#include <kernel/drivers/debug/debug.h>
#include <kernel/cpu/cpu.h>
#include <lang/include/vibe_app.h>

#define PROCESS_DEFAULT_STACK_SIZE 4096u
#define PROCESS_MIN_STACK_SIZE 1024u

__attribute__((noreturn)) static void process_entry_return_trampoline(void) {
    process_t *current = scheduler_current();

    if (current != NULL) {
        scheduler_terminate_task(current);
    }

    schedule();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static uint32_t process_normalize_stack_size(uint32_t stack_size) {
    if (stack_size == 0u) {
        stack_size = PROCESS_DEFAULT_STACK_SIZE;
    }
    if (stack_size < PROCESS_MIN_STACK_SIZE) {
        stack_size = PROCESS_MIN_STACK_SIZE;
    }
    return (stack_size + 15u) & ~15u;
}

int g_next_pid = 1;

/*
 * SMP-safe pid allocation. Plain g_next_pid++ is a non-atomic RMW: two CPUs
 * creating tasks concurrently could claim the same pid, and the whole
 * pid-keyed infrastructure (ipc queues via owner_pid, launch records,
 * scheduler_find_task_by_pid) would then bind to the wrong task.
 * lock xaddl is i486+; the kernel is built i586.
 */
int process_alloc_pid(void) {
    /*
     * Fetch-and-add 1: the register MUST enter with the increment value.
     * The previous form declared it output-only ("=&r"), so xaddl added
     * whatever stale value the register happened to hold to g_next_pid
     * (observed under SMP: pids advanced by 1628 per task, then by heap
     * addresses — the root cause of the "phantom task node" corruption,
     * where a node's pid equaled its own block address).
     */
    int pid = 1;
    __asm__ volatile("lock xaddl %0, %1"
                     : "+r"(pid), "+m"(g_next_pid)
                     :
                     : "memory", "cc");
    return pid;
}

static int process_entry_in_range(uintptr_t entry, uintptr_t base, uint32_t size) {
    return entry >= base && entry < (base + (uintptr_t)size);
}

int process_entry_supports_user_mode(uintptr_t entry) {
    return process_entry_in_range(entry, VIBE_APP_LOAD_ADDR, VIBE_APP_ARENA_SIZE) ||
           process_entry_in_range(entry, VIBE_APP_COMPAT_LOAD_ADDR_20260325, VIBE_APP_ARENA_SIZE) ||
           process_entry_in_range(entry, VIBE_APP_DESKTOP_LOAD_ADDR, VIBE_APP_DESKTOP_ARENA_SIZE) ||
           process_entry_in_range(entry, VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325, VIBE_APP_DESKTOP_ARENA_SIZE) ||
           process_entry_in_range(entry, VIBE_APP_BOOT_LOAD_ADDR, VIBE_APP_BOOT_ARENA_SIZE) ||
           process_entry_in_range(entry, VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325, VIBE_APP_BOOT_ARENA_SIZE);
}

uintptr_t process_user_stack_top_for_entry(uintptr_t entry) {
    if (process_entry_in_range(entry, VIBE_APP_LOAD_ADDR, VIBE_APP_ARENA_SIZE)) {
        return VIBE_APP_STACK_TOP;
    }
    if (process_entry_in_range(entry, VIBE_APP_COMPAT_LOAD_ADDR_20260325, VIBE_APP_ARENA_SIZE)) {
        return VIBE_APP_COMPAT_LOAD_ADDR_20260325 + VIBE_APP_ARENA_SIZE;
    }
    if (process_entry_in_range(entry, VIBE_APP_DESKTOP_LOAD_ADDR, VIBE_APP_DESKTOP_ARENA_SIZE)) {
        return VIBE_APP_DESKTOP_STACK_TOP;
    }
    if (process_entry_in_range(entry, VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325, VIBE_APP_DESKTOP_ARENA_SIZE)) {
        return VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325 + VIBE_APP_DESKTOP_ARENA_SIZE;
    }
    if (process_entry_in_range(entry, VIBE_APP_BOOT_LOAD_ADDR, VIBE_APP_BOOT_ARENA_SIZE)) {
        return VIBE_APP_BOOT_STACK_TOP;
    }
    if (process_entry_in_range(entry, VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325, VIBE_APP_BOOT_ARENA_SIZE)) {
        return VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325 + VIBE_APP_BOOT_ARENA_SIZE;
    }
    return 0u;
}


static uint32_t process_default_task_class_for(enum process_kind kind,
                                               uint32_t service_type,
                                               uint32_t launch_flags) {
    if (kind == PROCESS_KIND_USER) {
        if ((launch_flags & (MK_LAUNCH_FLAG_BOOTSTRAP | MK_LAUNCH_FLAG_CRITICAL)) != 0u) {
            return MK_TASK_CLASS_SUPERVISION;
        }
        if ((launch_flags & MK_LAUNCH_FLAG_USER_DESKTOP) != 0u) {
            return MK_TASK_CLASS_DESKTOP;
        }
        if ((launch_flags & MK_LAUNCH_FLAG_USER_SHELL) != 0u) {
            return MK_TASK_CLASS_SHELL;
        }
        if ((launch_flags & MK_LAUNCH_FLAG_USER_APP) != 0u) {
            return MK_TASK_CLASS_APP_RUNTIME;
        }
        return MK_TASK_CLASS_APP_RUNTIME;
    }

    if (kind == PROCESS_KIND_SERVICE) {
        switch (service_type) {
        case MK_SERVICE_INIT:
            return MK_TASK_CLASS_SUPERVISION;
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
        default:
            return MK_TASK_CLASS_NONE;
        }
    }

    return MK_TASK_CLASS_NONE;
}

static uint32_t process_priority_for_task_class(uint32_t task_class) {
    switch (task_class) {
    case MK_TASK_CLASS_SUPERVISION:
    case MK_TASK_CLASS_DESKTOP:
    case MK_TASK_CLASS_SHELL:
        return PROCESS_PRIORITY_DESKTOP_USER;
    case MK_TASK_CLASS_INPUT:
    case MK_TASK_CLASS_CONSOLE_IO:
        return PROCESS_PRIORITY_INPUT;
    case MK_TASK_CLASS_VIDEO_PRESENT:
    case MK_TASK_CLASS_VIDEO_CONTROL:
        return PROCESS_PRIORITY_VIDEO;
    case MK_TASK_CLASS_STORAGE_IO:
    case MK_TASK_CLASS_FILESYSTEM_IO:
        return PROCESS_PRIORITY_STORAGE;
    case MK_TASK_CLASS_AUDIO_IO:
        /*
         * Audio playback/control must be able to continue while the desktop is
         * animating and processing input; otherwise startup WAVs and player
         * output appear tied to UI activity.
         */
        return PROCESS_PRIORITY_DESKTOP_USER;
    case MK_TASK_CLASS_NETWORK_IO:
        return PROCESS_PRIORITY_NETWORK;
    case MK_TASK_CLASS_APP_RUNTIME:
        return PROCESS_PRIORITY_APP;
    default:
        return PROCESS_PRIORITY_BACKGROUND;
    }
}

static uint32_t process_priority_for(enum process_kind kind,
                                     uint32_t service_type,
                                     uint32_t launch_flags,
                                     uint32_t task_class) {
    if (task_class != MK_TASK_CLASS_NONE) {
        return process_priority_for_task_class(task_class);
    }
    if (kind == PROCESS_KIND_USER) {
        if ((launch_flags & (MK_LAUNCH_FLAG_USER_SHELL |
                             MK_LAUNCH_FLAG_USER_DESKTOP |
                             MK_LAUNCH_FLAG_BOOTSTRAP |
                             MK_LAUNCH_FLAG_CRITICAL)) != 0u) {
            return PROCESS_PRIORITY_DESKTOP_USER;
        }
        return PROCESS_PRIORITY_APP;
    }
    if (kind == PROCESS_KIND_SERVICE) {
        switch (service_type) {
        case MK_SERVICE_INPUT:
        case MK_SERVICE_CONSOLE:
            return PROCESS_PRIORITY_INPUT;
        case MK_SERVICE_VIDEO:
            return PROCESS_PRIORITY_VIDEO;
        case MK_SERVICE_STORAGE:
        case MK_SERVICE_FILESYSTEM:
            return PROCESS_PRIORITY_STORAGE;
        case MK_SERVICE_AUDIO:
            return PROCESS_PRIORITY_DESKTOP_USER;
        case MK_SERVICE_NETWORK:
            return PROCESS_PRIORITY_NETWORK;
        case MK_SERVICE_INIT:
            return PROCESS_PRIORITY_DESKTOP_USER;
        default:
            return PROCESS_PRIORITY_BACKGROUND;
        }
    }
    return PROCESS_PRIORITY_BACKGROUND;
}

void process_setup_initial_context_arg(process_t *proc,
                                       uintptr_t entry,
                                       uintptr_t stack_top,
                                       uintptr_t arg) {
    kernel_trap_frame_t *frame;
    uintptr_t entry_stack_top;
    uint32_t *return_slot;
    uint32_t *arg_slot;

    if (proc == NULL || stack_top < (sizeof(kernel_trap_frame_t) + (sizeof(uint32_t) * 2u))) {
        return;
    }

    entry_stack_top = stack_top - (sizeof(uint32_t) * 2u);
    return_slot = (uint32_t *)entry_stack_top;
    arg_slot = return_slot + 1;
    *return_slot = (uint32_t)(uintptr_t)process_entry_return_trampoline;
    *arg_slot = (uint32_t)arg;

    frame = (kernel_trap_frame_t *)(entry_stack_top - sizeof(kernel_trap_frame_t));
    memset(frame, 0, sizeof(*frame));
    frame->esp_dummy = (uint32_t)entry_stack_top;
    frame->eip = (uint32_t)entry;
    frame->cs = KERNEL_CS_SELECTOR;
    frame->eflags = 0x00000202u;
    proc->context = frame;
    proc->runs_in_user_mode = 0u;
    proc->user_stack_top = 0u;
}

void process_setup_initial_user_context_arg(process_t *proc,
                                            uintptr_t entry,
                                            uintptr_t user_stack_top,
                                            uintptr_t arg) {
    kernel_user_trap_frame_t *frame;
    uintptr_t kernel_stack_top;
    uintptr_t entry_stack_top;
    uint32_t *return_slot;
    uint32_t *arg_slot;

    if (proc == NULL || proc->stack == NULL || proc->stack_size < sizeof(*frame)) {
        return;
    }
    if (user_stack_top < (sizeof(uint32_t) * 2u)) {
        return;
    }

    entry_stack_top = user_stack_top - (sizeof(uint32_t) * 2u);
    return_slot = (uint32_t *)entry_stack_top;
    arg_slot = return_slot + 1;
    *return_slot = 0u;
    *arg_slot = (uint32_t)arg;

    kernel_stack_top = (uintptr_t)proc->stack + proc->stack_size;
    frame = (kernel_user_trap_frame_t *)(kernel_stack_top - sizeof(*frame));
    memset(frame, 0, sizeof(*frame));
    frame->base.esp_dummy = (uint32_t)(uintptr_t)&frame->base.eip;
    frame->base.eip = (uint32_t)entry;
    frame->base.cs = USER_CS_SELECTOR;
    frame->base.eflags = 0x00000202u;
    frame->user_esp = (uint32_t)entry_stack_top;
    frame->user_ss = USER_DS_SELECTOR;
    proc->context = &frame->base;
    proc->runs_in_user_mode = 1u;
    proc->user_stack_top = user_stack_top;
    if (proc->pid == 12) {
        kernel_debug_printf("process: user context init proc=%d proc_ptr=%x context=%x stack=%x stack_size=%u user_stack_top=%x frame=%x esp_dummy=%x eip=%x cs=%x user_esp=%x user_ss=%x\n",
                            proc->pid,
                            (unsigned int)(uintptr_t)proc,
                            (unsigned int)(uintptr_t)proc->context,
                            (unsigned int)(uintptr_t)proc->stack,
                            proc->stack_size,
                            (unsigned int)proc->user_stack_top,
                            (unsigned int)(uintptr_t)frame,
                            (unsigned int)frame->base.esp_dummy,
                            (unsigned int)frame->base.eip,
                            (unsigned int)frame->base.cs,
                            (unsigned int)frame->user_esp,
                            (unsigned int)frame->user_ss);
    }
}

void process_setup_initial_context(process_t *proc, uintptr_t entry, uintptr_t stack_top) {
    process_setup_initial_context_arg(proc, entry, stack_top, 0u);
}

process_t *process_create(void (*entry)(void)) {
    return process_create_with_stack(entry,
                                     PROCESS_KIND_USER,
                                     0u,
                                     0u,
                                     MK_TASK_CLASS_NONE,
                                     PROCESS_DEFAULT_STACK_SIZE);
}

process_t *process_create_kind(void (*entry)(void), enum process_kind kind, uint32_t service_type) {
    return process_create_with_stack(entry,
                                     kind,
                                     service_type,
                                     0u,
                                     MK_TASK_CLASS_NONE,
                                     PROCESS_DEFAULT_STACK_SIZE);
}

process_t *process_create_with_stack(void (*entry)(void),
                                     enum process_kind kind,
                                     uint32_t service_type,
                                     uint32_t launch_flags,
                                     uint32_t task_class,
                                     uint32_t stack_size) {
    uint32_t normalized_stack_size;
    uint32_t effective_task_class;

    if (entry == NULL) {
        return NULL;
    }
    normalized_stack_size = process_normalize_stack_size(stack_size);

    process_t *p = kernel_malloc(sizeof(process_t));
    if (!p) {
        return NULL;
    }
    memset(p, 0, sizeof(*p));

    p->pid = process_alloc_pid();
    p->pgid = p->pid;
    p->sid = p->pid;
    p->current_cpu = -1;
    { int i; for (i = 0; i < RLIMIT_NLIMITS; ++i) { p->rlimits[i].rlim_cur = -1; p->rlimits[i].rlim_max = -1; } }
    p->preferred_cpu = -1;
    p->last_cpu = -1;
    p->state = PROCESS_READY;
    p->kind = kind;
    p->service_type = service_type;
    effective_task_class = task_class != MK_TASK_CLASS_NONE
                               ? task_class
                               : process_default_task_class_for(kind, service_type, launch_flags);
    p->task_class = effective_task_class;
    p->priority_tier = process_priority_for(kind,
                                            service_type,
                                            launch_flags,
                                            effective_task_class);
    p->stack_size = normalized_stack_size;
    p->runtime_ticks = 0u;
    p->last_start_tick = 0u;
    p->context_switches = 0u;
    p->wait_channel = 0;
    p->wait_deadline = 0u;
    p->wait_result = TASK_WAIT_RESULT_NONE;
    p->wait_event_kind = TASK_WAIT_EVENT_NONE;
    p->wait_event_class = TASK_WAIT_CLASS_NONE;
    p->wait_owner_service = 0u;
    p->wake_boost_budget = 0u;
    p->abi_kind = PROCESS_ABI_UNKNOWN;
    p->abi_version = 0u;
    p->abi_osabi = 0u;
    p->abi_machine = 0u;
    p->launch_context_pid = 0u;
    p->image_base = 0u;
    p->image_size = 0u;
    p->entry_point = (uintptr_t)entry;
    p->wait_next = 0;
    p->next = NULL;
    p->user_stack_top = 0u;
    p->runs_in_user_mode = 0u;

    /* allocate stack memory and set initial register state */
    p->stack = kernel_malloc(normalized_stack_size);
    if (!p->stack) {
        kernel_free(p);
        return NULL;
    }
    /*
     * Stack watermark fill (COMPLETION_ACTION_PLAN P4). The refined SMP
     * signature (a live task block whose canary is trashed FIRST, then its
     * payload/header filled with return-address-shaped values, with NO
     * heap-API guard firing) is exactly what a kernel-stack exhaustion
     * looks like: the stack block is allocated immediately AFTER the
     * process_t block, and stacks grow down, so an overflow walks the
     * process_t canary -> payload -> header. Fill the stack with a pattern
     * before the initial frame is placed so the deepest used byte can be
     * measured later. Must happen BEFORE process_setup_initial_context.
     */
    memset(p->stack, 0xA5, normalized_stack_size);

    process_setup_initial_context(p,
                                  (uintptr_t)entry,
                                  (uintptr_t)p->stack + normalized_stack_size);
    return p;
}

void process_terminate(process_t *proc) {
    if (!proc) {
        return;
    }

    proc->state = PROCESS_TERMINATED;
    proc->current_cpu = -1;
    proc->preferred_cpu = -1;
    proc->last_start_tick = 0u;
}

void process_set_abi_metadata(process_t *proc,
                              uint32_t abi_kind,
                              uint32_t abi_version,
                              uint32_t abi_osabi,
                              uint32_t abi_machine,
                              uintptr_t image_base,
                              uint32_t image_size,
                              uintptr_t entry_point) {
    if (proc == NULL) {
        return;
    }

    proc->abi_kind = abi_kind;
    proc->abi_version = abi_version;
    proc->abi_osabi = abi_osabi;
    proc->abi_machine = abi_machine;
    proc->image_base = image_base;
    proc->image_size = image_size;
    proc->entry_point = entry_point;
}

void process_destroy(process_t *proc) {
    if (!proc) {
        return;
    }
    /*
     * Refuse to free a task still reachable from the scheduler list.
     * g_head is written exactly once by scheduler_add_task, so a
     * linked-then-freed block is precisely how the head ends up dangling
     * at a recycled allocation (real-time smp2 BAD-HEAD signature,
     * COMPLETION_ACTION_PLAN P4 / KNOWN_BUGS classe #5). A leak with a
     * named caller diagnostic beats silent corruption of all dispatch.
     */
    if (scheduler_task_is_linked(proc)) {
        kernel_debug_printf("process-destroy: STILL-LINKED pid=%d node=0x%x caller=0x%x (free refused)\n",
                            proc->pid,
                            (unsigned int)(uintptr_t)proc,
                            (unsigned int)(uintptr_t)__builtin_return_address(0));
        return;
    }
    if (proc->stack) {
        kernel_free(proc->stack);
    }
    kernel_free(proc);
}
