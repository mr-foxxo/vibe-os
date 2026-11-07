#include <kernel/kernel.h>
#include <kernel/scheduler.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/input/input.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/fs.h>
#include <kernel/elf_loader.h>
#include <kernel/memory/paging.h>
#include <sys/stat.h>
#include <kernel/microkernel.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/ipc.h>
#include <kernel/process.h>
#include <kernel/hal/io.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/physmem.h>
#include <kernel/cpu/cpu.h>
#include <kernel/smp.h>
#include <lang/include/vibe_app.h>
#include <stdint.h>
#include <include/userland_api.h> /* syscall IDs */
#include <string.h>
#include <kernel/drivers/input/input.h>

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

/* syscall table and helpers for the new kernel-side mechanism.  The
   legacy stage2 dispatch is still compiled into the image; eventually we
   will migrate completely to this table-driven approach. */

#define MAX_SYSCALLS 232
typedef uint32_t (*syscall_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static syscall_fn syscall_table[MAX_SYSCALLS];
static uint8_t g_audio_boot_trace_launch_info = 0u;
static uint8_t g_audio_boot_trace_text_write = 0u;
static uint8_t g_audio_boot_trace_write_debug = 0u;
static uint8_t g_audio_boot_trace_storage_load = 0u;

#define MAX_SHM_OBJECTS 16
#define SHM_NAME_MAX 32
typedef struct {
    char name[SHM_NAME_MAX];
    size_t size;
    void *data;
    int used;
} shm_object_t;
static shm_object_t g_shm_objects[MAX_SHM_OBJECTS];
static uint32_t g_shm_count = 0u;

/*
 * User-pointer validation (COMPLETION_ACTION_PLAN P4, real-time smp2 #GP).
 * Root cause proven by live-guest forensics: the syscall layer passed
 * app-supplied pointers straight into kernel copy paths (ATA inw loops,
 * memcpy of replies), so a buggy desktop app handing a garbage buffer
 * pointer made the KERNEL write disk bytes into its own .text (kernel
 * symbol bytes at 0x30bb3 found overwritten with file-shaped data while
 * icons loaded) and into task trap frames on the kernel heap — random
 * #GP/#UD/deadlocks downstream. Fault containment (AGENTS.md): an app
 * bug must cost the app, never the kernel.
 *
 * Policy: ring0 service tasks are trusted (they pass kernel heap
 * pointers legitimately — zero regression for services); ring3 tasks get
 * every pointer checked against (a) the user VA windows and (b) the
 * per-page U/S+present bits. The identity-mapped low 4MB has U set, so
 * windows alone are not enough; conversely mmap at 0x40000000+ is legal
 * user memory (pages mapped 0x007 = P|RW|U) and must pass.
 */
#define SYS_EFAULT_TRACE_BUDGET 8u
static uint32_t g_sys_efault_trace_budget = SYS_EFAULT_TRACE_BUDGET;

static int sys_va_in_user_window(uintptr_t va) {
    if (va >= (uintptr_t)VIBE_APP_LOAD_ADDR &&
        va < (uintptr_t)VIBE_APP_RESERVED_TOP) {
        return 1;
    }
    if (va >= 0x40000000u) {
        return 1; /* sys_mmap region */
    }
    if (va >= (uintptr_t)VIBE_APP_COMPAT_LOAD_ADDR_20260325 &&
        va < (uintptr_t)VIBE_APP_COMPAT_LOAD_ADDR_20260325 +
             (uintptr_t)VIBE_APP_ARENA_SIZE) {
        return 1;
    }
    if (va >= (uintptr_t)VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325 &&
        va < (uintptr_t)VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325 +
             (uintptr_t)VIBE_APP_DESKTOP_ARENA_SIZE) {
        return 1;
    }
    if (va >= (uintptr_t)VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325 &&
        va < (uintptr_t)VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325 +
             (uintptr_t)VIBE_APP_BOOT_ARENA_SIZE) {
        return 1;
    }
    return 0;
}

static int sys_user_ptr_ok(uintptr_t ptr, size_t len) {
    process_t *current;
    uintptr_t page;

    current = scheduler_current();
    if (current == 0 || current->runs_in_user_mode == 0u) {
        return 1; /* kernel/boot context: trusted */
    }
    if (len == 0u) {
        return 1;
    }
    if (ptr + len < ptr) {
        return 0;
    }
    if (!sys_va_in_user_window(ptr) || !sys_va_in_user_window(ptr + len - 1u)) {
        goto bad;
    }
    page = ptr & ~0xFFFu;
    for (;;) {
        if (!paging_va_user_accessible(page)) {
            goto bad;
        }
        if (page + 0x1000u >= ptr + len) {
            break;
        }
        page += 0x1000u;
    }
    return 1;
bad:
    if (g_sys_efault_trace_budget != 0u) {
        g_sys_efault_trace_budget--;
        kernel_debug_printf("syscall: EFAULT ptr=0x%x len=%u pid=%d\n",
                            (unsigned int)ptr, (unsigned int)len, current->pid);
    }
    return 0;
}

static int sys_user_str_ok(uintptr_t ptr, size_t maxlen) {
    process_t *current;
    uintptr_t addr = ptr;
    size_t n = 0u;

    current = scheduler_current();
    if (current == 0 || current->runs_in_user_mode == 0u) {
        return 1;
    }
    if (!sys_va_in_user_window(addr)) {
        goto bad;
    }
    while (n < maxlen) {
        if (!paging_va_user_accessible(addr & ~0xFFFu)) {
            goto bad;
        }
        if (*(const char *)addr == '\0') {
            return 1;
        }
        addr++;
        n++;
    }
bad:
    if (g_sys_efault_trace_budget != 0u) {
        g_sys_efault_trace_budget--;
        kernel_debug_printf("syscall: EFAULT-str ptr=0x%x pid=%d\n",
                            (unsigned int)ptr, current ? current->pid : -1);
    }
    return 0;
}

/* Whole-page range check for mm syscalls: every page must be mapped
 * user-accessible (munmap/madvise act on already-mapped memory). */
static int sys_user_range_mapped_ok(uintptr_t addr, size_t len) {
    uintptr_t page;

    if (len == 0u) {
        return 1;
    }
    if (addr + len < addr) {
        return 0;
    }
    if (!sys_va_in_user_window(addr) ||
        !sys_va_in_user_window(addr + len - 1u)) {
        return 0;
    }
    page = addr;
    while (page < addr + len) {
        if (!paging_va_user_accessible(page)) {
            return 0;
        }
        page += 0x1000u;
    }
    return 1;
}

extern void userland_shell_host_entry(void);
extern void userland_shell_session_entry(void);
extern void userland_desktop_host_entry(void);
extern void userland_startx_host_entry(void);
extern void userland_desktop_session_entry(void);
extern void userland_desktop_audio_host_entry(void);
extern void userland_boot_audio_host_entry(void);
extern void userland_audio_asset_host_entry(void);
extern void userland_app_host_entry(void);
extern void userland_app_runtime_entry(void);

static int sys_launch_app_copy_argv(const char *const *argv,
                                    uint32_t argc,
                                    struct mk_launch_descriptor *descriptor);
static void sys_launch_app_apply_role(struct mk_launch_descriptor *descriptor);
static process_t *sys_async_message_target(const struct mk_async_message *message);

static uint32_t sys_memory_pressure_level(uint32_t total_bytes, uint32_t free_bytes) {
    uint32_t free_percent;
    uint32_t divisor;

    if (total_bytes == 0u) {
        return MK_MEMORY_PRESSURE_NORMAL;
    }
    divisor = total_bytes / 100u;
    if (divisor == 0u) {
        divisor = 1u;
    }
    free_percent = free_bytes / divisor;
    if (free_percent > 100u) {
        free_percent = 100u;
    }
    if (free_percent >= 55u) {
        return MK_MEMORY_PRESSURE_IDLE;
    }
    if (free_percent <= 8u) {
        return MK_MEMORY_PRESSURE_CRITICAL;
    }
    if (free_percent <= 16u) {
        return MK_MEMORY_PRESSURE_HIGH;
    }
    if (free_percent <= 28u) {
        return MK_MEMORY_PRESSURE_ELEVATED;
    }
    return MK_MEMORY_PRESSURE_NORMAL;
}

static uint32_t sys_memory_current_arena_bytes(const process_t *current,
                                               const struct mk_launch_context *context) {
    if (context != 0) {
        if ((context->flags & MK_LAUNCH_FLAG_USER_DESKTOP) != 0u) {
            return VIBE_APP_DESKTOP_ARENA_SIZE;
        }
        if ((context->flags & MK_LAUNCH_FLAG_USER_APP) != 0u) {
            return VIBE_APP_ARENA_SIZE;
        }
        if ((context->flags & MK_LAUNCH_FLAG_BOOTSTRAP) != 0u) {
            return VIBE_APP_BOOT_ARENA_SIZE;
        }
    }
    if (current != 0 && current->task_class == MK_TASK_CLASS_APP_RUNTIME) {
        return VIBE_APP_ARENA_SIZE;
    }
    return 0u;
}

static uint32_t sys_memory_current_stack_reserve_bytes(const process_t *current,
                                                       const struct mk_launch_context *context) {
    if (context != 0) {
        if ((context->flags & MK_LAUNCH_FLAG_USER_DESKTOP) != 0u) {
            return VIBE_APP_DESKTOP_STACK_SIZE;
        }
        if ((context->flags & MK_LAUNCH_FLAG_USER_APP) != 0u) {
            return VIBE_APP_STACK_SIZE;
        }
        if ((context->flags & MK_LAUNCH_FLAG_BOOTSTRAP) != 0u) {
            return VIBE_APP_BOOT_STACK_SIZE;
        }
    }
    if (current != 0 && current->task_class == MK_TASK_CLASS_APP_RUNTIME) {
        return VIBE_APP_STACK_SIZE;
    }
    return current != 0 ? current->stack_size : 0u;
}

static uint32_t sys_memory_heap_ceiling_bytes(uint32_t arena_bytes, uint32_t stack_reserve_bytes) {
    return arena_bytes > stack_reserve_bytes ? (arena_bytes - stack_reserve_bytes) : 0u;
}

static uint32_t sys_memory_min(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t sys_memory_percent(uint32_t bytes, uint32_t percent) {
    uint32_t whole = bytes / 100u;
    uint32_t remainder = bytes % 100u;

    return (whole * percent) + ((remainder * percent) / 100u);
}

static uint32_t sys_memory_headroom_budget(uint32_t total_bytes,
                                           uint32_t ceiling_bytes,
                                           uint32_t percent) {
    return sys_memory_min(ceiling_bytes, sys_memory_percent(total_bytes, percent));
}

static uint32_t sys_memory_latency_flags(uint32_t task_class) {
    switch (task_class) {
    case MK_TASK_CLASS_DESKTOP:
    case MK_TASK_CLASS_APP_RUNTIME:
    case MK_TASK_CLASS_INPUT:
    case MK_TASK_CLASS_VIDEO_PRESENT:
    case MK_TASK_CLASS_AUDIO_IO:
        return MK_MEMORY_BUDGET_PREFER_LOW_LATENCY;
    default:
        return 0u;
    }
}

static uint32_t sys_memory_cache_flags(uint32_t task_class) {
    switch (task_class) {
    case MK_TASK_CLASS_DESKTOP:
    case MK_TASK_CLASS_APP_RUNTIME:
        return MK_MEMORY_BUDGET_PREFER_RESIDENT_CACHE;
    default:
        return 0u;
    }
}

static int sys_boot_trace_is_audio_service_current(void) {
    process_t *current = scheduler_current();

    return current != 0 && current->service_type == MK_SERVICE_AUDIO;
}

static const char *sys_launch_path_basename(const char *path) {
    const char *last = path;

    if (path == 0) {
        return 0;
    }

    while (*path != '\0') {
        if (*path == '/' && path[1] != '\0') {
            last = path + 1;
        }
        ++path;
    }

    return last;
}

static const char *sys_launch_descriptor_arg(const struct mk_launch_descriptor *descriptor,
                                             uint32_t index) {
    uint32_t current = 0u;
    uint32_t offset = 0u;

    if (descriptor == 0 || index >= descriptor->argc) {
        return 0;
    }

    while (current < descriptor->argc && offset < MK_LAUNCH_ARGV_BYTES) {
        uint32_t len = 0u;
        const char *arg = &descriptor->argv_data[offset];

        while ((offset + len) < MK_LAUNCH_ARGV_BYTES && arg[len] != '\0') {
            len += 1u;
        }
        if ((offset + len) >= MK_LAUNCH_ARGV_BYTES) {
            return 0;
        }
        if (current == index) {
            return arg;
        }
        offset += len + 1u;
        current += 1u;
    }

    return 0;
}

static process_t *sys_async_message_target(const struct mk_async_message *message) {
    process_t *destination = 0;

    if (message == 0) {
        return 0;
    }
    if ((message->flags & MK_ASYNC_MESSAGE_TO_PID) != 0u &&
        message->target_pid != 0u) {
        destination = scheduler_find_task_by_pid((int)message->target_pid);
        if (destination != 0) {
            return destination;
        }
    }
    if ((message->flags & MK_ASYNC_MESSAGE_TO_SERVICE) != 0u &&
        message->target_service != MK_SERVICE_NONE) {
        const struct mk_service_record *record =
            mk_service_find_by_type(message->target_service);

        if (record != 0 && record->pid > 0) {
            destination = scheduler_find_task_by_pid(record->pid);
        }
    }
    return destination;
}

static uint32_t sys_gfx_clear(uint32_t color, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_video_service_clear((uint8_t)(color & 0xFFu));
}

static uint32_t sys_gfx_rect(uint32_t x, uint32_t y, uint32_t w,
                             uint32_t h, uint32_t color) {
    return (uint32_t)mk_video_service_rect((int)x,
                                           (int)y,
                                           (int)w,
                                           (int)h,
                                           (uint8_t)(color & 0xFFu));
}

static uint32_t sys_gfx_text(uint32_t x, uint32_t y, uint32_t text_ptr,
                             uint32_t color, uint32_t e) {
    (void)e;
    if (text_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)text_ptr, 256u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_text((int)x,
                                           (int)y,
                                           (uint8_t)(color & 0xFFu),
                                           (const char *)(uintptr_t)text_ptr);
}

static uint32_t sys_gfx_flip(uint32_t a, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_video_service_flip_mode(a);
}

static uint32_t sys_gfx_set_present_policy(uint32_t policy, uint32_t b, uint32_t c,
                                           uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)(mk_video_service_set_present_policy(policy) == 0 ? 0 : -1);
}

static uint32_t sys_gfx_set_present_copy_override(uint32_t kind, uint32_t b, uint32_t c,
                                                  uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)(mk_video_service_set_present_copy_override(kind) == 0 ? 0 : -1);
}

