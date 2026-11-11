#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/resource.h>

union sigval {
    int sival_int;
    void *sival_ptr;
};

struct sigevent {
    int sigev_notify;
    int sigev_signo;
    union sigval sigev_value;
    void (*sigev_notify_function)(union sigval);
    void *sigev_notify_attributes;
};
#include <kernel/event.h>

#define PTHREAD_KEYS_MAX 64

/* process states for scheduler bookkeeping */
enum process_state {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED,
};

enum process_kind {
    PROCESS_KIND_USER = 0,
    PROCESS_KIND_SERVICE = 1,
    PROCESS_KIND_KERNEL_TASK = 2,
};

enum process_priority_tier {
    PROCESS_PRIORITY_DESKTOP_USER = 0,
    PROCESS_PRIORITY_INPUT = 1,
    PROCESS_PRIORITY_VIDEO = 2,
    PROCESS_PRIORITY_STORAGE = 3,
    PROCESS_PRIORITY_AUDIO = 4,
    PROCESS_PRIORITY_NETWORK = 5,
    PROCESS_PRIORITY_APP = 6,
    PROCESS_PRIORITY_BACKGROUND = 7,
};

enum process_abi_kind {
    PROCESS_ABI_UNKNOWN = 0,
    PROCESS_ABI_NATIVE = 1,
    PROCESS_ABI_ELF32 = 2,
    PROCESS_ABI_APPFS = 3,
};

/*
 * Saved trap frame layout used by timer/yield preemption for ring0 tasks.
 * It matches the stack produced by:
 *   CPU interrupt gate push: EIP, CS, EFLAGS
 *   followed by the stub's pusha: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
 * When observed from the final ESP after pusha, the order below is correct.
 */
typedef struct kernel_trap_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} kernel_trap_frame_t;

/*
 * Saved trap frame layout when returning to ring3.
 * The CPU appends the user ESP/SS pair after EFLAGS on privilege-change
 * traps, so the common prefix above still lines up for scheduler/IRQ use.
 */
typedef struct kernel_user_trap_frame {
    kernel_trap_frame_t base;
    uint32_t user_esp;
    uint32_t user_ss;
} kernel_user_trap_frame_t;

typedef struct process {
    int pid;                /* process identifier */
    void *stack;            /* base pointer of allocated stack memory */
    uint32_t stack_size;    /* allocated stack size in bytes */
    kernel_trap_frame_t *context; /* saved trap frame on this process stack */
    uintptr_t user_stack_top;
    int current_cpu;        /* CPU que esta executando este processo, -1 se nenhuma */
    int preferred_cpu;      /* CPU alvo para balanceamento inicial */
    int last_cpu;           /* ultimo CPU que executou a tarefa */
    enum process_state state;
    enum process_kind kind;
    uint32_t service_type;
    uint32_t task_class;
    uint32_t priority_tier;
    uint32_t runtime_ticks;
    uint32_t last_start_tick;
    uint32_t context_switches;
    uint32_t last_task_event_sequence;
    uint32_t last_task_event_type;
    uint32_t last_task_event_tick;
    const void *wait_channel;
    uint32_t wait_deadline;
    uint32_t wait_result;
    uint32_t wait_event_kind;
    uint32_t wait_event_class;
    uint32_t wait_owner_service;
    uint32_t wake_boost_budget;
    uint32_t abi_kind;
    uint32_t abi_version;
    uint32_t abi_osabi;
    uint32_t abi_machine;
    uint32_t launch_context_pid;
    uintptr_t image_base;
    uint32_t image_size;
    uintptr_t entry_point;
    uint32_t runs_in_user_mode;
    struct process *wait_next;
    struct process *next;   /* linked‑list pointer for scheduler */
    /*
     * execve stack swap (P4 fix): the OLD kernel stack must NOT be freed
     * while the kernel is still executing on it and the TSS esp0 may still
     * point at its top. sys_execve parks it here; the next switch-away or
     * the terminate of this task releases it (both run off this stack).
     */
    void *pending_stack_free;

    /* Signal state */
    uint32_t signal_pending;
    uint32_t signal_blocked;
    uint32_t alarm_ticks;
    struct kernel_sigaction {
        uint32_t handler;
        uint32_t mask;
        int      flags;
    } signal_actions[32];

    /* POSIX extensions */
    int parent_pid;
    int exit_status;
    int child_exit_pid;
    int child_exit_status;
    int fds[16];
    uint32_t program_break;
    uint32_t program_break_start;
    char cwd[256];

    /* POSIX security and identity */
    pid_t pgid;
    pid_t sid;
    uid_t uid;
    uid_t euid;
    uid_t saved_uid;
    gid_t gid;
    gid_t egid;
    gid_t saved_gid;
    int nice_value;
    struct rlimit rlimits[RLIMIT_NLIMITS];
    gid_t groups[16];
    int ngroups;
    mode_t umask;

    /* Thread-local storage and thread management */
    unsigned int thread_group_id;
    void *thread_retval;
    int thread_detached;
    int thread_finished;
    kernel_waitable_t thread_join_waitable;
    void *tls_values[PTHREAD_KEYS_MAX];

    /* POSIX timers */
#define POSIX_TIMER_MAX 4
    struct posix_timer {
        int used;
        clockid_t clock_id;
        struct sigevent evp;
        timer_t timer_id;
        uint32_t it_value_ticks;
        uint32_t it_interval_ticks;
        uint32_t start_tick;
        int armed;
    } posix_timers[POSIX_TIMER_MAX];
} process_t;