static uint32_t sys_gfx_blit8(uint32_t src_ptr, uint32_t packed_wh, uint32_t dst_x,
                              uint32_t dst_y, uint32_t scale) {
    int src_w = (int)(packed_wh & 0xFFFFu);
    int src_h = (int)((packed_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_blit8((const uint8_t *)(uintptr_t)src_ptr,
                                            src_w,
                                            src_h,
                                            (int)dst_x,
                                            (int)dst_y,
                                            (int)scale);
}

static uint32_t sys_gfx_blit8_present(uint32_t src_ptr, uint32_t packed_wh, uint32_t dst_x,
                                      uint32_t dst_y, uint32_t scale) {
    int src_w = (int)(packed_wh & 0xFFFFu);
    int src_h = (int)((packed_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_blit8_present((const uint8_t *)(uintptr_t)src_ptr,
                                                    src_w,
                                                    src_h,
                                                    (int)dst_x,
                                                    (int)dst_y,
                                                    (int)scale);
}

static uint32_t sys_gfx_blit8_stretch(uint32_t src_ptr, uint32_t packed_src_wh,
                                      uint32_t dst_x, uint32_t dst_y,
                                      uint32_t packed_dst_wh) {
    int src_w = (int)(packed_src_wh & 0xFFFFu);
    int src_h = (int)((packed_src_wh >> 16) & 0xFFFFu);
    int dst_w = (int)(packed_dst_wh & 0xFFFFu);
    int dst_h = (int)((packed_dst_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_blit8_stretch((const uint8_t *)(uintptr_t)src_ptr,
                                                    src_w,
                                                    src_h,
                                                    (int)dst_x,
                                                    (int)dst_y,
                                                    dst_w,
                                                    dst_h);
}

static uint32_t sys_gfx_blit8_stretch_present(uint32_t src_ptr, uint32_t packed_src_wh,
                                              uint32_t dst_x, uint32_t dst_y,
                                              uint32_t packed_dst_wh) {
    int src_w = (int)(packed_src_wh & 0xFFFFu);
    int src_h = (int)((packed_src_wh >> 16) & 0xFFFFu);
    int dst_w = (int)(packed_dst_wh & 0xFFFFu);
    int dst_h = (int)((packed_dst_wh >> 16) & 0xFFFFu);

    if (src_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_blit8_stretch_present((const uint8_t *)(uintptr_t)src_ptr,
                                                            src_w,
                                                            src_h,
                                                            (int)dst_x,
                                                            (int)dst_y,
                                                            dst_w,
                                                            dst_h);
}

static uint32_t sys_gfx_leave(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_video_service_leave_graphics();
}

static uint32_t sys_gfx_set_mode(uint32_t width, uint32_t height, __attribute__((unused)) uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_video_service_set_mode(width, height);
}

static uint32_t sys_gfx_set_palette(uint32_t ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, MK_VIDEO_PALETTE_BYTES)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_set_palette((const uint8_t *)(uintptr_t)ptr);
}

static uint32_t sys_gfx_get_palette(uint32_t ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, MK_VIDEO_PALETTE_BYTES)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_get_palette((uint8_t *)(uintptr_t)ptr);
}

static uint32_t sys_storage_load(uint32_t ptr, uint32_t size, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!g_audio_boot_trace_storage_load && sys_boot_trace_is_audio_service_current()) {
        g_audio_boot_trace_storage_load = 1u;
        kernel_text_puts("audiosvc: syscall storage_load\n");
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_storage_service_load((void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_save(uint32_t ptr, uint32_t size, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!sys_user_ptr_ok((uintptr_t)ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_storage_service_save((const void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_read_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                         uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (sector_count == 0u || sector_count > 65536u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, (size_t)sector_count * 512u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_storage_service_read_sectors(lba, (void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_write_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                          uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (sector_count == 0u || sector_count > 65536u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, (size_t)sector_count * 512u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_storage_service_write_sectors(lba, (const void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_total_sectors(uint32_t a, uint32_t b, uint32_t c,
                                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return mk_storage_service_total_sectors();
}

static uint32_t sys_storage_partition_start_lba(uint32_t a, uint32_t b, uint32_t c,
                                                uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return mk_storage_service_partition_start_lba();
}

static uint32_t sys_storage_backend_load(uint32_t ptr, uint32_t size, uint32_t c,
                                         uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)kernel_storage_load((void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_backend_save(uint32_t ptr, uint32_t size, uint32_t c,
                                         uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)kernel_storage_save((const void *)(uintptr_t)ptr, size);
}

static uint32_t sys_storage_backend_read_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    /* Ceiling BEFORE the *512 product: sector_count>=0x800000 wraps it to
     * 0 in 32-bit and sys_user_ptr_ok(len=0) passes anything (class #3). */
    if (sector_count == 0u || sector_count > 65536u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, (size_t)sector_count * 512u)) {
        return (uint32_t)-1;
    }
    {
        /* P4 adendo 43: THE live host-side PIO chokepoint (the bootstrap
         * storage task calls this per chunk; mk_storage_local_handler is
         * dead code in steady state — probes there were unreachable).
         * Duration in local CPU ticks, 6 budget prints. */
        static uint32_t g_be_dur_budget = 6u;
        uint32_t bcpu = kernel_cpu_index();
        uint32_t bt0 = smp_tick_count(bcpu);
        int64_t brc = kernel_storage_read_sectors(lba, (void *)(uintptr_t)ptr,
                                                  sector_count);
        if (g_be_dur_budget != 0u && sector_count > 1u) {
            g_be_dur_budget -= 1u;
            kernel_debug_printf("storage: pio n=%u dur=%u cpu=%u\n",
                                (unsigned int)sector_count,
                                (unsigned int)(smp_tick_count(bcpu) - bt0),
                                (unsigned int)bcpu);
        }
        return (uint32_t)brc;
    }
}

static uint32_t sys_storage_backend_write_sectors(uint32_t lba, uint32_t ptr, uint32_t sector_count,
                                                  uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    if (sector_count == 0u || sector_count > 65536u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)ptr, (size_t)sector_count * 512u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)kernel_storage_write_sectors(lba, (const void *)(uintptr_t)ptr, sector_count);
}

static uint32_t sys_storage_backend_total_sectors(uint32_t a, uint32_t b, uint32_t c,
                                                  uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    return kernel_storage_total_sectors();
}

static uint32_t sys_storage_backend_partition_start_lba(uint32_t a, uint32_t b, uint32_t c,
                                                        uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_STORAGE)) {
        return (uint32_t)-1;
    }
    return kernel_storage_partition_start_lba();
}

static uint32_t sys_fs_open(uint32_t path_ptr, uint32_t flags, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_filesystem_service_open((const char *)(uintptr_t)path_ptr, (int)flags);
}

static uint32_t sys_fs_read(uint32_t fd, uint32_t buf_ptr, uint32_t count,
                            uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!sys_user_ptr_ok((uintptr_t)buf_ptr, (size_t)count)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_filesystem_service_read((int)fd, (void *)(uintptr_t)buf_ptr, count);
}

static uint32_t sys_fs_write(uint32_t fd, uint32_t buf_ptr, uint32_t count,
                             uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!sys_user_ptr_ok((uintptr_t)buf_ptr, (size_t)count)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_filesystem_service_write((int)fd, (const void *)(uintptr_t)buf_ptr, count);
}

static uint32_t sys_fs_close(uint32_t fd, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_close((int)fd);
}

static uint32_t sys_fs_lseek(uint32_t fd, uint32_t offset, uint32_t whence,
                             uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_filesystem_service_lseek((int)fd, (off_t)(int32_t)offset, (int)whence);
}

static uint32_t sys_fs_stat(uint32_t path_ptr, uint32_t stat_ptr, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u) ||
        !sys_user_ptr_ok((uintptr_t)stat_ptr, sizeof(struct stat))) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_filesystem_service_stat((const char *)(uintptr_t)path_ptr,
                                                (struct stat *)(uintptr_t)stat_ptr);
}

static uint32_t sys_fs_fstat(uint32_t fd, uint32_t stat_ptr, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!sys_user_ptr_ok((uintptr_t)stat_ptr, sizeof(struct stat))) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_filesystem_service_fstat((int)fd,
                                                 (struct stat *)(uintptr_t)stat_ptr);
}

static uint32_t sys_fs_backend_open(uint32_t path_ptr, uint32_t flags, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)open((const char *)(uintptr_t)path_ptr, (int)flags);
}

static uint32_t sys_fs_backend_read(uint32_t fd, uint32_t buf_ptr, uint32_t count,
                                    uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)buf_ptr, (size_t)count)) {
        return (uint32_t)-1;
    }
    return (uint32_t)read((int)fd, (void *)(uintptr_t)buf_ptr, count);
}

static uint32_t sys_fs_backend_write(uint32_t fd, uint32_t buf_ptr, uint32_t count,
                                     uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)buf_ptr, (size_t)count)) {
        return (uint32_t)-1;
    }
    return (uint32_t)write((int)fd, (const void *)(uintptr_t)buf_ptr, count);
}

static uint32_t sys_fs_backend_close(uint32_t fd, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)close((int)fd);
}

static uint32_t sys_fs_backend_lseek(uint32_t fd, uint32_t offset, uint32_t whence,
                                     uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)lseek((int)fd, (off_t)(int32_t)offset, (int)whence);
}

static uint32_t sys_fs_backend_stat(uint32_t path_ptr, uint32_t stat_ptr, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u) ||
        !sys_user_ptr_ok((uintptr_t)stat_ptr, sizeof(struct stat))) {
        return (uint32_t)-1;
    }
    return (uint32_t)stat((const char *)(uintptr_t)path_ptr,
                          (struct stat *)(uintptr_t)stat_ptr);
}

static uint32_t sys_fs_backend_fstat(uint32_t fd, uint32_t stat_ptr, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (!mk_service_backend_bridge_allowed_current(MK_SERVICE_FILESYSTEM)) {
        return (uint32_t)-1;
    }
    return (uint32_t)fstat((int)fd, (struct stat *)(uintptr_t)stat_ptr);
}

static uint32_t sys_input_mouse(uint32_t state_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    struct mouse_state *state;

    (void)b; (void)c; (void)d; (void)e;
    if (state_ptr == 0u) {
        return 0;
    }

    state = (struct mouse_state *)(uintptr_t)state_ptr;
    if (!mk_input_service_poll_mouse(state)) {
        memset(state, 0, sizeof(*state));
        return 0;
    }
    return 1u;
}

static uint32_t sys_input_event(uint32_t event_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    struct input_event *event;

    (void)b; (void)c; (void)d; (void)e;
    if (event_ptr == 0u) {
        return 0u;
    }

    event = (struct input_event *)(uintptr_t)event_ptr;
    return (uint32_t)mk_input_service_next_event(event);
}

static uint32_t sys_network_listen(uint32_t handle, uint32_t backlog, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_network_service_listen((int)handle, (int)backlog);
}

static uint32_t sys_network_accept(uint32_t handle, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_network_service_accept((int)handle);
}

static uint32_t sys_getpid(uint32_t a, uint32_t b, uint32_t c,
                           uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return scheduler_current_pid();
}

static uint32_t sys_launch_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    const struct mk_launch_context *context;
    struct userland_launch_info *out;

    (void)b; (void)c; (void)d; (void)e;
    if (!g_audio_boot_trace_launch_info && sys_boot_trace_is_audio_service_current()) {
        g_audio_boot_trace_launch_info = 1u;
        kernel_text_puts("audiosvc: syscall launch_info\n");
    }
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }

    context = mk_launch_context_current();
    if (context == 0) {
        return (uint32_t)-1;
    }

    out = (struct userland_launch_info *)(uintptr_t)out_ptr;
    memcpy(out, context, sizeof(*out));
    return 0;
}

static uint32_t sys_yield(uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    mk_audio_service_pump_async();
    yield();
    return 0;
}

static uint32_t sys_write_debug(uint32_t a, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (!g_audio_boot_trace_write_debug && sys_boot_trace_is_audio_service_current()) {
        g_audio_boot_trace_write_debug = 1u;
        kernel_text_puts("audiosvc: syscall write_debug\n");
    }
    if (!sys_user_str_ok((uintptr_t)a, 4096u)) {
        return (uint32_t)-1;
    }
    kernel_debug_puts((const char *)(uintptr_t)a);
    return 0;
}

static uint32_t sys_input_key(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_input_service_read_key();
}

/*
 * SYSCALL_INPUT_KEY_WAIT (231, additive — bug #19 item 0.12, 01/11):
 * a = timeout ticks (0 = indefinite). One non-blocking KEY_READ through
 * the input service host, then a blocking wait on the driver mailbox —
 * no busy-poll while idle. Returns the key (>0) or -1. Existing
 * SYSCALL_INPUT_KEY is unchanged for every current caller.
 */
static uint32_t sys_input_key_wait(uint32_t a, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_input_service_read_key_wait(a);
}

static uint32_t sys_text_clear(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    kernel_text_clear();
    return 0;
}

static uint32_t sys_text_move_cursor(uint32_t a, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_text_move_cursor((int32_t)a);
    return 0;
}

static uint32_t sys_text_putc(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    kernel_text_putc((char)(a & 0xFF));
    return 0;
}

static uint32_t sys_text_write(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    int i;
    char buf[128];
    const char *src = (const char *)(uintptr_t)a;

    (void)b; (void)c; (void)d; (void)e;
    if (!sys_user_str_ok((uintptr_t)a, sizeof(buf))) {
        return (uint32_t)-1;
    }
    for (i = 0; i < (int)sizeof(buf) - 1 && src[i] != '\0'; ++i) {
        buf[i] = src[i];
    }
    buf[i] = '\0';
    if (!g_audio_boot_trace_text_write && sys_boot_trace_is_audio_service_current()) {
        g_audio_boot_trace_text_write = 1u;
        kernel_text_puts("audiosvc: syscall text_write\n");
    }
    kernel_text_puts(buf);
    return 0;
}

static uint32_t sys_sleep(uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    mk_audio_service_pump_async();
    __asm__ volatile("hlt");
    return 0;
}

static uint32_t sys_time_ticks(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return kernel_timer_get_ticks();
}

static uint32_t sys_gfx_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_get_info((struct video_mode *)(uintptr_t)out_ptr);
}

static uint32_t sys_gfx_caps(uint32_t out_ptr, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_video_service_get_caps((struct video_capabilities *)(uintptr_t)out_ptr);
}

static uint32_t sys_gfx_bench(uint32_t out_ptr, uint32_t b, uint32_t c,
                              uint32_t d, uint32_t e) {
    struct video_bench_info *out;

    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    out = (struct video_bench_info *)(uintptr_t)out_ptr;
    kernel_video_get_benchmarks(out);
    return 0u;
}

static uint32_t sys_audio_get_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_get_info((struct mk_audio_info *)(uintptr_t)out_ptr);
}

static uint32_t sys_audio_get_status(uint32_t out_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_get_status((struct audio_status *)(uintptr_t)out_ptr);
}

static uint32_t sys_audio_set_params(uint32_t params_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (params_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_set_params((const struct audio_swpar *)(uintptr_t)params_ptr);
}

static uint32_t sys_audio_start(uint32_t a, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_audio_service_start();
}

static uint32_t sys_audio_stop(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_audio_service_stop();
}

static uint32_t sys_audio_write(uint32_t data_ptr, uint32_t size, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)data_ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_write((const void *)(uintptr_t)data_ptr, size);
}

static uint32_t sys_audio_write_async(uint32_t data_ptr, uint32_t size, uint32_t c,
                                      uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)data_ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_write_async((const void *)(uintptr_t)data_ptr, size);
}