/* create / destroy a process object.  the entry point will be installed
   in the register state and the stack allocated automatically. */
process_t *process_create(void (*entry)(void));
process_t *process_create_kind(void (*entry)(void), enum process_kind kind, uint32_t service_type);
process_t *process_create_with_stack(void (*entry)(void),
                                     enum process_kind kind,
                                     uint32_t service_type,
                                     uint32_t launch_flags,
                                     uint32_t task_class,
                                     uint32_t stack_size);
void process_setup_initial_context(process_t *proc, uintptr_t entry, uintptr_t stack_top);
void process_setup_initial_context_arg(process_t *proc,
                                       uintptr_t entry,
                                       uintptr_t stack_top,
                                       uintptr_t arg);
void process_setup_initial_user_context_arg(process_t *proc,
                                            uintptr_t entry,
                                            uintptr_t user_stack_top,
                                            uintptr_t arg);
void process_terminate(process_t *proc);
void process_destroy(process_t *proc);
void process_set_abi_metadata(process_t *proc,
                              uint32_t abi_kind,
                              uint32_t abi_version,
                              uint32_t abi_osabi,
                              uint32_t abi_machine,
                              uintptr_t image_base,
                              uint32_t image_size,
                              uintptr_t entry_point);
int process_entry_supports_user_mode(uintptr_t entry);
uintptr_t process_user_stack_top_for_entry(uintptr_t entry);

extern int g_next_pid;
int process_alloc_pid(void);

static inline int process_frame_is_user_mode(const kernel_trap_frame_t *frame) {
    return frame != NULL && (frame->cs & 0x3u) == 0x3u;
}

static inline uint32_t process_saved_eip(const process_t *proc) {
    return (proc != NULL && proc->context != NULL) ? proc->context->eip : 0u;
}

static inline uint32_t process_saved_esp(const process_t *proc) {
    if (proc == NULL || proc->context == NULL) {
        return 0u;
    }
    if (process_frame_is_user_mode(proc->context)) {
        const kernel_user_trap_frame_t *user_frame =
            (const kernel_user_trap_frame_t *)(const void *)proc->context;

        return user_frame->user_esp;
    }
    return (uint32_t)((uintptr_t)proc->context + sizeof(kernel_trap_frame_t));
}

#endif /* KERNEL_PROCESS_H */