static uint32_t sys_audio_play_asset(uint32_t path_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    const char *const argv[2] = {(const char *)(uintptr_t)"audio-asset",
                                 (const char *)(uintptr_t)path_ptr};
    struct mk_launch_descriptor descriptor;
    int result;

    (void)b; (void)c; (void)d; (void)e;
    if (path_ptr == 0u) {
        return (uint32_t)-1;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_USER;
    descriptor.flags = MK_LAUNCH_FLAG_BUILTIN | MK_LAUNCH_FLAG_USER_APP;
    descriptor.task_class = MK_TASK_CLASS_AUDIO_IO;
    descriptor.stack_size = 65536u;

    if (sys_launch_app_copy_argv(argv, 2u, &descriptor) != 0) {
        return (uint32_t)-1;
    }
    descriptor.entry = userland_audio_asset_host_entry;

    result = mk_launch_bootstrap(&descriptor);
    return result > 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_audio_read(uint32_t data_ptr, uint32_t size, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)data_ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_read((void *)(uintptr_t)data_ptr, size);
}

static uint32_t sys_audio_control_info(uint32_t index, uint32_t out_ptr, uint32_t c,
                                       uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_get_control_info(index,
                                                       (struct mk_audio_control_info *)(uintptr_t)out_ptr);
}

static uint32_t sys_audio_mixer_read(uint32_t control_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (control_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_mixer_read((mixer_ctrl_t *)(uintptr_t)control_ptr);
}

static uint32_t sys_audio_mixer_write(uint32_t control_ptr, uint32_t b, uint32_t c,
                                      uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (control_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_mixer_write((const mixer_ctrl_t *)(uintptr_t)control_ptr);
}

static uint32_t sys_network_get_info(uint32_t out_ptr, uint32_t b, uint32_t c,
                                     uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_get_info((struct mk_network_info *)(uintptr_t)out_ptr);
}

static uint32_t sys_network_get_status(uint32_t out_ptr, uint32_t b, uint32_t c,
                                       uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_get_status((struct mk_network_status *)(uintptr_t)out_ptr);
}

static uint32_t sys_network_scan(uint32_t index, uint32_t out_ptr, uint32_t c,
                                 uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (out_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_get_scan(index,
                                                 (struct mk_network_scan_info *)(uintptr_t)out_ptr);
}

static uint32_t sys_network_connect_wifi(uint32_t request_ptr, uint32_t b, uint32_t c,
                                         uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (request_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_connect_wifi(
        (const struct mk_network_connect_request *)(uintptr_t)request_ptr);
}

static uint32_t sys_network_disconnect(uint32_t if_name_ptr, uint32_t b, uint32_t c,
                                       uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (if_name_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)if_name_ptr, 64u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_disconnect((const char *)(uintptr_t)if_name_ptr);
}

static uint32_t sys_network_connect_ethernet(uint32_t if_name_ptr, uint32_t b, uint32_t c,
                                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (if_name_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)if_name_ptr, 64u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_connect_ethernet((const char *)(uintptr_t)if_name_ptr);
}

static uint32_t sys_network_configure_ethernet(uint32_t config_ptr, uint32_t b, uint32_t c,
                                               uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (config_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_configure_ethernet(
        (const struct mk_network_ethernet_config *)(uintptr_t)config_ptr);
}

static uint32_t sys_network_socket(uint32_t domain, uint32_t type, uint32_t protocol,
                                   uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return (uint32_t)mk_network_service_socket(domain, type, protocol);
}

static uint32_t sys_network_bind(uint32_t handle, uint32_t address_ptr, uint32_t address_length,
                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (address_ptr == 0u || address_length == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_bind((int)handle,
                                             (const struct sockaddr *)(uintptr_t)address_ptr,
                                             address_length);
}

static uint32_t sys_network_socket_connect(uint32_t handle, uint32_t address_ptr, uint32_t address_length,
                                           uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (address_ptr == 0u || address_length == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_socket_connect((int)handle,
                                                       (const struct sockaddr *)(uintptr_t)address_ptr,
                                                       address_length);
}

static uint32_t sys_network_send(uint32_t handle, uint32_t data_ptr, uint32_t size,
                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)data_ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_send((int)handle,
                                             (const void *)(uintptr_t)data_ptr,
                                             size);
}

static uint32_t sys_network_recv(uint32_t handle, uint32_t buffer_ptr, uint32_t size,
                                 uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (buffer_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)buffer_ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_recv((int)handle,
                                             (void *)(uintptr_t)buffer_ptr,
                                             size);
}

static uint32_t sys_network_close(uint32_t handle, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_network_service_close((int)handle);
}

static uint32_t sys_network_sendto(uint32_t handle, uint32_t data_ptr, uint32_t size,
                                   uint32_t addr_ptr, uint32_t addrlen) {
    if (data_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)data_ptr, (size_t)size) ||
        (addr_ptr != 0u && !sys_user_ptr_ok((uintptr_t)addr_ptr, 128u))) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_network_service_sendto((int)handle,
                                               (const void *)(uintptr_t)data_ptr,
                                               size,
                                               addr_ptr != 0u ? (const struct sockaddr *)(uintptr_t)addr_ptr : 0,
                                               addrlen);
}

static uint32_t sys_network_recvfrom(uint32_t handle, uint32_t buffer_ptr, uint32_t size,
                                     uint32_t addr_ptr, uint32_t addrlen_ptr) {
    uint32_t addrlen = addrlen_ptr != 0u ? *(const uint32_t *)(uintptr_t)addrlen_ptr : 0u;
    int result;

    if (buffer_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)buffer_ptr, (size_t)size) ||
        (addr_ptr != 0u && !sys_user_ptr_ok((uintptr_t)addr_ptr, 128u)) ||
        (addrlen_ptr != 0u && !sys_user_ptr_ok((uintptr_t)addrlen_ptr, sizeof(uint32_t)))) {
        return (uint32_t)-1;
    }
    result = mk_network_service_recvfrom_addr((int)handle,
                                              (void *)(uintptr_t)buffer_ptr,
                                              size,
                                              addr_ptr != 0u ? (struct sockaddr *)(uintptr_t)addr_ptr : 0,
                                              &addrlen);
    if (result > 0 && addrlen_ptr != 0u) {
        *(uint32_t *)(uintptr_t)addrlen_ptr = addrlen;
    }
    return (uint32_t)result;
}

static uint32_t sys_keyboard_set_layout(uint32_t name_ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)mk_input_service_set_layout((const char*)(uintptr_t)name_ptr);
}

static uint32_t sys_keyboard_get_layout(uint32_t buffer_ptr, uint32_t size, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_input_service_get_layout((char*)(uintptr_t)buffer_ptr, (int)size);
}

static uint32_t sys_keyboard_get_available_layouts(uint32_t buffer_ptr, uint32_t size, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)mk_input_service_get_available_layouts((char*)(uintptr_t)buffer_ptr, (int)size);
}

static uint32_t sys_transfer_size(uint32_t transfer_id, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return mk_transfer_size(transfer_id);
}

static uint32_t sys_transfer_read(uint32_t transfer_id, uint32_t dst_ptr, uint32_t size,
                                  uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (dst_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)dst_ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return mk_transfer_copy_to(transfer_id, (void *)(uintptr_t)dst_ptr, size) == 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_transfer_write(uint32_t transfer_id, uint32_t src_ptr, uint32_t size,
                                   uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (src_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)src_ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    return mk_transfer_copy_from(transfer_id, (const void *)(uintptr_t)src_ptr, size) == 0 ? 0u : (uint32_t)-1;
}

/* TEMP isolation probe (adendo 36): enable #DB record watchpoints from the
 * CALLING task context (kernel stack of the caller) instead of the tick
 * hook, to A/B the QEMU arm-freeze location. Remove once the freeze is
 * located. */
static uint32_t sys_debug_watch(uint32_t on, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    scheduler_db_enable(on);
    kernel_debug_printf("syscall: debug_watch on=%u caller-pid=%d\n",
                        (unsigned int)on,
                        scheduler_current() ? scheduler_current()->pid : -1);
    return 0u;
}

static uint32_t sys_shutdown(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;

    kernel_debug_puts("sys_shutdown: poweroff requested\n");

    /* Common ACPI/QEMU/Bochs poweroff ports. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    __asm__ volatile("cli" : : : "memory");
    for (;;) {
        __asm__ volatile("hlt");
    }

    __builtin_unreachable();
}

static uint32_t sys_service_receive(uint32_t message_ptr, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    process_t *current;

    (void)b; (void)c; (void)d; (void)e;
    if (message_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    if (!sys_user_ptr_ok((uintptr_t)message_ptr, sizeof(struct mk_message))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ipc_receive_wait(current,
                                      (void *)(uintptr_t)message_ptr,
                                      sizeof(struct mk_message));
}

static uint32_t sys_service_send(uint32_t message_ptr, uint32_t b, uint32_t c,
                                 uint32_t d, uint32_t e) {
    const struct mk_message *message;
    process_t *destination;
    int rc;

    (void)b; (void)c; (void)d; (void)e;
    if (message_ptr == 0u) {
        return (uint32_t)-1;
    }

    message = (const struct mk_message *)(uintptr_t)message_ptr;
    if (message->target_pid == 0u) {
        return (uint32_t)-1;
    }

    destination = scheduler_find_task_by_pid((int)message->target_pid);
    if (destination == 0) {
        return (uint32_t)-1;
    }

    rc = ipc_send(destination, message, sizeof(*message));
    return (uint32_t)rc;
}

static uint32_t sys_message_post(uint32_t message_ptr, uint32_t b, uint32_t c,
                                 uint32_t d, uint32_t e) {
    const struct mk_async_message *message;
    struct mk_async_message queued;
    process_t *current;
    process_t *destination;
    int rc;

    (void)b; (void)c; (void)d; (void)e;
    if (message_ptr == 0u) {
        return (uint32_t)-1;
    }

    message = (const struct mk_async_message *)(uintptr_t)message_ptr;
    if (mk_async_message_validate(message) != 0) {
        return (uint32_t)-1;
    }

    destination = sys_async_message_target(message);
    if (destination == 0) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    queued = *message;
    queued.source_pid = current != 0 ? (uint32_t)current->pid : 0u;
    if ((queued.flags & MK_ASYNC_MESSAGE_TO_PID) == 0u) {
        queued.target_pid = (uint32_t)destination->pid;
    }

    rc = ipc_send(destination, &queued, sizeof(queued));
    return (uint32_t)rc;
}

static uint32_t sys_message_receive(uint32_t message_ptr, uint32_t timeout_ticks, uint32_t c,
                                    uint32_t d, uint32_t e) {
    process_t *current;

    (void)c; (void)d; (void)e;
    if (message_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    if (!sys_user_ptr_ok((uintptr_t)message_ptr, sizeof(struct mk_async_message))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ipc_receive_wait_timeout(current,
                                              (void *)(uintptr_t)message_ptr,
                                              sizeof(struct mk_async_message),
                                              timeout_ticks);
}

static uint32_t sys_service_subscribe(uint32_t service_type, uint32_t b, uint32_t c,
                                      uint32_t d, uint32_t e) {
    process_t *current;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || service_type == MK_SERVICE_NONE) {
        return (uint32_t)-1;
    }

    return (uint32_t)mk_service_subscribe(service_type, current);
}

static uint32_t sys_service_event_receive(uint32_t service_type, uint32_t event_ptr,
                                          uint32_t timeout_ticks, uint32_t d, uint32_t e) {
    process_t *current;

    (void)d; (void)e;
    if (service_type == MK_SERVICE_NONE || event_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    if (!sys_user_ptr_ok((uintptr_t)event_ptr, sizeof(struct mk_service_event))) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_service_event_receive(service_type,
                                              current,
                                              (struct mk_service_event *)(uintptr_t)event_ptr,
                                              timeout_ticks);
}

static uint32_t sys_service_pid(uint32_t service_type, uint32_t b, uint32_t c,
                                uint32_t d, uint32_t e) {
    const struct mk_service_record *service;

    (void)b; (void)c; (void)d; (void)e;
    if (service_type == MK_SERVICE_NONE) {
        return 0u;
    }

    service = mk_service_find_by_type(service_type);
    if (service == 0 || service->pid <= 0) {
        return 0u;
    }
    return (uint32_t)service->pid;
}

static uint32_t sys_service_restart(uint32_t service_type, uint32_t b, uint32_t c,
                                    uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (service_type == MK_SERVICE_NONE) {
        return (uint32_t)-1;
    }
    return mk_service_restart(service_type) == 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_service_health(uint32_t health_ptr, uint32_t service_type,
                                   uint32_t c, uint32_t d, uint32_t e) {
    struct mk_service_health_stats health;
    struct mk_service_health_stats *user_health;

    (void)c; (void)d; (void)e;
    if (health_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)health_ptr, sizeof(struct mk_service_health_stats))) {
        return (uint32_t)-1;
    }
    user_health = (struct mk_service_health_stats *)(uintptr_t)health_ptr;
    if (mk_service_query_health(service_type, &health) != 0) {
        return (uint32_t)-1;
    }
    memcpy(user_health, &health, sizeof(health));
    return 0u;
}

static uint32_t sys_audio_event_subscribe(uint32_t a, uint32_t b, uint32_t c,
                                          uint32_t d, uint32_t e) {
    process_t *current;

    (void)a; (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    return (uint32_t)mk_audio_service_subscribe(current);
}

static uint32_t sys_audio_event_receive(uint32_t event_ptr, uint32_t timeout_ticks,
                                        uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;

    (void)c; (void)d; (void)e;
    if (event_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    if (!sys_user_ptr_ok((uintptr_t)event_ptr, sizeof(struct mk_audio_event))) {
        return (uint32_t)-1;
    }
    return (uint32_t)mk_audio_service_event_receive(current,
                                                    (struct mk_audio_event *)(uintptr_t)event_ptr,
                                                    timeout_ticks);
}

static uint32_t sys_task_snapshot(uint32_t summary_ptr, uint32_t entries_ptr, uint32_t max_entries,
                                  uint32_t d, uint32_t e) {
    struct task_snapshot_summary *summary;
    struct task_snapshot_entry *entries;
    uint32_t count;

    (void)d; (void)e;
    if (summary_ptr == 0u) {
        return (uint32_t)-1;
    }

    if (!sys_user_ptr_ok((uintptr_t)summary_ptr, sizeof(struct task_snapshot_summary)) ||
        (entries_ptr != 0u && max_entries > 0u &&
         !sys_user_ptr_ok((uintptr_t)entries_ptr,
                          (size_t)max_entries * sizeof(struct task_snapshot_entry)))) {
        return (uint32_t)-1;
    }
    summary = (struct task_snapshot_summary *)(uintptr_t)summary_ptr;
    entries = entries_ptr != 0u ? (struct task_snapshot_entry *)(uintptr_t)entries_ptr : 0;
    if (max_entries > TASK_SNAPSHOT_MAX) {
        max_entries = TASK_SNAPSHOT_MAX;
    }

    count = scheduler_snapshot(entries, max_entries, summary);
    summary->kernel_heap_used = (uint32_t)kernel_heap_used();
    summary->kernel_heap_free = (uint32_t)kernel_heap_free();
    summary->physmem_total_kb = (uint32_t)(physmem_usable_size() / 1024u);
    summary->physmem_free_kb = (uint32_t)((physmem_free_pages() * PHYSMEM_PAGE_SIZE) / 1024u);

    if (entries != 0) {
        for (uint32_t i = 0u; i < count; ++i) {
            const struct mk_launch_context *context = mk_launch_context_for_pid((int)entries[i].pid);

            if (context != 0) {
                memcpy(entries[i].name, context->name, sizeof(entries[i].name));
                entries[i].flags = context->flags;
                entries[i].service_type = context->service_type;
            }
            mk_service_fill_task_snapshot(&entries[i]);
        }
    }

    return count;
}

static uint32_t sys_memory_status(uint32_t status_ptr, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    struct mk_memory_status *status;
    process_t *current;
    const struct mk_launch_context *context;
    uint32_t total_bytes;
    uint32_t free_bytes;
    uint32_t arena_bytes;
    uint32_t stack_reserve_bytes;
    uint32_t reserved_bytes;

    (void)b;
    (void)c;
    (void)d;
    (void)e;
    if (status_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    context = mk_launch_context_current();
    total_bytes = (uint32_t)physmem_usable_size();
    free_bytes = (uint32_t)(physmem_free_pages() * PHYSMEM_PAGE_SIZE);
    arena_bytes = sys_memory_current_arena_bytes(current, context);
    stack_reserve_bytes = sys_memory_current_stack_reserve_bytes(current, context);
    reserved_bytes = VIBE_APP_ARENA_SIZE + VIBE_APP_DESKTOP_ARENA_SIZE + VIBE_APP_BOOT_ARENA_SIZE;
    if (!sys_user_ptr_ok((uintptr_t)status_ptr, sizeof(struct mk_memory_status))) {
        return (uint32_t)-1;
    }
    status = (struct mk_memory_status *)(uintptr_t)status_ptr;
    memset(status, 0, sizeof(*status));
    status->abi_version = MK_MEMORY_STATUS_ABI_VERSION;
    status->current_pid = current != 0 ? (uint32_t)current->pid : 0u;
    status->current_task_class = current != 0 ? current->task_class : 0u;
    status->pressure_level = sys_memory_pressure_level(total_bytes, free_bytes);
    status->physmem_total_bytes = total_bytes;
    status->physmem_free_bytes = free_bytes;
    status->kernel_heap_used_bytes = (uint32_t)kernel_heap_used();
    status->kernel_heap_free_bytes = (uint32_t)kernel_heap_free();
    status->app_arena_reserved_bytes = reserved_bytes;
    status->current_arena_bytes = arena_bytes;
    status->current_stack_reserve_bytes = stack_reserve_bytes;
    status->current_heap_ceiling_bytes =
        sys_memory_heap_ceiling_bytes(arena_bytes, stack_reserve_bytes);
    return 0u;
}

static uint32_t sys_memory_budget(uint32_t budget_ptr, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    struct mk_memory_budget *budget;
    process_t *current;
    const struct mk_launch_context *context;
    uint32_t total_bytes;
    uint32_t free_bytes;
    uint32_t pressure_level;
    uint32_t arena_bytes;
    uint32_t stack_reserve_bytes;
    uint32_t ceiling_bytes;
    uint32_t floor_bytes = 0u;
    uint32_t working_bytes = 0u;
    uint32_t opportunistic_bytes = 0u;
    uint32_t concurrency_hint = 1u;
    uint32_t reclaim_batch_bytes = 0x00100000u;
    uint32_t flags = 0u;
    uint32_t task_class = 0u;

    (void)b;
    (void)c;
    (void)d;
    (void)e;
    if (budget_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    context = mk_launch_context_current();
    total_bytes = (uint32_t)physmem_usable_size();
    free_bytes = (uint32_t)(physmem_free_pages() * PHYSMEM_PAGE_SIZE);
    pressure_level = sys_memory_pressure_level(total_bytes, free_bytes);
    task_class = current != 0 ? current->task_class : 0u;
    arena_bytes = sys_memory_current_arena_bytes(current, context);
    stack_reserve_bytes = sys_memory_current_stack_reserve_bytes(current, context);
    ceiling_bytes = sys_memory_heap_ceiling_bytes(arena_bytes, stack_reserve_bytes);
    flags |= sys_memory_latency_flags(task_class);
    flags |= sys_memory_cache_flags(task_class);
    if (pressure_level <= MK_MEMORY_PRESSURE_HIGH) {
        flags |= MK_MEMORY_BUDGET_ALLOW_OPPORTUNISTIC;
    }

    switch (task_class) {
    case MK_TASK_CLASS_DESKTOP:
        floor_bytes = sys_memory_min(ceiling_bytes, 0x04000000u);
        if (pressure_level >= MK_MEMORY_PRESSURE_CRITICAL) {
            working_bytes = sys_memory_headroom_budget(total_bytes, ceiling_bytes, 40u);
            opportunistic_bytes = working_bytes;
        } else if (pressure_level >= MK_MEMORY_PRESSURE_HIGH) {
            working_bytes = sys_memory_headroom_budget(total_bytes, ceiling_bytes, 60u);
            opportunistic_bytes = working_bytes;
        } else {
            working_bytes = sys_memory_headroom_budget(total_bytes, ceiling_bytes, 80u);
            opportunistic_bytes = working_bytes;
        }
        concurrency_hint = pressure_level >= MK_MEMORY_PRESSURE_HIGH ? 2u : 3u;
        reclaim_batch_bytes = 0x00800000u;
        break;
    case MK_TASK_CLASS_APP_RUNTIME:
        floor_bytes = sys_memory_min(ceiling_bytes, 0x01000000u);
        if (pressure_level >= MK_MEMORY_PRESSURE_CRITICAL) {
            working_bytes = sys_memory_headroom_budget(total_bytes, ceiling_bytes, 40u);
            opportunistic_bytes = working_bytes;
        } else if (pressure_level >= MK_MEMORY_PRESSURE_HIGH) {
            working_bytes = sys_memory_headroom_budget(total_bytes, ceiling_bytes, 60u);
            opportunistic_bytes = working_bytes;
        } else {
            working_bytes = sys_memory_headroom_budget(total_bytes, ceiling_bytes, 80u);
            opportunistic_bytes = working_bytes;
        }
        concurrency_hint = pressure_level >= MK_MEMORY_PRESSURE_HIGH ? 2u : 4u;
        reclaim_batch_bytes = 0x00400000u;
        break;
    case MK_TASK_CLASS_AUDIO_IO:
    case MK_TASK_CLASS_NETWORK_IO:
    case MK_TASK_CLASS_STORAGE_IO:
    case MK_TASK_CLASS_FILESYSTEM_IO:
    case MK_TASK_CLASS_CONSOLE_IO:
        floor_bytes = sys_memory_min(ceiling_bytes, 0x00400000u);
        working_bytes = sys_memory_min(ceiling_bytes, 0x00800000u);
        opportunistic_bytes = sys_memory_min(ceiling_bytes, 0x01000000u);
        concurrency_hint = 1u;
        reclaim_batch_bytes = 0x00100000u;
        break;
    default:
        floor_bytes = sys_memory_min(ceiling_bytes, 0x00800000u);
        working_bytes = sys_memory_min(ceiling_bytes,
                                       pressure_level >= MK_MEMORY_PRESSURE_HIGH
                                           ? 0x01000000u
                                           : 0x01800000u);
        opportunistic_bytes = pressure_level >= MK_MEMORY_PRESSURE_HIGH
                                   ? working_bytes
                                   : sys_memory_min(ceiling_bytes, 0x02000000u);
        concurrency_hint = pressure_level >= MK_MEMORY_PRESSURE_HIGH ? 1u : 2u;
        reclaim_batch_bytes = 0x00200000u;
        break;
    }

    if (working_bytes < floor_bytes) {
        working_bytes = floor_bytes;
    }
    if (opportunistic_bytes < working_bytes) {
        opportunistic_bytes = working_bytes;
    }

    if (!sys_user_ptr_ok((uintptr_t)budget_ptr, sizeof(struct mk_memory_budget))) {
        return (uint32_t)-1;
    }
    budget = (struct mk_memory_budget *)(uintptr_t)budget_ptr;
    memset(budget, 0, sizeof(*budget));
    budget->abi_version = MK_MEMORY_BUDGET_ABI_VERSION;
    budget->current_pid = current != 0 ? (uint32_t)current->pid : 0u;
    budget->current_task_class = task_class;
    budget->pressure_level = pressure_level;
    budget->flags = flags;
    budget->current_arena_bytes = arena_bytes;
    budget->stack_reserve_bytes = stack_reserve_bytes;
    budget->commit_floor_bytes = floor_bytes;
    budget->working_set_target_bytes = working_bytes;
    budget->opportunistic_target_bytes = opportunistic_bytes;
    budget->absolute_ceiling_bytes = ceiling_bytes;
    budget->concurrency_hint = concurrency_hint;
    budget->reclaim_batch_bytes = reclaim_batch_bytes;
    return 0u;
}

static uint32_t sys_launch_builtin_user(uint32_t target, uint32_t b, uint32_t c,
                                        uint32_t d, uint32_t e) {
    struct mk_launch_descriptor descriptor;
    uint32_t stack_size = 65536u;

    (void)b; (void)c; (void)d; (void)e;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_USER;
    descriptor.flags = MK_LAUNCH_FLAG_BUILTIN;

    switch (target) {
    case USERLAND_BUILTIN_SHELL:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_SHELL;
        descriptor.task_class = MK_TASK_CLASS_SHELL;
        memcpy(descriptor.name, "shell-host", 11u);
        descriptor.entry = userland_shell_host_entry;
        break;
    case USERLAND_BUILTIN_SHELL_SESSION:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_SHELL;
        descriptor.task_class = MK_TASK_CLASS_SHELL;
        memcpy(descriptor.name, "shell", 6u);
        descriptor.entry = userland_shell_session_entry;
        break;
    case USERLAND_BUILTIN_DESKTOP:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_DESKTOP;
        descriptor.task_class = MK_TASK_CLASS_DESKTOP;
        stack_size = 262144u;
        memcpy(descriptor.name, "desktop-host", 13u);
        descriptor.entry = userland_desktop_host_entry;
        break;
    case USERLAND_BUILTIN_STARTX:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_DESKTOP;
        descriptor.task_class = MK_TASK_CLASS_DESKTOP;
        stack_size = 262144u;
        memcpy(descriptor.name, "startx-host", 12u);
        descriptor.entry = userland_startx_host_entry;
        break;
    case USERLAND_BUILTIN_DESKTOP_AUDIO:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_APP;
        descriptor.task_class = MK_TASK_CLASS_AUDIO_IO;
        memcpy(descriptor.name, "audio-host", 11u);
        descriptor.entry = userland_desktop_audio_host_entry;
        break;
    case USERLAND_BUILTIN_BOOT_AUDIO:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_APP;
        descriptor.task_class = MK_TASK_CLASS_AUDIO_IO;
        memcpy(descriptor.name, "boot-audio", 11u);
        descriptor.entry = userland_boot_audio_host_entry;
        break;
    case USERLAND_BUILTIN_DESKTOP_SESSION:
        descriptor.flags |= MK_LAUNCH_FLAG_USER_DESKTOP;
        descriptor.task_class = MK_TASK_CLASS_DESKTOP;
        stack_size = 262144u;
        memcpy(descriptor.name, "desktop", 8u);
        descriptor.entry = userland_desktop_session_entry;
        break;
    default:
        return (uint32_t)-1;
    }

    descriptor.stack_size = stack_size;
    return (uint32_t)mk_launch_bootstrap(&descriptor);
}

static uint32_t sys_task_terminate(uint32_t pid, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e) {
    process_t *current;
    process_t *task;
    const struct mk_launch_context *context;

    current = scheduler_current();
    (void)b; (void)c; (void)d; (void)e;
    if (pid == 0u) {
        if (current == 0) {
            return (uint32_t)-1;
        }
        scheduler_terminate_task(current);
        yield();
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    if (current != 0 && (uint32_t)current->pid == pid) {
        return (uint32_t)-1;
    }

    task = scheduler_find_task_by_pid((int)pid);
    if (task == 0) {
        return (uint32_t)-1;
    }

    context = mk_launch_context_for_pid((int)pid);
    if (context != 0 && (context->flags & MK_LAUNCH_FLAG_CRITICAL) != 0u) {
        return (uint32_t)-1;
    }

    scheduler_terminate_task(task);
    return 0u;
}

static uint32_t sys_task_create(uint32_t entry_ptr, uint32_t arg_ptr, uint32_t stack_size,
                                uint32_t task_class, uint32_t e) {
    process_t *current;
    process_t *child;
    const struct mk_launch_context *context;
    uint32_t effective_task_class;
    uint32_t launch_flags;
    uintptr_t user_stack_top;

    (void)e;
    if (entry_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }
    if (task_class != 0u && task_class > MK_TASK_CLASS_VIDEO_CONTROL) {
        return (uint32_t)-1;
    }

    context = mk_launch_context_current();
    launch_flags = context != 0 ? context->flags : 0u;
    effective_task_class = task_class != 0u ? task_class : current->task_class;
    child = process_create_with_stack((void (*)(void))(uintptr_t)entry_ptr,
                                      current->kind,
                                      current->service_type,
                                      launch_flags,
                                      effective_task_class,
                                      stack_size);
    if (child == 0) {
        return (uint32_t)-1;
    }

    user_stack_top = process_user_stack_top_for_entry((uintptr_t)entry_ptr);
    if (user_stack_top != 0u && process_entry_supports_user_mode((uintptr_t)entry_ptr)) {
        process_setup_initial_user_context_arg(child,
                                               (uintptr_t)entry_ptr,
                                               user_stack_top,
                                               (uintptr_t)arg_ptr);
    } else {
        /*
         * Privilege guard (COMPLETION_ACTION_PLAN P4, class #4). The child's
         * mode used to be DERIVED from the entry address alone: a ring3 host
         * calling with an entry outside every arena window (e.g. a garbage
         * app header from a partial SMP read — observed: EIP sliding through
         * 0x025dabb8, the gap between the compat arenas, at CPL=0 CS=0x08
         * with ESP=0xb) silently produced a RING0 task executing untrusted
         * bytes at kernel privilege — every kernel-structure guard stays
         * silent because nothing kernel-side is touched, only the task's
         * own state. A ring3 caller must NEVER mint a ring0 task. Ring0
         * callers (services, init) keep the derived behavior unchanged.
         */
        if (current->runs_in_user_mode != 0u) {
            static uint32_t priv_reject_budget = 8u;
            if (priv_reject_budget != 0u) {
                priv_reject_budget--;
                kernel_debug_printf("syscall: TASK-CREATE-PRIV-REJECT entry=0x%x caller_pid=%d\n",
                                    (unsigned int)entry_ptr, current->pid);
            }
            kernel_free(child->stack);
            kernel_free(child);
            return (uint32_t)-1;
        }
        process_setup_initial_context_arg(child,
                                          (uintptr_t)entry_ptr,
                                          (uintptr_t)child->stack + child->stack_size,
                                          (uintptr_t)arg_ptr);
    }
    {
        static uint32_t create_trace_budget = 32u;
        if (create_trace_budget != 0u) {
            create_trace_budget--;
            kernel_debug_printf("syscall: TASK-CREATE pid=%d entry=0x%x mode=%s caller_ring3=%d\n",
                                child->pid, (unsigned int)entry_ptr,
                                (user_stack_top != 0u &&
                                 process_entry_supports_user_mode((uintptr_t)entry_ptr))
                                    ? "ring3" : "ring0",
                                (int)(current->runs_in_user_mode != 0u));
        }
    }
    process_set_abi_metadata(child,
                             current->abi_kind,
                             current->abi_version,
                             current->abi_osabi,
                             current->abi_machine,
                             current->image_base,
                             current->image_size,
                             (uintptr_t)entry_ptr);
    child->launch_context_pid = current->launch_context_pid != 0u
                                    ? current->launch_context_pid
                                    : (uint32_t)current->pid;
    scheduler_add_task(child);
    return (uint32_t)child->pid;
}

static int sys_launch_app_copy_name(const char *name, struct mk_launch_descriptor *descriptor) {
    const char *label;
    uint32_t len = 0u;
    uint32_t label_len = 0u;

    if (name == 0 || descriptor == 0) {
        return -1;
    }

    while (len < (MK_LAUNCH_ARGV_BYTES - 1u) && name[len] != '\0') {
        len += 1u;
    }
    if (len == 0u || name[len] != '\0') {
        return -1;
    }

    label = sys_launch_path_basename(name);
    if (label == 0 || label[0] == '\0') {
        label = name;
    }
    while (label[label_len] != '\0' && label_len < (MK_LAUNCH_NAME_MAX - 1u)) {
        descriptor->name[label_len] = label[label_len];
        label_len += 1u;
    }
    if (label_len == 0u) {
        return -1;
    }
    descriptor->name[label_len] = '\0';
    descriptor->argc = 1u;
    memcpy(descriptor->argv_data, name, len);
    descriptor->argv_data[len] = '\0';
    return 0;
}

static int sys_launch_app_copy_argv(const char *const *argv,
                                    uint32_t argc,
                                    struct mk_launch_descriptor *descriptor) {
    uint32_t arg_index;
    uint32_t used = 0u;

    if (argv == 0 || descriptor == 0 || argc == 0u || argc > MK_LAUNCH_ARGC_MAX) {
        return -1;
    }

    for (arg_index = 0; arg_index < argc; ++arg_index) {
        const char *arg = argv[arg_index];
        uint32_t len = 0u;

        if (arg == 0) {
            return -1;
        }
        while (used + len < (MK_LAUNCH_ARGV_BYTES - 1u) && arg[len] != '\0') {
            len += 1u;
        }
        if (len == 0u || arg[len] != '\0' || (used + len + 1u) > MK_LAUNCH_ARGV_BYTES) {
            return -1;
        }

        memcpy(&descriptor->argv_data[used], arg, len);
        descriptor->argv_data[used + len] = '\0';
        if (arg_index == 0u) {
            const char *label = sys_launch_path_basename(arg);
            uint32_t label_len = 0u;

            if (label == 0 || label[0] == '\0') {
                label = arg;
            }
            while (label[label_len] != '\0' && label_len < (MK_LAUNCH_NAME_MAX - 1u)) {
                descriptor->name[label_len] = label[label_len];
                label_len += 1u;
            }
            if (label_len == 0u) {
                return -1;
            }
            descriptor->name[label_len] = '\0';
        }
        used += len + 1u;
    }

    descriptor->argc = argc;
    return 0;
}

static void sys_launch_app_apply_role(struct mk_launch_descriptor *descriptor) {
    process_t *current_process;
    const struct mk_launch_context *current_context;
    const char *subcommand;

    if (descriptor == 0) {
        return;
    }

    if (strcmp(descriptor->name, "startx") == 0) {
        descriptor->flags &= ~MK_LAUNCH_FLAG_USER_APP;
        descriptor->flags |= MK_LAUNCH_FLAG_USER_DESKTOP;
        descriptor->task_class = MK_TASK_CLASS_DESKTOP;
        if (descriptor->stack_size < 262144u) {
            descriptor->stack_size = 262144u;
        }
    }

    current_process = scheduler_current();
    current_context = mk_launch_context_current();
    if ((current_process == 0 ||
         (current_process->task_class != MK_TASK_CLASS_DESKTOP &&
          current_process->task_class != MK_TASK_CLASS_SUPERVISION)) &&
        (current_context == 0 ||
         (current_context->flags & MK_LAUNCH_FLAG_USER_DESKTOP) == 0u)) {
        return;
    }

    subcommand = sys_launch_descriptor_arg(descriptor, 1u);

    if (strcmp(descriptor->name, "audiosvc") == 0) {
        if (subcommand != 0 &&
            (strcmp(subcommand, "play-asset") == 0 ||
             strcmp(subcommand, "apply-settings") == 0 ||
             strcmp(subcommand, "export-state") == 0)) {
            descriptor->task_class = MK_TASK_CLASS_AUDIO_IO;
        }
        return;
    }

    if (strcmp(descriptor->name, "netmgrd") == 0 ||
        strcmp(descriptor->name, "netctl") == 0) {
        if (subcommand != 0 &&
            (strcmp(subcommand, "reconcile") == 0 ||
             strcmp(subcommand, "export-state") == 0 ||
             strcmp(subcommand, "status") == 0 ||
             strcmp(subcommand, "autoconnect") == 0)) {
            descriptor->task_class = MK_TASK_CLASS_NETWORK_IO;
        }
        return;
    }
}

static uint32_t sys_launch_app(uint32_t name_ptr, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    struct mk_launch_descriptor descriptor;

    (void)c; (void)d; (void)e;
    if (name_ptr == 0u) {
        return (uint32_t)-1;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.abi_version = MK_LAUNCH_ABI_VERSION;
    descriptor.kind = MK_LAUNCH_KIND_USER;
    descriptor.stack_size = 65536u;
    descriptor.flags = MK_LAUNCH_FLAG_USER_APP;
    descriptor.task_class = MK_TASK_CLASS_APP_RUNTIME;
    if (b == 0u) {
        if (!sys_user_str_ok((uintptr_t)name_ptr, 64u)) {
            return (uint32_t)-1;
        }
        if (sys_launch_app_copy_name((const char *)(uintptr_t)name_ptr, &descriptor) != 0) {
            return (uint32_t)-1;
        }
    } else {
        if (!sys_user_ptr_ok((uintptr_t)name_ptr, (size_t)b * sizeof(uintptr_t))) {
            return (uint32_t)-1;
        }
        if (sys_launch_app_copy_argv((const char *const *)(uintptr_t)name_ptr, b, &descriptor) != 0) {
            return (uint32_t)-1;
        }
    }
    sys_launch_app_apply_role(&descriptor);
    descriptor.entry = userland_app_runtime_entry;
    return (uint32_t)mk_launch_bootstrap(&descriptor);
}

static uint32_t sys_task_event_subscribe(uint32_t a, uint32_t b, uint32_t c,
                                         uint32_t d, uint32_t e) {
    process_t *current;

    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)(scheduler_task_event_subscribe(current, a, b) == 0 ? 0 : (uint32_t)-1);
}

static uint32_t sys_task_event_receive(uint32_t event_ptr, uint32_t timeout_ticks, uint32_t c,
                                       uint32_t d, uint32_t e) {
    process_t *current;
    struct mk_task_event *event;

    (void)c; (void)d; (void)e;
    if (event_ptr == 0u) {
        return (uint32_t)-1;
    }

    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    if (!sys_user_ptr_ok((uintptr_t)event_ptr, sizeof(struct mk_task_event))) {
        return (uint32_t)-1;
    }
    event = (struct mk_task_event *)(uintptr_t)event_ptr;
    return (uint32_t)(scheduler_task_event_receive(current, event, timeout_ticks) == 0 ? 0 : (uint32_t)-1);
}

static uint32_t sys_video_event_subscribe(uint32_t a, uint32_t b, uint32_t c,
                                          uint32_t d, uint32_t e) {
    process_t *current = scheduler_current();
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;

    if (current == 0) {
        return (uint32_t)-1;
    }
    return mk_video_service_subscribe(current) == 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_video_event_receive(uint32_t event_ptr, uint32_t timeout_ticks,
                                        uint32_t c, uint32_t d, uint32_t e) {
    process_t *current = scheduler_current();
    struct mk_video_event event;

    (void)c;
    (void)d;
    (void)e;
    if (current == 0 || event_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (mk_video_service_event_receive(current,
                                       &event,
                                       timeout_ticks) != 0) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)event_ptr, sizeof(event))) {
        return (uint32_t)-1;
    }
    memcpy((void *)(uintptr_t)event_ptr, &event, sizeof(event));
    return 0u;
}

static uint32_t sys_video_present_submit(uint32_t mode, uint32_t sequence_ptr,
                                         uint32_t c, uint32_t d, uint32_t e) {
    uint32_t sequence = 0u;

    (void)c;
    (void)d;
    (void)e;
    if (mk_video_service_present_submit(mode, &sequence) != 0) {
        return (uint32_t)-1;
    }
    if (sequence_ptr != 0u &&
        !sys_user_ptr_ok((uintptr_t)sequence_ptr, sizeof(uint32_t))) {
        return (uint32_t)-1;
    }
    if (sequence_ptr != 0u) {
        *(uint32_t *)(uintptr_t)sequence_ptr = sequence;
    }
    return 0u;
}

static uint32_t sys_network_event_subscribe(uint32_t a, uint32_t b, uint32_t c,
                                            uint32_t d, uint32_t e) {
    process_t *current = scheduler_current();

    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    if (current == 0) {
        return (uint32_t)-1;
    }
    return mk_network_service_subscribe(current) == 0 ? 0u : (uint32_t)-1;
}

static uint32_t sys_network_event_receive(uint32_t event_ptr, uint32_t timeout_ticks,
                                          uint32_t c, uint32_t d, uint32_t e) {
    process_t *current = scheduler_current();
    struct mk_network_event event;

    (void)c;
    (void)d;
    (void)e;
    if (current == 0 || event_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (mk_network_service_event_receive(current, &event, timeout_ticks) != 0) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)event_ptr, sizeof(event))) {
        return (uint32_t)-1;
    }
    memcpy((void *)(uintptr_t)event_ptr, &event, sizeof(event));
    return 0u;
}

/* ------------------------------------------------------------------ */
/* POSIX process & memory syscalls                                    */
/* ------------------------------------------------------------------ */

static uint32_t sys_fork(uint32_t a, uint32_t b, uint32_t c,
                         uint32_t d, uint32_t e) {
    process_t *current;
    process_t *child;
    uintptr_t stack_offset;
    uint32_t context_size;
    uintptr_t user_stack_top;

    (void)a; (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    child = kernel_malloc(sizeof(process_t));
    if (child == 0) {
        return (uint32_t)-1;
    }
    memcpy(child, current, sizeof(process_t));
    child->pending_stack_free = 0;   /* owned by parent only */

    if (current->stack == 0 || current->stack_size == 0u) {
        kernel_free(child);
        return (uint32_t)-1;
    }

    child->stack = kernel_malloc(current->stack_size);
    if (child->stack == 0) {
        kernel_free(child);
        return (uint32_t)-1;
    }
    memcpy(child->stack, current->stack, current->stack_size);

    stack_offset = (uintptr_t)current->context - (uintptr_t)current->stack;
    child->context = (kernel_trap_frame_t *)((uintptr_t)child->stack + stack_offset);

    if (current->runs_in_user_mode) {
        /* Forking a user-mode task. Create a fresh user-mode context
         * for the child instead of copying the parent's (possibly
         * kernel-mode) saved context from the last timer interrupt. */
        context_size = sizeof(kernel_user_trap_frame_t);
        user_stack_top = current->user_stack_top;
        uintptr_t child_stack_top = 0u;
        if (current->user_stack_top == VIBE_APP_DESKTOP_STACK_TOP)
            child_stack_top = VIBE_APP_DESKTOP_LOAD_ADDR + VIBE_APP_DESKTOP_ARENA_SIZE / 2u;
        else if (current->user_stack_top == VIBE_APP_STACK_TOP)
            child_stack_top = VIBE_APP_LOAD_ADDR + VIBE_APP_ARENA_SIZE / 2u;
        else if (current->user_stack_top == VIBE_APP_BOOT_STACK_TOP)
            child_stack_top = VIBE_APP_BOOT_LOAD_ADDR + VIBE_APP_BOOT_ARENA_SIZE / 2u;
        if (child_stack_top != 0u) {
            uint32_t parent_used = current->user_stack_top - current->user_stack_top / 4u;
            uint32_t max_stack = VIBE_APP_DESKTOP_ARENA_SIZE / 4u;
            if (parent_used > max_stack) parent_used = max_stack;
            uint32_t child_esp = (uint32_t)(child_stack_top - parent_used);
            if (child_esp > VIBE_APP_LOAD_ADDR) {
                memcpy((void *)(uintptr_t)child_esp,
                       (void *)(uintptr_t)(current->user_stack_top - parent_used),
                       parent_used);
                user_stack_top = child_stack_top;
            }
        }
    } else {
        context_size = sizeof(kernel_trap_frame_t);
        user_stack_top = 0u;
    }
    if (stack_offset + context_size > current->stack_size) {
        kernel_free(child->stack);
        kernel_free(child);
        return (uint32_t)-1;
    }

    child->pid = process_alloc_pid();
    child->current_cpu = -1;
    child->preferred_cpu = -1;
    child->last_cpu = -1;
    child->state = PROCESS_READY;
    child->next = NULL;
    child->wait_channel = 0;
    child->wait_deadline = 0u;
    child->wait_result = TASK_WAIT_RESULT_NONE;
    child->wait_event_kind = TASK_WAIT_EVENT_NONE;
    child->wait_event_class = TASK_WAIT_CLASS_NONE;
    child->wait_owner_service = 0u;
    child->wake_boost_budget = 0u;
    child->wait_next = 0;
    child->user_stack_top = user_stack_top;
    child->launch_context_pid = current->launch_context_pid != 0u
                                    ? current->launch_context_pid
                                    : (uint32_t)current->pid;
    child->parent_pid = current->pid;
    child->exit_status = 0;
    child->child_exit_pid = 0;
    child->child_exit_status = 0;

    /* For user-mode forks: copy parent's entire used user stack to
     * the child's new stack area, then set child EIP to fork_child_entry
     * which does jmp *%edi (where edi = return address from parent). */
    if (current->runs_in_user_mode) {
        uint32_t child_eip = d;  /* fork_child_entry from esi */
        uintptr_t child_st = 0u;
        if (current->user_stack_top == VIBE_APP_DESKTOP_STACK_TOP)
            child_st = VIBE_APP_DESKTOP_LOAD_ADDR + VIBE_APP_DESKTOP_ARENA_SIZE / 2u;
        else if (current->user_stack_top == VIBE_APP_STACK_TOP)
            child_st = VIBE_APP_LOAD_ADDR + VIBE_APP_ARENA_SIZE / 2u;
        else if (current->user_stack_top == VIBE_APP_BOOT_STACK_TOP)
            child_st = VIBE_APP_BOOT_LOAD_ADDR + VIBE_APP_BOOT_ARENA_SIZE / 2u;
        if (child_st != 0u) {
            /* Copy parent's user stack (top 8KB) to child's stack */
            uint32_t copy_sz = 8192u;
            uintptr_t parent_bot = current->user_stack_top - copy_sz;
            uintptr_t child_bot = child_st - copy_sz;
            if (parent_bot >= VIBE_APP_LOAD_ADDR && child_bot >= VIBE_APP_LOAD_ADDR) {
                memcpy((void *)child_bot, (void *)parent_bot, copy_sz);
            }
            /* Build user trap frame. Child EIP = fork_child_entry.
             * It jumps to edi which holds the parent's return address.
             * Child ESP = parent's ESP relative position. */
            kernel_user_trap_frame_t *kf;
            uintptr_t kt = (uintptr_t)child->stack + child->stack_size;
            kf = (kernel_user_trap_frame_t *)(kt - sizeof(*kf));
            memset(kf, 0, sizeof(*kf));
            kf->base.esp_dummy = (uint32_t)(uintptr_t)&kf->base.eip;
            kf->base.eip = child_eip;
            kf->base.cs = USER_CS_SELECTOR;
            kf->base.eflags = 0x00000202u;
            kf->base.eax = 0u;
            /* ESP offset: return address at ESP + 20.
             * fork prologue: push ebp, push edi, push esi, push ebx, push ecx
             * EBP = ESP + 16 → return_addr at EBP+4 = ESP + 20.
             * Child ret pops [ESP] = return_address. */
            kf->user_esp = (uint32_t)(child_st - copy_sz + 20u);
            kf->user_ss = USER_DS_SELECTOR;
            child->context = &kf->base;
            child->runs_in_user_mode = 1u;
            child->user_stack_top = child_st;
            user_stack_top = child_st;
        }
    }

    kernel_debug_printf("fork: child_pid=%d eip=%x cs=%x user_esp=%x user_ss=%x runs_user=%d\n",
        child->pid,
        child->context ? child->context->eip : 0,
        child->context ? child->context->cs : 0,
        (current->runs_in_user_mode && child->context) ?
            ((kernel_user_trap_frame_t*)child->context)->user_esp : 0,
        (current->runs_in_user_mode && child->context) ?
            ((kernel_user_trap_frame_t*)child->context)->user_ss : 0,
        current->runs_in_user_mode);

    /*
     * Return value in eax: for the child, set eax to 0 so
     * the frame we copied will return 0 when it resumes.
     */
    if (child->context != 0) {
        child->context->eax = 0u;
    }

    scheduler_add_task(child);
    return (uint32_t)child->pid;
}

static uint32_t sys_execve(uint32_t path_ptr, uint32_t argv_ptr,
                           uint32_t envp_ptr, uint32_t d, uint32_t e) {
    process_t *current;
    process_t *loaded;
    const void *elf_data;
    size_t file_size;
    int fd;
    int read_result;
    struct stat st;
    uintptr_t entry_point;
    uintptr_t user_stack_top;
    uint32_t *arg_slot;
    uint32_t *return_slot;

    (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || path_ptr == 0u) {
        return (uint32_t)-1;
    }

    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u)) {
        return (uint32_t)-1;
    }
    fd = open((const char *)(uintptr_t)path_ptr, 0);
    if (fd < 0) {
        kernel_debug_printf("sys_execve: open failed %s\n",
                            (const char *)(uintptr_t)path_ptr);
        return (uint32_t)-1;
    }

    if (fstat(fd, &st) != 0) {
        close(fd);
        return (uint32_t)-1;
    }
    file_size = (size_t)st.st_size;
    if (file_size == 0u) {
        close(fd);
        return (uint32_t)-1;
    }

    elf_data = kernel_malloc(file_size);
    if (elf_data == 0) {
        close(fd);
        return (uint32_t)-1;
    }

    read_result = read(fd, (void *)elf_data, file_size);
    close(fd);
    if (read_result < 0 || (size_t)read_result != file_size) {
        kernel_free((void *)elf_data);
        return (uint32_t)-1;
    }

    loaded = elf_load(elf_data, file_size);
    kernel_free((void *)elf_data);
    if (loaded == 0) {
        kernel_debug_printf("sys_execve: elf_load failed\n");
        return (uint32_t)-1;
    }

    /*
     * UAF fix (P4 audit): the old code did kernel_free(current->stack)
     * HERE, while sys_execve itself is executing on that very stack and
     * this cpu's TSS esp0 still points to its top. The freed block returns
     * to the allocator, so the ongoing trap frame, later IRQ frames, or a
     * ring3->ring0 entry before the next dispatch all WRITE FREED MEMORY
     * that another owner may already hold. Park the old stack instead:
     * released at the next switch-away/terminate (see pending_stack_free).
     */
    if (current->pending_stack_free != 0) {
        kernel_free(current->pending_stack_free);
        current->pending_stack_free = 0;
    }
    if (current->stack != 0) {
        current->pending_stack_free = current->stack;
    }

    /* Watchpoint bracket: execve legitimately rewrites {stack,stack_size,
     * context} — the DR0/DR1 write trap must classify this as benign (P4
     * adendo 36). */
    scheduler_db_save_begin(current);
    current->stack = loaded->stack;
    current->stack_size = loaded->stack_size;
    scheduler_rec_watch_refresh(current); /* shadow: stack moved too */
    /* Close the esp0 window NOW: any ring3->ring0 trap between this return
     * and the next dispatch must land on the NEW stack, not the parked one. */
    kernel_tss_set_kernel_stack((uintptr_t)current->stack + current->stack_size);
    current->context = loaded->context;
    scheduler_db_save_end(current);
    /* RECORD-WATCH (P4 adendo 33): execve legitimately moves stack+context;
     * refresh the shadow so the watchdog does not report the legit swap. */
    scheduler_rec_watch_refresh(current);
    current->image_base = loaded->image_base;
    current->image_size = loaded->image_size;
    current->entry_point = loaded->entry_point;
    current->abi_kind = loaded->abi_kind;
    current->abi_version = loaded->abi_version;
    current->abi_osabi = loaded->abi_osabi;
    current->abi_machine = loaded->abi_machine;
    entry_point = loaded->entry_point;

    loaded->stack = 0;
    loaded->image_base = 0u;
    loaded->image_size = 0u;
    process_destroy(loaded);

    {
        int sig_idx;
        current->signal_pending = 0u;
        current->signal_blocked = 0u;
        current->alarm_ticks = 0u;
        for (sig_idx = 0; sig_idx < 32; ++sig_idx) {
            current->signal_actions[sig_idx].handler = 0u;
            current->signal_actions[sig_idx].mask = 0u;
            current->signal_actions[sig_idx].flags = 0;
        }
    }

    user_stack_top = process_user_stack_top_for_entry(entry_point);
    if (user_stack_top != 0u && process_entry_supports_user_mode(entry_point)) {
        uintptr_t entry_stack_top;
        uint32_t arg = argv_ptr;

        if (envp_ptr != 0u) {
            arg = envp_ptr;
        }
        entry_stack_top = user_stack_top - (sizeof(uint32_t) * 2u);
        return_slot = (uint32_t *)entry_stack_top;
        arg_slot = return_slot + 1;
        *return_slot = 0u;
        *arg_slot = arg;
        process_setup_initial_user_context_arg(current,
                                                entry_point,
                                                user_stack_top,
                                                (uintptr_t)arg);
    } else {
        process_setup_initial_context(current,
                                       entry_point,
                                       (uintptr_t)current->stack + current->stack_size);
    }

    return 0u;
}

static uint32_t sys_exit(uint32_t status, uint32_t b, uint32_t c,
                         uint32_t d, uint32_t e) {
    process_t *current;
    process_t *parent;
    uint32_t packed_status;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        for (;;) { __asm__ volatile("hlt"); }
    }

    packed_status = (status & 0xFFu) << 8;
    current->exit_status = (int)packed_status;

    if (current->parent_pid > 0) {
        parent = scheduler_find_task_by_pid(current->parent_pid);
        if (parent != 0 &&
            parent->state != PROCESS_TERMINATED) {
            parent->child_exit_pid = current->pid;
            parent->child_exit_status = (int)packed_status;
            if (parent->wait_channel != 0) {
                int woke = 0;
                if (scheduler_complete_wait(parent,
                        TASK_WAIT_RESULT_SIGNALED) == 0) {
                    woke = 1;
                }
                scheduler_wake_task(parent);
                /* wake IPI (adendo 43 gap): without this a CPU idling in
                 * hlt picks the parent up only at its next tick. */
                if (woke != 0) {
                    smp_wake_sleeping_cpus();
                }
            }
        }
    }

    scheduler_terminate_task(current);
    yield();
    for (;;) { __asm__ volatile("hlt"); }
    __builtin_unreachable();
}

static uint32_t sys_waitpid(uint32_t pid, uint32_t status_ptr,
                            uint32_t options, uint32_t d, uint32_t e) {
    process_t *current;
    int target_pid;

    (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    target_pid = (int)pid;

    if (current->child_exit_pid > 0) {
        int cpid = current->child_exit_pid;
        int cstatus = current->child_exit_status;

        if (target_pid <= 0 || target_pid == cpid) {
            current->child_exit_pid = 0;
            current->child_exit_status = 0;
            if (status_ptr != 0u) {
                if (!sys_user_ptr_ok((uintptr_t)status_ptr, sizeof(int))) {
                    return (uint32_t)-1;
                }
                *(int *)(uintptr_t)status_ptr = cstatus;
            }
            return (uint32_t)cpid;
        }
    }

    if (options & 1u) {
        return 0u;
    }

    scheduler_block_current((const void *)(uintptr_t)(uint32_t)current->pid);
    yield();

    if (current->child_exit_pid > 0) {
        int cpid = current->child_exit_pid;
        int cstatus = current->child_exit_status;

        if (target_pid <= 0 || target_pid == cpid) {
            current->child_exit_pid = 0;
            current->child_exit_status = 0;
            if (status_ptr != 0u) {
                if (!sys_user_ptr_ok((uintptr_t)status_ptr, sizeof(int))) {
                    return (uint32_t)-1;
                }
                *(int *)(uintptr_t)status_ptr = cstatus;
            }
            return (uint32_t)cpid;
        }
    }

    return (uint32_t)-1;
}

static uint32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot,
                         uint32_t flags, uint32_t fd) {
    uintptr_t virt;
    uintptr_t aligned_virt;
    uint32_t aligned_len;
    uint32_t num_pages;
    uint32_t pte_flags;
    int is_anon;
    int is_shared;
    int is_fixed;
    int is_populate;
    static uint32_t mmap_next_addr = 0x40000000u;

    if (length == 0u) {
        return (uint32_t)-1;
    }

    is_anon = (flags & 0x1000u) != 0u;
    is_shared = (flags & 0x0001u) != 0u;
    is_fixed = (flags & 0x0010u) != 0u;
    is_populate = (flags & 0x0020u) != 0u;

    aligned_len = (length + 0xFFFu) & ~0xFFFu;
    num_pages = aligned_len / 0x1000u;
    if (num_pages == 0u) {
        return (uint32_t)-1;
    }

    if (is_fixed && addr != 0u) {
        aligned_virt = addr & ~0xFFFu;
        virt = aligned_virt;
    } else if (addr != 0u && !is_fixed) {
        aligned_virt = addr & ~0xFFFu;
        virt = aligned_virt;
    } else {
        virt = mmap_next_addr;
        mmap_next_addr += aligned_len;
    }

    pte_flags = 0u;
    if (prot & 0x1u)
        pte_flags |= 0x001u;
    if (prot & 0x2u)
        pte_flags |= 0x002u;
    if (prot & 0x4u)
        pte_flags |= 0x004u;

    if (!is_anon && fd != 0u) {
        shm_object_t *shm;
        uint32_t idx = fd - 1u;

        if (idx >= MAX_SHM_OBJECTS) {
            return (uint32_t)-1;
        }
        shm = &g_shm_objects[idx];
        if (!shm->used) {
            return (uint32_t)-1;
        }

        if (shm->data == 0) {
            shm->size = aligned_len;
            shm->data = kernel_malloc(aligned_len);
            if (shm->data == 0) {
                return (uint32_t)-1;
            }
            memset(shm->data, 0, aligned_len);
            for (uint32_t i = 0u; i < num_pages; ++i) {
                void *phys = alloc_phys_page();

                if (phys == 0) {
                    for (uint32_t j = 0u; j < i; ++j) {
                        uintptr_t pv = (uintptr_t)shm->data + (uintptr_t)(j * 0x1000u);
                        uintptr_t pp = paging_get_phys_addr(pv);

                        if (pp != 0u)
                            free_phys_page((void *)(pp & ~0xFFFu));
                    }
                    kernel_free(shm->data);
                    shm->data = 0;
                    shm->size = 0u;
                    return (uint32_t)-1;
                }
                if (paging_map_page((uintptr_t)shm->data + (uintptr_t)(i * 0x1000u),
                                    (uintptr_t)phys,
                                    0x007u) != 0) {
                    free_phys_page(phys);
                    for (uint32_t j = 0u; j < i; ++j) {
                        uintptr_t pv = (uintptr_t)shm->data + (uintptr_t)(j * 0x1000u);
                        uintptr_t pp = paging_get_phys_addr(pv);

                        if (pp != 0u)
                            free_phys_page((void *)(pp & ~0xFFFu));
                    }
                    kernel_free(shm->data);
                    shm->data = 0;
                    shm->size = 0u;
                    return (uint32_t)-1;
                }
            }
        }

        for (uint32_t i = 0u; i < num_pages; ++i) {
            uintptr_t shm_virt = (uintptr_t)shm->data + (uintptr_t)(i * 0x1000u);
            uintptr_t phys = paging_get_phys_addr(shm_virt);

            if (phys == 0u) {
                for (uint32_t j = 0u; j < i; ++j) {
                    paging_unmap_page(virt + (uintptr_t)(j * 0x1000u));
                }
                return (uint32_t)-1;
            }
            if (paging_map_page(virt + (uintptr_t)(i * 0x1000u),
                                phys & ~0xFFFu,
                                pte_flags) != 0) {
                for (uint32_t j = 0u; j < i; ++j) {
                    paging_unmap_page(virt + (uintptr_t)(j * 0x1000u));
                }
                return (uint32_t)-1;
            }
        }
    } else {
        for (uint32_t i = 0u; i < num_pages; ++i) {
            void *phys;

            phys = alloc_phys_page();
            if (phys == 0) {
                for (uint32_t j = 0u; j < i; ++j) {
                    paging_unmap_page(virt + (uintptr_t)(j * 0x1000u));
                }
                return (uint32_t)-1;
            }

            if (paging_map_page(virt + (uintptr_t)(i * 0x1000u),
                                (uintptr_t)phys,
                                pte_flags) != 0) {
                free_phys_page(phys);
                for (uint32_t j = 0u; j < i; ++j) {
                    paging_unmap_page(virt + (uintptr_t)(j * 0x1000u));
                }
                return (uint32_t)-1;
            }
        }
    }

    (void)is_shared;
    (void)is_populate;

    return (uint32_t)virt;
}

static uint32_t sys_munmap(uint32_t addr, uint32_t length, uint32_t c,
                           uint32_t d, uint32_t e) {
    uintptr_t aligned_virt;
    uint32_t aligned_len;
    uint32_t num_pages;

    (void)c; (void)d; (void)e;
    if (length == 0u) {
        return (uint32_t)-1;
    }

    aligned_virt = addr & ~0xFFFu;
    aligned_len = (length + 0xFFFu) & ~0xFFFu;
    num_pages = aligned_len / 0x1000u;

    if (!sys_user_range_mapped_ok(aligned_virt, aligned_len)) {
        return (uint32_t)-1;
    }
    for (uint32_t i = 0u; i < num_pages; ++i) {
        uintptr_t virt = aligned_virt + (uintptr_t)(i * 0x1000u);
        uintptr_t phys = paging_get_phys_addr(virt);

        if (phys != 0u) {
            free_phys_page((void *)(phys & ~0xFFFu));
        }
        paging_unmap_page(virt);
    }

    return 0u;
}

static uint32_t sys_sbrk(uint32_t increment, uint32_t b, uint32_t c,
                         uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t old_break;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    old_break = current->program_break;
    if (increment == 0u) {
        return old_break;
    }

    if (current->program_break_start == 0u) {
        current->program_break_start = 0x40000000u;
        current->program_break = current->program_break_start;
        old_break = current->program_break;
    }

    if ((int32_t)increment > 0) {
        uint32_t new_break = current->program_break + increment;
        uint32_t start_page;
        uint32_t end_page;
        uint32_t num_pages;

        start_page = (current->program_break + 0xFFFu) & ~0xFFFu;
        end_page = (new_break + 0xFFFu) & ~0xFFFu;

        if (end_page > start_page) {
            num_pages = (end_page - start_page) / 0x1000u;
            for (uint32_t i = 0u; i < num_pages; ++i) {
                void *phys = alloc_phys_page();
                if (phys == 0) {
                    return (uint32_t)-1;
                }
                if (paging_map_page(start_page + (uintptr_t)(i * 0x1000u),
                                    (uintptr_t)phys,
                                    0x007u) != 0) {
                    free_phys_page(phys);
                    return (uint32_t)-1;
                }
            }
        }
        current->program_break = new_break;
    } else {
        uint32_t shrink = (uint32_t)(-(int32_t)increment);
        uint32_t new_break;

        if (shrink > current->program_break - current->program_break_start) {
            return (uint32_t)-1;
        }

        new_break = current->program_break - shrink;
        {
            uint32_t free_start = (new_break + 0xFFFu) & ~0xFFFu;
            uint32_t free_end = (current->program_break + 0xFFFu) & ~0xFFFu;

            for (uint32_t vaddr = free_start; vaddr < free_end; vaddr += 0x1000u) {
                paging_unmap_page((uintptr_t)vaddr);
            }
        }

        current->program_break = new_break;
    }

    return old_break;
}

static uint32_t sys_pipe(uint32_t pipefd_ptr, uint32_t b, uint32_t c,
                         uint32_t d, uint32_t e) {
    int result;

    (void)b; (void)c; (void)d; (void)e;
    if (pipefd_ptr == 0u) {
        return (uint32_t)-1;
    }

    result = create_pipe();
    if (result < 0) {
        return (uint32_t)-1;
    }

    *(int *)(uintptr_t)pipefd_ptr = result >> 16;
    *((int *)(uintptr_t)pipefd_ptr + 1) = result & 0xFFFF;
    return 0u;
}

static uint32_t sys_kill(uint32_t pid, uint32_t sig, uint32_t c,
                         uint32_t d, uint32_t e) {
    process_t *target;

    (void)c; (void)d; (void)e;
    if (pid == 0u) {
        return (uint32_t)-1;
    }

    target = scheduler_find_task_by_pid((int)pid);
    if (target == 0) {
        return (uint32_t)-1;
    }

    if (sig == 0u) {
        return 0u;
    }

    if (sig == 9u || sig == 15u) {
        scheduler_terminate_task(target);
        return 0u;
    }

    if (sig < 32u) {
        target->signal_pending |= (1u << (sig - 1u));
        if (target->state == PROCESS_BLOCKED) {
            int woke = 0;
            if (scheduler_complete_wait(target,
                    TASK_WAIT_RESULT_SIGNALED) == 0) {
                woke = 1;
            }
            scheduler_wake_task(target);
            if (woke != 0) {
                smp_wake_sleeping_cpus();
            }
        }
    }

    return 0u;
}

static uint32_t sys_getcwd(uint32_t buf_ptr, uint32_t size, uint32_t c,
                           uint32_t d, uint32_t e) {
    process_t *current;
    size_t len;

    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || buf_ptr == 0u || size == 0u) {
        return (uint32_t)-1;
    }

    if (!sys_user_ptr_ok((uintptr_t)buf_ptr, (size_t)size)) {
        return (uint32_t)-1;
    }
    if (current->cwd[0] == '\0') {
        memcpy((void *)(uintptr_t)buf_ptr, "/", 2u);
        return 1u;
    }

    len = strlen(current->cwd);
    if (len >= size) {
        return (uint32_t)-1;
    }

    memcpy((void *)(uintptr_t)buf_ptr, current->cwd, len);
    ((char *)(uintptr_t)buf_ptr)[len] = '\0';
    return (uint32_t)len;
}

static uint32_t sys_chdir(uint32_t path_ptr, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e) {
    process_t *current;
    const char *path;
    size_t len;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || path_ptr == 0u) {
        return (uint32_t)-1;
    }

    if (!sys_user_str_ok((uintptr_t)path_ptr, sizeof(current->cwd))) {
        return (uint32_t)-1;
    }
    path = (const char *)(uintptr_t)path_ptr;
    len = strlen(path);
    if (len == 0u || len >= sizeof(current->cwd)) {
        return (uint32_t)-1;
    }

    memcpy(current->cwd, path, len);
    current->cwd[len] = '\0';
    return 0u;
}

static uint32_t sys_opendir(uint32_t path_ptr, uint32_t b, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (path_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)opendir_fd((const char *)(uintptr_t)path_ptr);
}

static uint32_t sys_readdir(uint32_t dirfd, uint32_t entry_ptr, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (entry_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_ptr_ok((uintptr_t)entry_ptr, sizeof(struct vfs_dirent))) {
        return (uint32_t)-1;
    }
    return (uint32_t)readdir_fd((int)dirfd,
                                 (struct vfs_dirent *)(uintptr_t)entry_ptr);
}

static uint32_t sys_closedir(uint32_t dirfd, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)closedir_fd((int)dirfd);
}

static uint32_t sys_mkdir(uint32_t path_ptr, uint32_t mode, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (path_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u)) {
        return (uint32_t)-1;
    }
    return (uint32_t)mkdir((const char *)(uintptr_t)path_ptr, (mode_t)mode);
}

static uint32_t sys_mprotect(uint32_t addr, uint32_t len, uint32_t prot,
                             uint32_t d, uint32_t e) {
    uintptr_t start;
    uintptr_t end;
    uint32_t pte_flags;

    (void)d; (void)e;
    if (len == 0u) {
        return (uint32_t)-1;
    }

    start = addr & ~0xFFFu;
    end = ((addr + len + 0xFFFu) & ~0xFFFu);
    if (end <= start) {
        return (uint32_t)-1;
    }

    pte_flags = 0u;
    if (prot & 0x1u)
        pte_flags |= 0x001u;
    if (prot & 0x2u)
        pte_flags |= 0x002u;
    if (prot & 0x4u)
        pte_flags |= 0x004u;

    for (uintptr_t v = start; v < end; v += 0x1000u) {
        if (!paging_page_present(v)) {
            continue;
        }
        if (prot == 0u) {
            paging_unmap_page(v);
        } else {
            if (paging_set_page_flags(v, pte_flags) != 0) {
                return (uint32_t)-1;
            }
        }
    }

    return 0u;
}

static uint32_t sys_mlock(uint32_t addr, uint32_t len, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)addr; (void)len; (void)c; (void)d; (void)e;
    return 0u;
}

static uint32_t sys_munlock(uint32_t addr, uint32_t len, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)addr; (void)len; (void)c; (void)d; (void)e;
    return 0u;
}

static uint32_t sys_mlockall(uint32_t flags, uint32_t b, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (flags == 0u) {
        return (uint32_t)-1;
    }
    return 0u;
}

static uint32_t sys_munlockall(uint32_t a, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return 0u;
}

static uint32_t sys_madvise(uint32_t addr, uint32_t length, uint32_t advice,
                            uint32_t d, uint32_t e) {
    uintptr_t start;
    uintptr_t end;

    (void)d; (void)e;
    if (length == 0u) {
        return 0u;
    }

    start = addr & ~0xFFFu;
    end = ((addr + length + 0xFFFu) & ~0xFFFu);

    if (!sys_user_range_mapped_ok(start, (size_t)(end - start))) {
        return (uint32_t)-1;
    }
    if (advice == 4u) {
        for (uintptr_t v = start; v < end; v += 0x1000u) {
            uintptr_t phys = paging_get_phys_addr(v);

            if (phys != 0u) {
                memset((void *)(uintptr_t)v, 0, 0x1000u);
                free_phys_page((void *)(phys & ~0xFFFu));
                paging_unmap_page(v);
            }
        }
    }

    return 0u;
}

static uint32_t sys_shm_open(uint32_t name_ptr, uint32_t oflag, uint32_t c,
                             uint32_t d, uint32_t e) {
    const char *name;
    uint32_t i;
    uint32_t name_len;

    (void)oflag; (void)c; (void)d; (void)e;
    if (name_ptr == 0u) {
        return (uint32_t)-1;
    }

    if (!sys_user_str_ok((uintptr_t)name_ptr, SHM_NAME_MAX)) {
        return (uint32_t)-1;
    }
    name = (const char *)(uintptr_t)name_ptr;
    name_len = 0u;
    while (name_len < SHM_NAME_MAX && name[name_len] != '\0') {
        name_len += 1u;
    }
    if (name_len == 0u || name_len >= SHM_NAME_MAX) {
        return (uint32_t)-1;
    }

    for (i = 0u; i < MAX_SHM_OBJECTS; ++i) {
        if (g_shm_objects[i].used &&
            memcmp(g_shm_objects[i].name, name, name_len + 1u) == 0) {
            return (uint32_t)(i + 1);
        }
    }

    if (g_shm_count >= MAX_SHM_OBJECTS) {
        return (uint32_t)-1;
    }

    for (i = 0u; i < MAX_SHM_OBJECTS; ++i) {
        if (!g_shm_objects[i].used) {
            memcpy(g_shm_objects[i].name, name, name_len + 1u);
            g_shm_objects[i].size = 0u;
            g_shm_objects[i].data = 0;
            g_shm_objects[i].used = 1;
            g_shm_count += 1u;
            return (uint32_t)(i + 1);
        }
    }

    return (uint32_t)-1;
}

static uint32_t sys_shm_unlink(uint32_t name_ptr, uint32_t b, uint32_t c,
                               uint32_t d, uint32_t e) {
    const char *name;
    uint32_t i;
    uint32_t name_len;

    (void)b; (void)c; (void)d; (void)e;
    if (name_ptr == 0u) {
        return (uint32_t)-1;
    }

    if (!sys_user_str_ok((uintptr_t)name_ptr, SHM_NAME_MAX)) {
        return (uint32_t)-1;
    }
    name = (const char *)(uintptr_t)name_ptr;
    name_len = 0u;
    while (name_len < SHM_NAME_MAX && name[name_len] != '\0') {
        name_len += 1u;
    }
    if (name_len == 0u || name_len >= SHM_NAME_MAX) {
        return (uint32_t)-1;
    }

    for (i = 0u; i < MAX_SHM_OBJECTS; ++i) {
        if (g_shm_objects[i].used &&
            memcmp(g_shm_objects[i].name, name, name_len + 1u) == 0) {
            if (g_shm_objects[i].data != 0) {
                uintptr_t base = (uintptr_t)g_shm_objects[i].data;
                size_t pages = (g_shm_objects[i].size + 0xFFFu) / 0x1000u;

                for (size_t p = 0u; p < pages; ++p) {
                    uintptr_t phys = paging_get_phys_addr(base + p * 0x1000u);

                    if (phys != 0u) {
                        free_phys_page((void *)(phys & ~0xFFFu));
                    }
                }
                kernel_free(g_shm_objects[i].data);
            }
            memset(&g_shm_objects[i], 0, sizeof(shm_object_t));
            g_shm_count -= 1u;
            return 0u;
        }
    }

    return (uint32_t)-1;
}

/* extended POSIX file syscalls */

static uint32_t sys_truncate(uint32_t path_ptr, uint32_t length, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (path_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)truncate_path((const char *)(uintptr_t)path_ptr, (off_t)(int32_t)length);
}

static uint32_t sys_ftruncate(uint32_t fd, uint32_t length, uint32_t c,
                              uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return (uint32_t)ftruncate_fd((int)fd, (off_t)(int32_t)length);
}

static uint32_t sys_chmod(uint32_t path_ptr, uint32_t mode, uint32_t c,
                          uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (path_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)chmod_path((const char *)(uintptr_t)path_ptr, (mode_t)mode);
}

static uint32_t sys_chown(uint32_t path_ptr, uint32_t owner, uint32_t group,
                          uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (path_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)chown_path((const char *)(uintptr_t)path_ptr, (uid_t)owner, (gid_t)group);
}

static uint32_t sys_link(uint32_t oldpath_ptr, uint32_t newpath_ptr, uint32_t c,
                         uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (oldpath_ptr == 0u || newpath_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)link_path((const char *)(uintptr_t)oldpath_ptr,
                                (const char *)(uintptr_t)newpath_ptr);
}

static uint32_t sys_symlink(uint32_t target_ptr, uint32_t linkpath_ptr, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (target_ptr == 0u || linkpath_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)symlink_path((const char *)(uintptr_t)target_ptr,
                                   (const char *)(uintptr_t)linkpath_ptr);
}

static uint32_t sys_readlink(uint32_t path_ptr, uint32_t buf_ptr, uint32_t bufsz,
                             uint32_t d, uint32_t e) {
    (void)d; (void)e;
    if (path_ptr == 0u || buf_ptr == 0u || bufsz == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u) ||
        !sys_user_ptr_ok((uintptr_t)buf_ptr, (size_t)bufsz)) {
        return (uint32_t)-1;
    }
    return (uint32_t)readlink_path((const char *)(uintptr_t)path_ptr,
                                    (char *)(uintptr_t)buf_ptr,
                                    (size_t)bufsz);
}

static uint32_t sys_utimensat(uint32_t dirfd, uint32_t path_ptr, uint32_t times_ptr,
                              uint32_t flags, uint32_t e) {
    (void)e;
    if (dirfd != (uint32_t)-100 && dirfd != 0u) {
        return (uint32_t)-1;
    }
    (void)flags;
    if (path_ptr == 0u) {
        return (uint32_t)-1;
    }
    if (!sys_user_str_ok((uintptr_t)path_ptr, 256u) ||
        (times_ptr != 0u && !sys_user_ptr_ok((uintptr_t)times_ptr, sizeof(struct timespec)))) {
        return (uint32_t)-1;
    }
    return (uint32_t)utimens_path((const char *)(uintptr_t)path_ptr,
                                   times_ptr != 0u ? (const struct timespec *)(uintptr_t)times_ptr : 0);
}

static uint32_t sys_mount(uint32_t source_ptr, uint32_t target_ptr, uint32_t fstype_ptr,
                          uint32_t flags, uint32_t data_ptr) {
    if (source_ptr == 0u || target_ptr == 0u || fstype_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)mount_fs((const char *)(uintptr_t)source_ptr,
                               (const char *)(uintptr_t)target_ptr,
                               (const char *)(uintptr_t)fstype_ptr,
                               (unsigned long)flags,
                               (const void *)(uintptr_t)data_ptr);
}

static uint32_t sys_umount(uint32_t target_ptr, uint32_t b, uint32_t c,
                           uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    if (target_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)umount_fs((const char *)(uintptr_t)target_ptr);
}

static uint32_t sys_statvfs(uint32_t path_ptr, uint32_t buf_ptr, uint32_t c,
                            uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (path_ptr == 0u || buf_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)statvfs_path((const char *)(uintptr_t)path_ptr,
                                   (struct statvfs *)(uintptr_t)buf_ptr);
}

static uint32_t sys_realpath(uint32_t path_ptr, uint32_t resolved_ptr, uint32_t c,
                             uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    if (path_ptr == 0u || resolved_ptr == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)realpath_path((const char *)(uintptr_t)path_ptr,
                                    (char *)(uintptr_t)resolved_ptr);
}

/* ------------------------------------------------------------------ */
/* Signal syscalls                                                    */
/* ------------------------------------------------------------------ */

static uint32_t sys_sigaction(uint32_t sig, uint32_t act_ptr,
                              uint32_t oldact_ptr, uint32_t d, uint32_t e) {
    process_t *current;

    (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || sig == 0u || sig >= 32u) {
        return (uint32_t)-1;
    }

    if (oldact_ptr != 0u) {
        uint32_t *dst = (uint32_t *)(uintptr_t)oldact_ptr;
        uint32_t idx = sig;
        dst[0] = current->signal_actions[idx].handler;
        dst[1] = current->signal_actions[idx].mask;
        dst[2] = (uint32_t)current->signal_actions[idx].flags;
    }

    if (act_ptr != 0u) {
        const uint32_t *src = (const uint32_t *)(uintptr_t)act_ptr;
        uint32_t idx = sig;
        current->signal_actions[idx].handler = src[0];
        current->signal_actions[idx].mask = src[1];
        current->signal_actions[idx].flags = (int)src[2];
    }

    return 0u;
}

static uint32_t sys_sigprocmask(uint32_t how, uint32_t set_ptr,
                                uint32_t oldset_ptr, uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t set;

    (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return (uint32_t)-1;
    }

    if (oldset_ptr != 0u) {
        *(uint32_t *)(uintptr_t)oldset_ptr = current->signal_blocked;
    }

    if (set_ptr == 0u) {
        return 0u;
    }

    set = *(const uint32_t *)(uintptr_t)set_ptr;
    switch (how) {
    case 1u:
        current->signal_blocked |= set;
        break;
    case 2u:
        current->signal_blocked &= ~set;
        break;
    case 3u:
        current->signal_blocked = set;
        break;
    default:
        return (uint32_t)-1;
    }

    return 0u;
}

static uint32_t sys_sigsuspend(uint32_t mask_ptr, uint32_t b,
                               uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t saved_mask;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || mask_ptr == 0u) {
        return (uint32_t)-1;
    }

    saved_mask = current->signal_blocked;
    current->signal_blocked = *(const uint32_t *)(uintptr_t)mask_ptr;

    scheduler_block_current((const void *)(uintptr_t)1u);
    yield();

    current->signal_blocked = saved_mask;
    return (uint32_t)-1;
}

static uint32_t sys_sigpending(uint32_t set_ptr, uint32_t b,
                               uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || set_ptr == 0u) {
        return (uint32_t)-1;
    }

    *(uint32_t *)(uintptr_t)set_ptr = current->signal_pending;
    current->signal_pending = 0u;
    return 0u;
}

static uint32_t sys_alarm(uint32_t seconds, uint32_t b,
                          uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t previous;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        return 0u;
    }

    previous = current->alarm_ticks;
    current->alarm_ticks = seconds;
    return previous;
}

static uint32_t sys_setitimer(uint32_t which, uint32_t value_ptr,
                              uint32_t ovalue_ptr, uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t it_value_sec;
    uint32_t it_value_usec;

    (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || value_ptr == 0u) {
        return (uint32_t)-1;
    }

    it_value_sec = *(const uint32_t *)((uintptr_t)value_ptr + 8u);
    it_value_usec = *(const uint32_t *)((uintptr_t)value_ptr + 12u);

    if (ovalue_ptr != 0u) {
        uint32_t *dst = (uint32_t *)(uintptr_t)ovalue_ptr;
        dst[0] = 0u;
        dst[1] = 0u;
        dst[2] = 0u;
        dst[3] = 0u;
    }

    if (which == 0u && it_value_sec == 0u && it_value_usec == 0u) {
        current->alarm_ticks = 0u;
    } else if (which == 0u) {
        uint32_t total_sec = it_value_sec;
        if (it_value_usec > 0u) {
            total_sec += 1u;
        }
        current->alarm_ticks = total_sec;
    }

    return 0u;
}

static uint32_t sys_getitimer(uint32_t which, uint32_t value_ptr,
                              uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;

    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || value_ptr == 0u) {
        return (uint32_t)-1;
    }

    if (which == 0u) {
        uint32_t *dst = (uint32_t *)(uintptr_t)value_ptr;
        dst[0] = 0u;
        dst[1] = 0u;
        dst[2] = current->alarm_ticks;
        dst[3] = 0u;
    } else {
        uint32_t *dst = (uint32_t *)(uintptr_t)value_ptr;
        dst[0] = 0u;
        dst[1] = 0u;
        dst[2] = 0u;
        dst[3] = 0u;
    }

    return 0u;
}

static uint32_t sys_sigwait(uint32_t set_ptr, uint32_t sig_ptr,
                            uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t set;
    int i;

    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || set_ptr == 0u || sig_ptr == 0u) {
        return (uint32_t)-1;
    }

    set = *(const uint32_t *)(uintptr_t)set_ptr;
    for (i = 1; i < 32; ++i) {
        uint32_t bit = 1u << (i - 1u);
        if ((set & bit) != 0u && (current->signal_pending & bit) != 0u) {
            current->signal_pending &= ~bit;
            *(int *)(uintptr_t)sig_ptr = i;
            return 0u;
        }
    }

    scheduler_block_current((const void *)(uintptr_t)2u);
    yield();

    for (i = 1; i < 32; ++i) {
        uint32_t bit = 1u << (i - 1u);
        if ((set & bit) != 0u && (current->signal_pending & bit) != 0u) {
            current->signal_pending &= ~bit;
            *(int *)(uintptr_t)sig_ptr = i;
            return 0u;
        }
    }

    return (uint32_t)-1;
}

static uint32_t sys_sigwaitinfo(uint32_t set_ptr, uint32_t info_ptr,
                                uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t set;
    int i;

    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || set_ptr == 0u) {
        return (uint32_t)-1;
    }

    set = *(const uint32_t *)(uintptr_t)set_ptr;
    for (i = 1; i < 32; ++i) {
        uint32_t bit = 1u << (i - 1u);
        if ((set & bit) != 0u && (current->signal_pending & bit) != 0u) {
            current->signal_pending &= ~bit;
            if (info_ptr != 0u) {
                uint32_t *info = (uint32_t *)(uintptr_t)info_ptr;
                info[0] = (uint32_t)i;
                info[1] = 0u;
                info[2] = 0u;
            }
            return (uint32_t)i;
        }
    }

    scheduler_block_current((const void *)(uintptr_t)3u);
    yield();

    for (i = 1; i < 32; ++i) {
        uint32_t bit = 1u << (i - 1u);
        if ((set & bit) != 0u && (current->signal_pending & bit) != 0u) {
            current->signal_pending &= ~bit;
            if (info_ptr != 0u) {
                uint32_t *info = (uint32_t *)(uintptr_t)info_ptr;
                info[0] = (uint32_t)i;
                info[1] = 0u;
                info[2] = 0u;
            }
            return (uint32_t)i;
        }
    }

    return (uint32_t)-1;
}

/* end of signal syscalls */

/* end of POSIX syscalls */

/* ------------------------------------------------------------------ */
/* Thread syscalls                                                    */
/* ------------------------------------------------------------------ */

static uint32_t sys_thread_create(uint32_t entry_ptr, uint32_t arg_ptr, uint32_t stack_size, uint32_t d, uint32_t e) {
    process_t *current;
    process_t *thread;
    uint32_t normalized_stack;
    uintptr_t stack_top;

    (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || entry_ptr == 0u) {
        return (uint32_t)-1;
    }
    thread = kernel_malloc(sizeof(process_t));
    if (thread == 0) {
        return (uint32_t)-1;
    }
    memcpy(thread, current, sizeof(process_t));
    thread->pending_stack_free = 0;  /* owned by the task that execv/created it */
    normalized_stack = stack_size;
    if (normalized_stack == 0u) {
        normalized_stack = 4096u;
    }
    if (normalized_stack < 1024u) {
        normalized_stack = 1024u;
    }
    normalized_stack = (normalized_stack + 15u) & ~15u;
    thread->stack = kernel_malloc(normalized_stack);
    if (thread->stack == 0) {
        kernel_free(thread);
        return (uint32_t)-1;
    }
    thread->stack_size = normalized_stack;
    thread->pid = process_alloc_pid();
    thread->current_cpu = -1;
    thread->preferred_cpu = -1;
    thread->last_cpu = -1;
    thread->state = PROCESS_READY;
    thread->next = 0;
    thread->wait_channel = 0;
    thread->wait_deadline = 0u;
    thread->wait_result = TASK_WAIT_RESULT_NONE;
    thread->wait_event_kind = TASK_WAIT_EVENT_NONE;
    thread->wait_event_class = TASK_WAIT_CLASS_NONE;
    thread->wait_owner_service = 0u;
    thread->wake_boost_budget = 0u;
    thread->wait_next = 0;
    thread->context_switches = 0u;
    thread->runtime_ticks = 0u;
    thread->last_start_tick = 0u;
    thread->user_stack_top = 0u;
    thread->runs_in_user_mode = 0u;
    thread->thread_retval = 0;
    thread->thread_detached = 0;
    thread->thread_finished = 0;
    if (current->thread_group_id == 0u) {
        thread->thread_group_id = (unsigned int)current->pid;
    } else {
        thread->thread_group_id = current->thread_group_id;
    }
    kernel_waitable_init(&thread->thread_join_waitable);
    stack_top = (uintptr_t)thread->stack + normalized_stack;
    process_setup_initial_context_arg(thread, (uintptr_t)entry_ptr, stack_top, (uintptr_t)arg_ptr);
    scheduler_add_task(thread);
    return (uint32_t)thread->pid;
}

static uint32_t sys_thread_exit(uint32_t retval_ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) {
        for (;;) { __asm__ volatile("hlt"); }
    }
    current->thread_retval = (void *)(uintptr_t)retval_ptr;
    current->thread_finished = 1;
    kernel_waitable_signal(&current->thread_join_waitable, 1u);
    scheduler_terminate_task(current);
    yield();
    for (;;) { __asm__ volatile("hlt"); }
    __builtin_unreachable();
}

static uint32_t sys_thread_join(uint32_t tid, uint32_t retval_ptr, uint32_t c, uint32_t d, uint32_t e) {
    process_t *target;

    (void)c; (void)d; (void)e;
    if (tid == 0u) {
        return (uint32_t)-1;
    }
    target = scheduler_find_task_by_pid((int)tid);
    if (target == 0) {
        return (uint32_t)-1;
    }
    while (!target->thread_finished) {
        if (kernel_waitable_wait(&target->thread_join_waitable) < 0) {
            if (target->thread_finished) {
                break;
            }
            return (uint32_t)-1;
        }
    }
    if (retval_ptr != 0u) {
        void **user_retval = (void **)(uintptr_t)retval_ptr;

        *user_retval = target->thread_retval;
    }
    return 0u;
}

static uint32_t sys_thread_getspecific(uint32_t key, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;

    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || key >= PTHREAD_KEYS_MAX) {
        return 0u;
    }
    return (uint32_t)(uintptr_t)current->tls_values[key];
}

static uint32_t sys_thread_setspecific(uint32_t key, uint32_t value, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;

    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || key >= PTHREAD_KEYS_MAX) {
        return (uint32_t)-1;
    }
    current->tls_values[key] = (void *)(uintptr_t)value;
    return 0u;
}

static uint32_t sys_waitable_init(uint32_t waitable_ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    kernel_waitable_t *waitable;

    (void)b; (void)c; (void)d; (void)e;
    if (waitable_ptr == 0u) {
        return (uint32_t)-1;
    }
    waitable = (kernel_waitable_t *)(uintptr_t)waitable_ptr;
    kernel_waitable_init(waitable);
    return 0u;
}

static uint32_t sys_waitable_wait(uint32_t waitable_ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    kernel_waitable_t *waitable;

    (void)b; (void)c; (void)d; (void)e;
    if (waitable_ptr == 0u) {
        return (uint32_t)-1;
    }
    waitable = (kernel_waitable_t *)(uintptr_t)waitable_ptr;
    if (kernel_waitable_wait(waitable) < 0) {
        return (uint32_t)-1;
    }
    return 0u;
}

static uint32_t sys_waitable_wake(uint32_t waitable_ptr, uint32_t count, uint32_t c, uint32_t d, uint32_t e) {
    kernel_waitable_t *waitable;

    (void)c; (void)d; (void)e;
    if (waitable_ptr == 0u || count == 0u) {
        return (uint32_t)-1;
    }
    waitable = (kernel_waitable_t *)(uintptr_t)waitable_ptr;
    kernel_waitable_signal(waitable, count);
    return 0u;
}

static uint32_t sys_thread_detach(uint32_t tid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *target;

    (void)b; (void)c; (void)d; (void)e;
    if (tid == 0u) {
        return (uint32_t)-1;
    }
    target = scheduler_find_task_by_pid((int)tid);
    if (target == 0) {
        return (uint32_t)-1;
    }
    target->thread_detached = 1;
    if (target->thread_finished) {
        kernel_waitable_signal(&target->thread_join_waitable, 1u);
    }
    return 0u;
}

/* ------------------------------------------------------------------ */
/* POSIX process groups, users, priority, resource limit syscalls     */
/* ------------------------------------------------------------------ */

static uint32_t sys_setpgid(uint32_t pid, uint32_t pgid, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    process_t *target;
    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (pid == 0u) pid = (uint32_t)current->pid;
    target = scheduler_find_task_by_pid((int)pid);
    if (target == 0) return (uint32_t)-1;
    if (pgid == 0u) pgid = pid;
    target->pgid = (int)pgid;
    return 0u;
}

static uint32_t sys_getpgid(uint32_t pid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    process_t *target;
    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (pid == 0u) return (uint32_t)current->pgid;
    target = scheduler_find_task_by_pid((int)pid);
    if (target == 0) return (uint32_t)-1;
    return (uint32_t)target->pgid;
}

static uint32_t sys_setsid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)a; (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (current->pgid == current->pid) return (uint32_t)-1;
    current->pgid = current->pid;
    current->sid = current->pid;
    return (uint32_t)current->sid;
}

static uint32_t sys_getsid(uint32_t pid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    process_t *target;
    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (pid == 0u) return (uint32_t)current->sid;
    target = scheduler_find_task_by_pid((int)pid);
    if (target == 0) return (uint32_t)-1;
    return (uint32_t)target->sid;
}

static uint32_t sys_setuid(uint32_t uid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (current->euid == 0u) {
        current->uid = (uid_t)uid;
        current->euid = (uid_t)uid;
        current->saved_uid = (uid_t)uid;
    } else if (uid == (uint32_t)current->uid || uid == (uint32_t)current->euid || uid == (uint32_t)current->saved_uid) {
        current->euid = (uid_t)uid;
    } else {
        return (uint32_t)-1;
    }
    return 0u;
}

static uint32_t sys_getuid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)a; (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    return (uint32_t)current->uid;
}

static uint32_t sys_seteuid(uint32_t uid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (current->euid == 0u) {
        current->euid = (uid_t)uid;
    } else if (uid == (uint32_t)current->uid || uid == (uint32_t)current->euid || uid == (uint32_t)current->saved_uid) {
        current->euid = (uid_t)uid;
    } else {
        return (uint32_t)-1;
    }
    return 0u;
}

static uint32_t sys_geteuid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)a; (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    return (uint32_t)current->euid;
}

static uint32_t sys_setgid(uint32_t gid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (current->egid == 0u) {
        current->gid = (gid_t)gid;
        current->egid = (gid_t)gid;
        current->saved_gid = (gid_t)gid;
    } else if (gid == (uint32_t)current->gid || gid == (uint32_t)current->egid || gid == (uint32_t)current->saved_gid) {
        current->egid = (gid_t)gid;
    } else {
        return (uint32_t)-1;
    }
    return 0u;
}

static uint32_t sys_getgid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)a; (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    return (uint32_t)current->gid;
}

static uint32_t sys_setegid(uint32_t gid, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (current->egid == 0u) {
        current->egid = (gid_t)gid;
    } else if (gid == (uint32_t)current->gid || gid == (uint32_t)current->egid || gid == (uint32_t)current->saved_gid) {
        current->egid = (gid_t)gid;
    } else {
        return (uint32_t)-1;
    }
    return 0u;
}

static uint32_t sys_getegid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    (void)a; (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    return (uint32_t)current->egid;
}

static uint32_t sys_nice(uint32_t inc, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    int new_nice;
    (void)b; (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    new_nice = current->nice_value + (int)inc;
    if (new_nice < -20) new_nice = -20;
    if (new_nice > 19) new_nice = 19;
    current->nice_value = new_nice;
    return (uint32_t)(new_nice + 20);
}

static uint32_t sys_getpriority(uint32_t which, uint32_t who, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    process_t *task;
    int best = 20;
    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (which == PRIO_PROCESS) {
        if (who == 0u) who = (uint32_t)current->pid;
        task = scheduler_find_task_by_pid((int)who);
        if (task == 0) return (uint32_t)-1;
        return (uint32_t)(task->nice_value + 20);
    }
    best = 20;
    for (task = (process_t *)0; ;) {
        task = scheduler_find_task_by_pid(task != 0 ? task->pid + 1 : 1);
        if (task == 0) break;
        if (which == PRIO_USER && (uint32_t)task->uid != who && who != 0u) continue;
        if (task->nice_value < best) best = task->nice_value;
    }
    return (uint32_t)(best + 20);
}

static uint32_t sys_setpriority(uint32_t which, uint32_t who, uint32_t value, uint32_t d, uint32_t e) {
    process_t *current;
    process_t *task;
    int nv;
    (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    nv = (int)value;
    if (nv < -20) nv = -20;
    if (nv > 19) nv = 19;
    if (which == PRIO_PROCESS) {
        if (who == 0u) who = (uint32_t)current->pid;
        task = scheduler_find_task_by_pid((int)who);
        if (task == 0) return (uint32_t)-1;
        task->nice_value = nv;
        return 0u;
    }
    for (task = (process_t *)0; ;) {
        task = scheduler_find_task_by_pid(task != 0 ? task->pid + 1 : 1);
        if (task == 0) break;
        if (which == PRIO_USER && (uint32_t)task->uid != who && who != 0u) continue;
        task->nice_value = nv;
    }
    return 0u;
}

static uint32_t sys_getrlimit(uint32_t resource, uint32_t rlim_ptr, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    struct rlimit *rlim;
    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || rlim_ptr == 0u || resource >= RLIMIT_NLIMITS) return (uint32_t)-1;
    rlim = (struct rlimit *)(uintptr_t)rlim_ptr;
    rlim->rlim_cur = current->rlimits[resource].rlim_cur;
    rlim->rlim_max = current->rlimits[resource].rlim_max;
    return 0u;
}

static uint32_t sys_setrlimit(uint32_t resource, uint32_t rlim_ptr, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    const struct rlimit *rlim;
    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || rlim_ptr == 0u || resource >= RLIMIT_NLIMITS) return (uint32_t)-1;
    rlim = (const struct rlimit *)(uintptr_t)rlim_ptr;
    if (rlim->rlim_cur > rlim->rlim_max) return (uint32_t)-1;
    if (rlim->rlim_max > current->rlimits[resource].rlim_max && current->euid != 0u) return (uint32_t)-1;
    current->rlimits[resource].rlim_cur = rlim->rlim_cur;
    current->rlimits[resource].rlim_max = rlim->rlim_max;
    return 0u;
}

static uint32_t sys_getgroups(uint32_t size, uint32_t list_ptr, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t i;
    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (list_ptr == 0u && size != 0u) return (uint32_t)-1;
    if (list_ptr != 0u && size < (uint32_t)current->ngroups) return (uint32_t)-1;
    if (list_ptr != 0u) {
        for (i = 0u; i < (uint32_t)current->ngroups && i < size; ++i) {
            ((gid_t *)(uintptr_t)list_ptr)[i] = current->groups[i];
        }
    }
    return (uint32_t)current->ngroups;
}

static uint32_t sys_setgroups(uint32_t size, uint32_t list_ptr, uint32_t c, uint32_t d, uint32_t e) {
    process_t *current;
    uint32_t i;
    (void)c; (void)d; (void)e;
    current = scheduler_current();
    if (current == 0) return (uint32_t)-1;
    if (list_ptr == 0u) return (uint32_t)-1;
    if (size > 16u) return (uint32_t)-1;
    if (current->euid != 0u) return (uint32_t)-1;
    current->ngroups = (int)size;
    for (i = 0u; i < size; ++i) {
        current->groups[i] = ((const gid_t *)(uintptr_t)list_ptr)[i];
    }
    return 0u;
}

/* end of POSIX security syscalls */

/* ------------------------------------------------------------------ */
/* POSIX networking & time syscall stubs                              */
/* ------------------------------------------------------------------ */

static uint32_t sys_clock_gettime(uint32_t clk_id, uint32_t ts_ptr, uint32_t c,
                                   uint32_t d, uint32_t e) {
    process_t *current;
    struct timespec *ts;
    uint32_t ticks, hz;

    (void)c; (void)d; (void)e;
    if (ts_ptr == 0u) return (uint32_t)-1;
    ts = (struct timespec *)(uintptr_t)ts_ptr;
    current = scheduler_current();
    ticks = kernel_timer_get_ticks();
    hz = 1000u;
    if (clk_id == 0u || clk_id == 1u) {
        ts->tv_sec = (time_t)(ticks / hz);
        ts->tv_nsec = (long)(ticks % hz) * 1000000l;
        return 0u;
    }
    if (clk_id == 2u || clk_id == 3u) {
        if (current != 0) {
            uint32_t rt = current->runtime_ticks;
            ts->tv_sec = (time_t)(rt / hz);
            ts->tv_nsec = (long)(rt % hz) * 1000000l;
            return 0u;
        }
        return (uint32_t)-1;
    }
    return (uint32_t)-1;
}

static uint32_t sys_clock_settime(uint32_t clk_id, uint32_t ts_ptr, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)clk_id; (void)ts_ptr; (void)c; (void)d; (void)e;
    return (uint32_t)-1;
}

static uint32_t sys_nanosleep(uint32_t req_ptr, uint32_t rem_ptr, uint32_t c,
                               uint32_t d, uint32_t e) {
    struct timespec *req;
    uint32_t total_ms;

    (void)c; (void)d; (void)e;
    if (req_ptr == 0u) return (uint32_t)-1;
    req = (struct timespec *)(uintptr_t)req_ptr;
    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000l) {
        return (uint32_t)-1;
    }
    total_ms = (uint32_t)req->tv_sec * 1000u;
    total_ms += (uint32_t)(req->tv_nsec / 1000000l);
    if (req->tv_nsec > 0 && (req->tv_nsec % 1000000l) != 0) {
        total_ms += 1u;
    }
    if (total_ms > 0u) {
        {
            uint32_t deadline = kernel_timer_get_ticks() + total_ms;
            while ((int32_t)(kernel_timer_get_ticks() - deadline) < 0) {
                (void)sys_sleep(0u, 0u, 0u, 0u, 0u);
            }
        }
    }
    if (rem_ptr != 0u) {
        struct timespec *rem = (struct timespec *)(uintptr_t)rem_ptr;
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0u;
}

static uint32_t sys_timer_create(uint32_t clock_id, uint32_t evp_ptr, uint32_t timer_id_ptr,
                                  uint32_t d, uint32_t e) {
    process_t *current;
    int i;

    (void)d; (void)e;
    current = scheduler_current();
    if (current == 0 || timer_id_ptr == 0u) return (uint32_t)-1;
    for (i = 0; i < POSIX_TIMER_MAX; ++i) {
        if (!current->posix_timers[i].used) {
            current->posix_timers[i].used = 1;
            current->posix_timers[i].clock_id = (clockid_t)clock_id;
            current->posix_timers[i].armed = 0;
            current->posix_timers[i].timer_id = (timer_t)(i + 1);
            if (evp_ptr != 0u) {
                current->posix_timers[i].evp = *(const struct sigevent *)(uintptr_t)evp_ptr;
            }
            *(timer_t *)(uintptr_t)timer_id_ptr = current->posix_timers[i].timer_id;
            return 0u;
        }
    }
    return (uint32_t)-1;
}

static uint32_t sys_timer_settime(uint32_t timer_id, uint32_t flags, uint32_t value_ptr,
                                   uint32_t ovalue_ptr, uint32_t e) {
    process_t *current;
    int idx = (int)timer_id - 1;

    (void)e; (void)flags;
    current = scheduler_current();
    if (current == 0 || idx < 0 || idx >= POSIX_TIMER_MAX) return (uint32_t)-1;
    if (!current->posix_timers[idx].used) return (uint32_t)-1;
    if (ovalue_ptr != 0u) {
        struct itimerspec *old = (struct itimerspec *)(uintptr_t)ovalue_ptr;
        old->it_value.tv_sec = 0;
        old->it_value.tv_nsec = 0;
        old->it_interval.tv_sec = 0;
        old->it_interval.tv_nsec = 0;
        if (current->posix_timers[idx].armed) {
            uint32_t elapsed = kernel_timer_get_ticks() - current->posix_timers[idx].start_tick;
            uint32_t remaining = current->posix_timers[idx].it_value_ticks > elapsed
                ? current->posix_timers[idx].it_value_ticks - elapsed : 0u;
            old->it_value.tv_sec = (time_t)(remaining / 1000u);
            old->it_value.tv_nsec = (long)((remaining % 1000u) * 1000000l);
        }
    }
    if (value_ptr != 0u) {
        const struct itimerspec *val = (const struct itimerspec *)(uintptr_t)value_ptr;
        if (val->it_value.tv_sec == 0 && val->it_value.tv_nsec == 0) {
            current->posix_timers[idx].armed = 0;
        } else {
            uint32_t ticks = (uint32_t)val->it_value.tv_sec * 1000u;
            ticks += (uint32_t)(val->it_value.tv_nsec / 1000000l);
            if (val->it_value.tv_nsec % 1000000l != 0) ticks += 1u;
            current->posix_timers[idx].it_value_ticks = ticks;
            ticks = (uint32_t)val->it_interval.tv_sec * 1000u;
            ticks += (uint32_t)(val->it_interval.tv_nsec / 1000000l);
            if (val->it_interval.tv_nsec % 1000000l != 0) ticks += 1u;
            current->posix_timers[idx].it_interval_ticks = ticks;
            current->posix_timers[idx].start_tick = kernel_timer_get_ticks();
            current->posix_timers[idx].armed = 1;
        }
    }
    return 0u;
}

static uint32_t sys_getaddrinfo_stub(uint32_t a, uint32_t b, uint32_t c,
                                      uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)-1;
}

static uint32_t sys_getnameinfo_stub(uint32_t a, uint32_t b, uint32_t c,
                                      uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)-1;
}

static uint32_t sys_sendmsg_stub(uint32_t a, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)-1;
}

static uint32_t sys_recvmsg_stub(uint32_t a, uint32_t b, uint32_t c,
                                  uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)-1;
}

static uint32_t sys_shutdown_stub(uint32_t a, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (uint32_t)-1;
}

void syscall_init(void) {
    /* register new kernel syscalls; numbers are defined in
       include/userland_api.h */
    syscall_table[SYSCALL_GFX_CLEAR] = sys_gfx_clear;
    syscall_table[SYSCALL_GFX_RECT] = sys_gfx_rect;
    syscall_table[SYSCALL_GFX_TEXT] = sys_gfx_text;
    syscall_table[SYSCALL_GFX_FLIP] = sys_gfx_flip;
    syscall_table[SYSCALL_GFX_SET_PRESENT_POLICY] = sys_gfx_set_present_policy;
    syscall_table[SYSCALL_GFX_SET_PRESENT_COPY_OVERRIDE] = sys_gfx_set_present_copy_override;
    syscall_table[SYSCALL_GFX_BLIT8] = sys_gfx_blit8;
    syscall_table[SYSCALL_GFX_BLIT8_PRESENT] = sys_gfx_blit8_present;
    syscall_table[SYSCALL_GFX_BLIT8_STRETCH] = sys_gfx_blit8_stretch;
    syscall_table[SYSCALL_GFX_BLIT8_STRETCH_PRESENT] = sys_gfx_blit8_stretch_present;
    syscall_table[SYSCALL_GFX_LEAVE] = sys_gfx_leave;
    syscall_table[SYSCALL_GFX_SET_MODE] = sys_gfx_set_mode;
    syscall_table[SYSCALL_GFX_SET_PALETTE] = sys_gfx_set_palette;
    syscall_table[SYSCALL_GFX_GET_PALETTE] = sys_gfx_get_palette;
    syscall_table[SYSCALL_STORAGE_LOAD] = sys_storage_load;
    syscall_table[SYSCALL_STORAGE_SAVE] = sys_storage_save;
    syscall_table[SYSCALL_STORAGE_READ_SECTORS] = sys_storage_read_sectors;
    syscall_table[SYSCALL_STORAGE_WRITE_SECTORS] = sys_storage_write_sectors;
    syscall_table[SYSCALL_STORAGE_TOTAL_SECTORS] = sys_storage_total_sectors;
    syscall_table[SYSCALL_STORAGE_PARTITION_START_LBA] = sys_storage_partition_start_lba;
    syscall_table[SYSCALL_OPEN] = sys_fs_open;
    syscall_table[SYSCALL_READ] = sys_fs_read;
    syscall_table[SYSCALL_WRITE] = sys_fs_write;
    syscall_table[SYSCALL_CLOSE] = sys_fs_close;
    syscall_table[SYSCALL_LSEEK] = sys_fs_lseek;
    syscall_table[SYSCALL_STAT] = sys_fs_stat;
    syscall_table[SYSCALL_FSTAT] = sys_fs_fstat;
    syscall_table[SYSCALL_INPUT_MOUSE] = sys_input_mouse;
    syscall_table[SYSCALL_INPUT_KEY] = sys_input_key;
    syscall_table[SYSCALL_INPUT_EVENT] = sys_input_event;
    syscall_table[SYSCALL_TEXT_PUTC] = sys_text_putc;     /* legacy text mode */
    syscall_table[SYSCALL_TEXT_CLEAR] = sys_text_clear;   /* legacy text mode */
    syscall_table[SYSCALL_TEXT_MOVE_CURSOR] = sys_text_move_cursor;
    syscall_table[SYSCALL_TEXT_WRITE] = sys_text_write;
    syscall_table[SYSCALL_SLEEP] = sys_sleep;
    syscall_table[SYSCALL_TIME_TICKS] = sys_time_ticks;
    syscall_table[SYSCALL_GFX_INFO] = sys_gfx_info;
    syscall_table[SYSCALL_GFX_CAPS] = sys_gfx_caps;
    syscall_table[SYSCALL_GFX_BENCH] = sys_gfx_bench;
    syscall_table[SYSCALL_AUDIO_GETINFO] = sys_audio_get_info;
    syscall_table[SYSCALL_AUDIO_GET_STATUS] = sys_audio_get_status;
    syscall_table[SYSCALL_AUDIO_SET_PARAMS] = sys_audio_set_params;
    syscall_table[SYSCALL_AUDIO_START] = sys_audio_start;
    syscall_table[SYSCALL_AUDIO_STOP] = sys_audio_stop;
    syscall_table[SYSCALL_AUDIO_WRITE] = sys_audio_write;
    syscall_table[SYSCALL_AUDIO_WRITE_ASYNC] = sys_audio_write_async;
    syscall_table[SYSCALL_AUDIO_PLAY_ASSET] = sys_audio_play_asset;
    syscall_table[SYSCALL_AUDIO_READ] = sys_audio_read;
    syscall_table[SYSCALL_AUDIO_CONTROL_INFO] = sys_audio_control_info;
    syscall_table[SYSCALL_AUDIO_MIXER_READ] = sys_audio_mixer_read;
    syscall_table[SYSCALL_AUDIO_MIXER_WRITE] = sys_audio_mixer_write;
    syscall_table[SYSCALL_NETWORK_GETINFO] = sys_network_get_info;
    syscall_table[SYSCALL_NETWORK_GET_STATUS] = sys_network_get_status;
    syscall_table[SYSCALL_NETWORK_SCAN] = sys_network_scan;
    syscall_table[SYSCALL_NETWORK_CONNECT_WIFI] = sys_network_connect_wifi;
    syscall_table[SYSCALL_NETWORK_DISCONNECT] = sys_network_disconnect;
    syscall_table[SYSCALL_NETWORK_CONNECT_ETHERNET] = sys_network_connect_ethernet;
    syscall_table[SYSCALL_NETWORK_CONFIGURE_ETHERNET] = sys_network_configure_ethernet;
    syscall_table[SYSCALL_NETWORK_SOCKET] = sys_network_socket;
    syscall_table[SYSCALL_NETWORK_BIND] = sys_network_bind;
    syscall_table[SYSCALL_NETWORK_CONNECT] = sys_network_socket_connect;
    syscall_table[SYSCALL_NETWORK_SEND] = sys_network_send;
    syscall_table[SYSCALL_NETWORK_RECV] = sys_network_recv;
    syscall_table[SYSCALL_NETWORK_CLOSE] = sys_network_close;
    syscall_table[SYSCALL_NETWORK_LISTEN] = sys_network_listen;
    syscall_table[SYSCALL_NETWORK_ACCEPT] = sys_network_accept;
    syscall_table[SYSCALL_NETWORK_SENDTO] = sys_network_sendto;
    syscall_table[SYSCALL_NETWORK_RECVFROM] = sys_network_recvfrom;
    syscall_table[SYSCALL_GETPID] = sys_getpid;
    syscall_table[SYSCALL_LAUNCH_INFO] = sys_launch_info;
    syscall_table[SYSCALL_YIELD] = sys_yield;
    syscall_table[SYSCALL_WRITE_DEBUG] = sys_write_debug;
    syscall_table[SYSCALL_KEYBOARD_SET_LAYOUT] = sys_keyboard_set_layout;
    syscall_table[SYSCALL_KEYBOARD_GET_LAYOUT] = sys_keyboard_get_layout;
    syscall_table[SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS] = sys_keyboard_get_available_layouts;
    syscall_table[SYSCALL_SHUTDOWN] = sys_shutdown;
    syscall_table[SYSCALL_SERVICE_RECV] = sys_service_receive;
    syscall_table[SYSCALL_SERVICE_SEND] = sys_service_send;
    syscall_table[SYSCALL_MESSAGE_POST] = sys_message_post;
    syscall_table[SYSCALL_MESSAGE_RECV] = sys_message_receive;
    syscall_table[SYSCALL_SERVICE_SUBSCRIBE] = sys_service_subscribe;
    syscall_table[SYSCALL_SERVICE_PID] = sys_service_pid;
    syscall_table[SYSCALL_SERVICE_RESTART] = sys_service_restart;
    syscall_table[SYSCALL_SERVICE_HEALTH] = sys_service_health;
    syscall_table[SYSCALL_SERVICE_EVENT_RECV] = sys_service_event_receive;
    syscall_table[SYSCALL_TRANSFER_SIZE] = sys_transfer_size;
    syscall_table[SYSCALL_TRANSFER_READ] = sys_transfer_read;
    syscall_table[SYSCALL_TRANSFER_WRITE] = sys_transfer_write;
    syscall_table[SYSCALL_STORAGE_BACKEND_LOAD] = sys_storage_backend_load;
    syscall_table[SYSCALL_STORAGE_BACKEND_SAVE] = sys_storage_backend_save;
    syscall_table[SYSCALL_STORAGE_BACKEND_READ_SECTORS] = sys_storage_backend_read_sectors;
    syscall_table[SYSCALL_STORAGE_BACKEND_WRITE_SECTORS] = sys_storage_backend_write_sectors;
    syscall_table[SYSCALL_STORAGE_BACKEND_TOTAL_SECTORS] = sys_storage_backend_total_sectors;
    syscall_table[SYSCALL_STORAGE_BACKEND_PARTITION_START_LBA] =
        sys_storage_backend_partition_start_lba;
    syscall_table[SYSCALL_FS_BACKEND_OPEN] = sys_fs_backend_open;
    syscall_table[SYSCALL_FS_BACKEND_READ] = sys_fs_backend_read;
    syscall_table[SYSCALL_FS_BACKEND_WRITE] = sys_fs_backend_write;
    syscall_table[SYSCALL_FS_BACKEND_CLOSE] = sys_fs_backend_close;
    syscall_table[SYSCALL_FS_BACKEND_LSEEK] = sys_fs_backend_lseek;
    syscall_table[SYSCALL_FS_BACKEND_STAT] = sys_fs_backend_stat;
    syscall_table[SYSCALL_FS_BACKEND_FSTAT] = sys_fs_backend_fstat;
    syscall_table[SYSCALL_AUDIO_EVENT_SUBSCRIBE] = sys_audio_event_subscribe;
    syscall_table[SYSCALL_AUDIO_EVENT_RECV] = sys_audio_event_receive;
    syscall_table[SYSCALL_VIDEO_EVENT_SUBSCRIBE] = sys_video_event_subscribe;
    syscall_table[SYSCALL_VIDEO_EVENT_RECV] = sys_video_event_receive;
    syscall_table[SYSCALL_VIDEO_PRESENT_SUBMIT] = sys_video_present_submit;
    syscall_table[SYSCALL_NETWORK_EVENT_SUBSCRIBE] = sys_network_event_subscribe;
    syscall_table[SYSCALL_NETWORK_EVENT_RECV] = sys_network_event_receive;
    syscall_table[SYSCALL_TASK_SNAPSHOT] = sys_task_snapshot;
    syscall_table[SYSCALL_LAUNCH_BUILTIN_USER] = sys_launch_builtin_user;
    syscall_table[SYSCALL_TASK_TERMINATE] = sys_task_terminate;
    syscall_table[SYSCALL_LAUNCH_APP] = sys_launch_app;
    syscall_table[SYSCALL_TASK_EVENT_SUBSCRIBE] = sys_task_event_subscribe;
    syscall_table[SYSCALL_TASK_EVENT_RECV] = sys_task_event_receive;
    syscall_table[SYSCALL_TASK_CREATE] = sys_task_create;
    syscall_table[SYSCALL_MEMORY_STATUS] = sys_memory_status;
    syscall_table[SYSCALL_MEMORY_BUDGET] = sys_memory_budget;

    syscall_table[SYSCALL_FORK] = sys_fork;
    syscall_table[SYSCALL_EXECVE] = sys_execve;
    syscall_table[SYSCALL_EXIT] = sys_exit;
    syscall_table[SYSCALL_WAITPID] = sys_waitpid;
    syscall_table[SYSCALL_MMAP] = sys_mmap;
    syscall_table[SYSCALL_MUNMAP] = sys_munmap;
    syscall_table[SYSCALL_SBRK] = sys_sbrk;
    syscall_table[SYSCALL_PIPE] = sys_pipe;
    syscall_table[SYSCALL_KILL] = sys_kill;
    syscall_table[SYSCALL_GETCWD] = sys_getcwd;
    syscall_table[SYSCALL_CHDIR] = sys_chdir;
    syscall_table[SYSCALL_OPENDIR] = sys_opendir;
    syscall_table[SYSCALL_READDIR] = sys_readdir;
    syscall_table[SYSCALL_CLOSEDIR] = sys_closedir;
    syscall_table[SYSCALL_MKDIR] = sys_mkdir;

    syscall_table[SYSCALL_TRUNCATE] = sys_truncate;
    syscall_table[SYSCALL_FTRUNCATE] = sys_ftruncate;
    syscall_table[SYSCALL_CHMOD] = sys_chmod;
    syscall_table[SYSCALL_CHOWN] = sys_chown;
    syscall_table[SYSCALL_LINK] = sys_link;
    syscall_table[SYSCALL_SYMLINK] = sys_symlink;
    syscall_table[SYSCALL_READLINK] = sys_readlink;
    syscall_table[SYSCALL_UTIMENSAT] = sys_utimensat;
    syscall_table[SYSCALL_MOUNT] = sys_mount;
    syscall_table[SYSCALL_UMOUNT] = sys_umount;
    syscall_table[SYSCALL_STATVFS] = sys_statvfs;
    syscall_table[SYSCALL_REALPATH] = sys_realpath;

    syscall_table[SYSCALL_MPROTECT] = sys_mprotect;
    syscall_table[SYSCALL_MLOCK] = sys_mlock;
    syscall_table[SYSCALL_MUNLOCK] = sys_munlock;
    syscall_table[SYSCALL_MLOCKALL] = sys_mlockall;
    syscall_table[SYSCALL_MUNLOCKALL] = sys_munlockall;
    syscall_table[SYSCALL_MADVISE] = sys_madvise;
    syscall_table[SYSCALL_SHM_OPEN] = sys_shm_open;
    syscall_table[SYSCALL_SHM_UNLINK] = sys_shm_unlink;

    syscall_table[SYSCALL_THREAD_CREATE] = sys_thread_create;
    syscall_table[SYSCALL_THREAD_EXIT] = sys_thread_exit;
    syscall_table[SYSCALL_THREAD_JOIN] = sys_thread_join;
    syscall_table[SYSCALL_THREAD_GETTLS] = sys_thread_getspecific;
    syscall_table[SYSCALL_THREAD_SETTLS] = sys_thread_setspecific;
    syscall_table[SYSCALL_WAITABLE_INIT] = sys_waitable_init;
    syscall_table[SYSCALL_WAITABLE_WAIT] = sys_waitable_wait;
    syscall_table[SYSCALL_WAITABLE_WAKE] = sys_waitable_wake;
    syscall_table[SYSCALL_THREAD_DETACH] = sys_thread_detach;

    syscall_table[SYSCALL_SIGACTION] = sys_sigaction;
    syscall_table[SYSCALL_SIGPROCMASK] = sys_sigprocmask;
    syscall_table[SYSCALL_SIGSUSPEND] = sys_sigsuspend;
    syscall_table[SYSCALL_SIGPENDING] = sys_sigpending;
    syscall_table[SYSCALL_ALARM] = sys_alarm;
    syscall_table[SYSCALL_SETITIMER] = sys_setitimer;
    syscall_table[SYSCALL_GETITIMER] = sys_getitimer;
    syscall_table[SYSCALL_SIGWAIT] = sys_sigwait;
    syscall_table[SYSCALL_SIGWAITINFO] = sys_sigwaitinfo;

    syscall_table[SYSCALL_SETPGID] = sys_setpgid;
    syscall_table[SYSCALL_GETPGID] = sys_getpgid;
    syscall_table[SYSCALL_SETSID] = sys_setsid;
    syscall_table[SYSCALL_GETSID] = sys_getsid;
    syscall_table[SYSCALL_SETUID] = sys_setuid;
    syscall_table[SYSCALL_GETUID] = sys_getuid;
    syscall_table[SYSCALL_SETEUID] = sys_seteuid;
    syscall_table[SYSCALL_GETEUID] = sys_geteuid;
    syscall_table[SYSCALL_SETGID] = sys_setgid;
    syscall_table[SYSCALL_GETGID] = sys_getgid;
    syscall_table[SYSCALL_SETEGID] = sys_setegid;
    syscall_table[SYSCALL_GETEGID] = sys_getegid;
    syscall_table[SYSCALL_NICE] = sys_nice;
    syscall_table[SYSCALL_GETPRIORITY] = sys_getpriority;
    syscall_table[SYSCALL_SETPRIORITY] = sys_setpriority;
    syscall_table[SYSCALL_GETRLIMIT] = sys_getrlimit;
    syscall_table[SYSCALL_SETRLIMIT] = sys_setrlimit;
    syscall_table[SYSCALL_GETGROUPS] = sys_getgroups;
    syscall_table[SYSCALL_SETGROUPS] = sys_setgroups;

    syscall_table[SYSCALL_GETADDRINFO] = sys_getaddrinfo_stub;
    syscall_table[SYSCALL_GETNAMEINFO] = sys_getnameinfo_stub;
    syscall_table[SYSCALL_SENDMSG] = sys_sendmsg_stub;
    syscall_table[SYSCALL_RECVMSG] = sys_recvmsg_stub;
    /* TEMP probe (adendo 36 arm-freeze isolation) — removed with the probe */
    syscall_table[231u] = sys_debug_watch;
    syscall_table[SYSCALL_SOCKET_SHUTDOWN] = sys_shutdown_stub;
    syscall_table[SYSCALL_CLOCK_GETTIME] = sys_clock_gettime;
    syscall_table[SYSCALL_CLOCK_SETTIME] = sys_clock_settime;
    syscall_table[SYSCALL_NANOSLEEP] = sys_nanosleep;
    syscall_table[SYSCALL_TIMER_CREATE] = sys_timer_create;
    syscall_table[SYSCALL_TIMER_SETTIME] = sys_timer_settime;
    syscall_table[SYSCALL_INPUT_KEY_WAIT] = sys_input_key_wait;
}

/* dispatch routine called by ISR */
uint32_t syscall_dispatch_internal(uint32_t num, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d, uint32_t e) {
    /*
     * STALE-ESP0 witness (P4 classe #5, adendo 30): runs on EVERY ring3->ring0
     * transition, before any handler can push. If the TSS esp0 this cpu just
     * used to enter kernel mode differs from the CURRENT task's declared
     * stack top, the trap frame was pushed onto somebody else's memory
     * (dr4's WHO scene: pid=9 running on cpu0 with ESP inside INIT's stack).
     * This is the in-loco moment: esp0 wrong + tss-ring history names which
     * install left the stale value. Kernel-mode callers (execve from init's
     * host loop, service locals) may legitimately differ in esp0 vs current
     * when there is no user task; only check user-mode currents.
     */
    {
        process_t *t = scheduler_current();

        if (t != NULL && t->runs_in_user_mode != 0u &&
            t->stack != NULL && t->stack_size != 0u) {
            uintptr_t esp0 = kernel_tss_get_kernel_stack(kernel_cpu_index());
            uintptr_t want = (uintptr_t)t->stack + t->stack_size;

            if (esp0 != 0u && esp0 != want) {
                static volatile uint32_t g_syscall_esp0_budget = 8u;
                if (g_syscall_esp0_budget != 0u) {
                    g_syscall_esp0_budget--;
                    kernel_debug_printf("sys-esp0: STALE-AT-ENTRY pid=%d cpu=%u esp0=%x want=%x num=%u\n",
                                        t->pid,
                                        (unsigned int)kernel_cpu_index(),
                                        (unsigned int)esp0, (unsigned int)want,
                                        (unsigned int)num);
                    /* Name which INSTALLS left this stale value while the
                     * evidence is in place (se4 caught a transient one;
                     * the harmful ones — dr4 class — must show the
                     * preceding publish history here). */
                    kernel_tss_install_ring_dump("stale-entry");
                }
            }
        }
    }
    if (num < MAX_SYSCALLS && syscall_table[num]) {
        return syscall_table[num](a, b, c, d, e);
    }
    return (uint32_t)-1;
}
