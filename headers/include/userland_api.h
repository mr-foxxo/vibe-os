#ifndef USERLAND_API_H
#define USERLAND_API_H

#include <stdint.h>

#define USERLAND_LAUNCH_NAME_MAX 16u
#define USERLAND_LAUNCH_ARGC_MAX 8u
#define USERLAND_LAUNCH_ARGV_BYTES 192u

enum syscall_id {
    SYSCALL_GFX_CLEAR = 1,
    SYSCALL_GFX_RECT = 2,
    SYSCALL_GFX_TEXT = 3,
    SYSCALL_INPUT_MOUSE = 4,
    SYSCALL_INPUT_KEY = 5,
    SYSCALL_SLEEP = 6,
    SYSCALL_TIME_TICKS = 7,
    SYSCALL_GFX_INFO = 8,
    SYSCALL_GETPID = 9,
    SYSCALL_YIELD = 10,
    SYSCALL_WRITE_DEBUG = 11,
    SYSCALL_TEXT_PUTC = 12,
    SYSCALL_TEXT_CLEAR = 13,
    SYSCALL_GFX_FLIP = 14,
    SYSCALL_GFX_LEAVE = 15,
    SYSCALL_TEXT_MOVE_CURSOR = 16,
    SYSCALL_GFX_SET_MODE = 17,
    SYSCALL_STORAGE_LOAD = 18,
    SYSCALL_STORAGE_SAVE = 19,
    SYSCALL_STORAGE_READ_SECTORS = 20,
    SYSCALL_STORAGE_WRITE_SECTORS = 26,
    SYSCALL_STORAGE_TOTAL_SECTORS = 27,
    SYSCALL_GFX_SET_PALETTE = 28,
    SYSCALL_GFX_GET_PALETTE = 29,
    SYSCALL_GFX_BLIT8 = 30,

    SYSCALL_KEYBOARD_SET_LAYOUT = 21,
    SYSCALL_KEYBOARD_GET_LAYOUT = 22,
    SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS = 23,
    SYSCALL_GFX_CAPS = 24,
    SYSCALL_SHUTDOWN = 25,

    /* Filesystem syscalls */
    SYSCALL_OPEN = 31,
    SYSCALL_READ = 32,
    SYSCALL_WRITE = 33,
    SYSCALL_CLOSE = 34,
    SYSCALL_LSEEK = 35,
    SYSCALL_STAT = 36,
    SYSCALL_FSTAT = 37,
    SYSCALL_LAUNCH_INFO = 38,
    SYSCALL_TEXT_WRITE = 39,
    SYSCALL_SERVICE_RECV = 40,
    SYSCALL_SERVICE_SEND = 41,
    SYSCALL_SERVICE_BACKEND = 42,
    SYSCALL_TASK_SNAPSHOT = 43,
    SYSCALL_TASK_TERMINATE = 44,
    SYSCALL_GFX_BLIT8_STRETCH = 45,
    SYSCALL_GFX_SET_PRESENT_POLICY = 46,
    SYSCALL_GFX_BLIT8_STRETCH_PRESENT = 47,
    SYSCALL_GFX_BLIT8_PRESENT = 48,
    SYSCALL_GFX_BENCH = 49,
    SYSCALL_GFX_SET_PRESENT_COPY_OVERRIDE = 50,
    SYSCALL_AUDIO_GETINFO = 51,
    SYSCALL_AUDIO_MIXER_READ = 52,
    SYSCALL_AUDIO_MIXER_WRITE = 53,
    SYSCALL_AUDIO_CONTROL_INFO = 54,
    SYSCALL_NETWORK_GETINFO = 55,
    SYSCALL_NETWORK_GET_STATUS = 56,
    SYSCALL_NETWORK_SCAN = 57,
    SYSCALL_NETWORK_CONNECT_WIFI = 58,
    SYSCALL_NETWORK_DISCONNECT = 59,
    SYSCALL_AUDIO_SET_PARAMS = 60,
    SYSCALL_AUDIO_START = 61,
    SYSCALL_AUDIO_STOP = 62,
    SYSCALL_AUDIO_WRITE = 63,
    SYSCALL_AUDIO_GET_STATUS = 64,
    SYSCALL_NETWORK_SOCKET = 65,
    SYSCALL_NETWORK_BIND = 66,
    SYSCALL_NETWORK_CONNECT = 67,
    SYSCALL_NETWORK_SEND = 68,
    SYSCALL_NETWORK_RECV = 69,
    SYSCALL_NETWORK_CLOSE = 70,
    SYSCALL_NETWORK_LISTEN = 71,
    SYSCALL_NETWORK_ACCEPT = 72,
    SYSCALL_NETWORK_CONNECT_ETHERNET = 73,
    SYSCALL_NETWORK_CONFIGURE_ETHERNET = 74,
    SYSCALL_AUDIO_READ = 75,
    SYSCALL_AUDIO_WRITE_ASYNC = 76,
    SYSCALL_LAUNCH_BUILTIN_USER = 77,
    SYSCALL_INPUT_EVENT = 78,
    SYSCALL_SERVICE_SUBSCRIBE = 79,
    SYSCALL_SERVICE_EVENT_RECV = 80,
    SYSCALL_AUDIO_EVENT_SUBSCRIBE = 81,
    SYSCALL_AUDIO_EVENT_RECV = 82,
    SYSCALL_VIDEO_EVENT_SUBSCRIBE = 83,
    SYSCALL_VIDEO_EVENT_RECV = 84,
    SYSCALL_VIDEO_PRESENT_SUBMIT = 85,
    SYSCALL_NETWORK_EVENT_SUBSCRIBE = 86,
    SYSCALL_NETWORK_EVENT_RECV = 87,
    SYSCALL_TASK_EVENT_SUBSCRIBE = 88,
    SYSCALL_TASK_EVENT_RECV = 89,
    SYSCALL_SERVICE_PID = 90,
    SYSCALL_SERVICE_RESTART = 91,
    SYSCALL_LAUNCH_APP = 92,
    SYSCALL_TRANSFER_SIZE = 93,
    SYSCALL_TRANSFER_READ = 94,
    SYSCALL_TRANSFER_WRITE = 95,
    SYSCALL_STORAGE_PARTITION_START_LBA = 96,
    SYSCALL_STORAGE_BACKEND_LOAD = 97,
    SYSCALL_STORAGE_BACKEND_SAVE = 98,
    SYSCALL_STORAGE_BACKEND_READ_SECTORS = 99,
    SYSCALL_STORAGE_BACKEND_WRITE_SECTORS = 100,
    SYSCALL_STORAGE_BACKEND_TOTAL_SECTORS = 101,
    SYSCALL_STORAGE_BACKEND_PARTITION_START_LBA = 102,
    SYSCALL_FS_BACKEND_OPEN = 103,
    SYSCALL_FS_BACKEND_READ = 104,
    SYSCALL_FS_BACKEND_WRITE = 105,
    SYSCALL_FS_BACKEND_CLOSE = 106,
    SYSCALL_FS_BACKEND_LSEEK = 107,
    SYSCALL_FS_BACKEND_STAT = 108,
    SYSCALL_FS_BACKEND_FSTAT = 109,
    SYSCALL_AUDIO_PLAY_ASSET = 110,
    SYSCALL_TASK_CREATE = 111,
    SYSCALL_MESSAGE_POST = 112,
    SYSCALL_MESSAGE_RECV = 113,
    SYSCALL_MEMORY_STATUS = 114,
    SYSCALL_MEMORY_BUDGET = 115,
    SYSCALL_NETWORK_SENDTO = 116,
    SYSCALL_NETWORK_RECVFROM = 117,

    /* POSIX process & memory syscalls */
    SYSCALL_FORK = 120,
    SYSCALL_EXECVE = 121,
    SYSCALL_EXIT = 122,
    SYSCALL_WAITPID = 123,
    SYSCALL_MMAP = 124,
    SYSCALL_MUNMAP = 125,
    SYSCALL_SBRK = 126,
    SYSCALL_PIPE = 127,
    SYSCALL_KILL = 128,
    SYSCALL_GETCWD = 129,
    SYSCALL_CHDIR = 130,
    SYSCALL_OPENDIR = 131,
    SYSCALL_READDIR = 132,
    SYSCALL_CLOSEDIR = 133,
    SYSCALL_MKDIR = 134,
    SYSCALL_TRUNCATE = 144,
    SYSCALL_FTRUNCATE = 145,
    SYSCALL_CHMOD = 146,
    SYSCALL_CHOWN = 147,
    SYSCALL_LINK = 148,
    SYSCALL_SYMLINK = 149,
    SYSCALL_READLINK = 150,
    SYSCALL_UTIMENSAT = 151,
    SYSCALL_MOUNT = 152,
    SYSCALL_UMOUNT = 153,
    SYSCALL_STATVFS = 154,
    SYSCALL_REALPATH = 155,

    /* POSIX process groups, users, priority, resource limits */
    SYSCALL_SETPGID = 156,
    SYSCALL_GETPGID = 157,
    SYSCALL_SETSID = 158,
    SYSCALL_GETSID = 159,
    SYSCALL_SETUID = 160,
    SYSCALL_GETUID = 161,
    SYSCALL_SETEUID = 162,
    SYSCALL_GETEUID = 163,
    SYSCALL_SETGID = 164,
    SYSCALL_GETGID = 165,
    SYSCALL_SETEGID = 166,
    SYSCALL_GETEGID = 167,
    SYSCALL_NICE = 168,
    SYSCALL_GETPRIORITY = 169,
    SYSCALL_SETPRIORITY = 170,
    SYSCALL_GETRLIMIT = 171,
    SYSCALL_SETRLIMIT = 172,
    SYSCALL_GETGROUPS = 173,
    SYSCALL_SETGROUPS = 174,

    /* POSIX memory management syscalls */
    SYSCALL_MPROTECT = 175,
    SYSCALL_MLOCK = 176,
    SYSCALL_MUNLOCK = 177,
    SYSCALL_MLOCKALL = 178,
    SYSCALL_MUNLOCKALL = 179,
    SYSCALL_MADVISE = 180,
    SYSCALL_SHM_OPEN = 181,
    SYSCALL_SHM_UNLINK = 182,

    /* Thread syscalls */
    SYSCALL_THREAD_CREATE = 183,
    SYSCALL_THREAD_EXIT = 184,
    SYSCALL_THREAD_JOIN = 185,
    SYSCALL_THREAD_GETTLS = 186,
    SYSCALL_THREAD_SETTLS = 187,
    SYSCALL_WAITABLE_INIT = 188,
    SYSCALL_WAITABLE_WAIT = 189,
    SYSCALL_WAITABLE_WAKE = 200,

    /* Signal syscalls */
    SYSCALL_SIGACTION = 190,
    SYSCALL_SIGPROCMASK = 191,
    SYSCALL_SIGSUSPEND = 192,
    SYSCALL_SIGPENDING = 193,
    SYSCALL_ALARM = 194,
    SYSCALL_SETITIMER = 195,
    SYSCALL_GETITIMER = 196,
    SYSCALL_SIGWAIT = 197,
    SYSCALL_SIGWAITINFO = 198,
    SYSCALL_THREAD_DETACH = 199,

    SYSCALL_GETADDRINFO = 220,
    SYSCALL_GETNAMEINFO = 221,
    SYSCALL_SENDMSG = 222,
    SYSCALL_RECVMSG = 223,
    SYSCALL_SOCKET_SHUTDOWN = 224,
    SYSCALL_CLOCK_GETTIME = 225,
    SYSCALL_CLOCK_SETTIME = 226,
    SYSCALL_NANOSLEEP = 227,
    SYSCALL_TIMER_CREATE = 228,
    SYSCALL_TIMER_SETTIME = 229,
    SYSCALL_SERVICE_HEALTH = 230,
    /* Additive ABI (bug #19 item 0.12, 01/11): blocking key read.
     * a = timeout ticks (0 = indefinite); returns key (>0) or -1.
     * SYSCALL_INPUT_KEY (5) is unchanged for every existing caller. */
    SYSCALL_INPUT_KEY_WAIT = 231,
    /* Live socket-table export (item 1.5). Additive; the existing network
     * syscalls and structs are byte-for-byte untouched. */
    SYSCALL_NETWORK_SOCKET_LIST = 232
};

enum userland_builtin_target {
    USERLAND_BUILTIN_NONE = 0,
    USERLAND_BUILTIN_SHELL = 1,
    USERLAND_BUILTIN_DESKTOP = 2,
    USERLAND_BUILTIN_STARTX = 3,
    USERLAND_BUILTIN_DESKTOP_AUDIO = 4,
    USERLAND_BUILTIN_BOOT_AUDIO = 5,
    USERLAND_BUILTIN_DESKTOP_SESSION = 6,
    USERLAND_BUILTIN_SHELL_SESSION = 7
};

enum input_keycode {
    KEY_ARROW_UP = 0x100,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_DELETE
};

struct mouse_state {
    int x;
    int y;
    int dx;
    int dy;
    int wheel;
    uint8_t buttons;
};

enum input_event_type {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY = 1,
    INPUT_EVENT_MOUSE = 2
};

struct input_event {
    uint32_t type;
    int32_t value;
    struct mouse_state mouse;
};

struct video_mode {
    uint32_t fb_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

typedef struct video_mode video_mode_t;

#define VIDEO_MODE_LIST_MAX 16u

enum video_capability_flags {
    VIDEO_CAPS_TEXT_ONLY = 1u << 0,
    VIDEO_CAPS_BOOT_LFB = 1u << 1,
    VIDEO_CAPS_BGA = 1u << 2,
    VIDEO_CAPS_CAN_SET_MODE = 1u << 3
};

enum video_resolution_bits {
    VIDEO_RES_640X480 = 1u << 0,
    VIDEO_RES_800X600 = 1u << 1,
    VIDEO_RES_1024X768 = 1u << 2,
    VIDEO_RES_1360X768 = 1u << 3,
    VIDEO_RES_1366X768 = 1u << 4,
    VIDEO_RES_1920X1080 = 1u << 5
};

enum video_present_mode {
    VIDEO_PRESENT_AUTO = 0,
    VIDEO_PRESENT_DIRTY = 1,
    VIDEO_PRESENT_FULL = 2
};

enum video_present_policy {
    VIDEO_PRESENT_POLICY_DEFAULT = 0,
    VIDEO_PRESENT_POLICY_DESKTOP = 1,
    VIDEO_PRESENT_POLICY_FULLSCREEN = 2
};

enum video_backend_kind {
    VIDEO_BACKEND_NONE = 0,
    VIDEO_BACKEND_LEGACY_LFB = 1,
    VIDEO_BACKEND_FAST_LFB = 2
};

enum video_native_backend_kind {
    VIDEO_NATIVE_BACKEND_NONE = 0,
    VIDEO_NATIVE_BACKEND_BGA = 1,
    VIDEO_NATIVE_BACKEND_I915 = 2,
    VIDEO_NATIVE_BACKEND_RADEON = 3,
    VIDEO_NATIVE_BACKEND_NOUVEAU = 4,
    VIDEO_NATIVE_BACKEND_UNKNOWN = 5
};

enum video_present_copy_kind {
    VIDEO_PRESENT_COPY_BYTE_LOOP = 0,
    VIDEO_PRESENT_COPY_REP_MOVSD = 1,
    VIDEO_PRESENT_COPY_MOVNTDQ = 2
};

enum video_present_copy_override_kind {
    VIDEO_PRESENT_COPY_OVERRIDE_AUTO = 0,
    VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP = 1,
    VIDEO_PRESENT_COPY_OVERRIDE_REP_MOVSD = 2,
    VIDEO_PRESENT_COPY_OVERRIDE_MOVNTDQ = 3
};

enum mk_video_event_type {
    MK_VIDEO_EVENT_NONE = 0,
    MK_VIDEO_EVENT_PRESENT = 1,
    MK_VIDEO_EVENT_MODE_SET = 2,
    MK_VIDEO_EVENT_LEAVE = 3,
    MK_VIDEO_EVENT_OVERFLOW = 4,
    MK_VIDEO_EVENT_PRESENT_SUBMITTED = 5,
    MK_VIDEO_EVENT_MODE_SET_BEGIN = 6,
    MK_VIDEO_EVENT_MODE_SET_DONE = 7,
    MK_VIDEO_EVENT_BACKEND_FAILED = 8,
    MK_VIDEO_EVENT_BACKEND_RECOVERED = 9
};

struct mk_video_event {
    uint32_t abi_version;
    uint32_t event_type;
    uint32_t present_mode;
    uint32_t sequence;
    uint32_t completed_sequence;
    uint32_t pending_depth;
    uint32_t active_width;
    uint32_t active_height;
    uint32_t dropped_events;
    uint32_t tick;
};

struct video_capabilities {
    uint32_t flags;
    uint32_t supported_modes;
    uint32_t active_width;
    uint32_t active_height;
    uint32_t active_bpp;
    uint32_t mode_count;
    uint16_t mode_width[VIDEO_MODE_LIST_MAX];
    uint16_t mode_height[VIDEO_MODE_LIST_MAX];
};

struct video_bench_info {
    uint32_t active_width;
    uint32_t active_height;
    uint32_t active_pitch;
    uint32_t gpu_vendor_id;
    uint32_t gpu_device_id;
    uint32_t gpu_revision;
    uint32_t detected_gpu_vendor_id;
    uint32_t detected_gpu_device_id;
    uint32_t detected_gpu_revision;
    uint32_t cpu_family;
    uint32_t cpu_model;
    uint32_t cpu_stepping;
    uint32_t fill_ticks;
    uint32_t present_ticks;
    uint32_t frame_ticks;
    uint32_t fullscreen_direct_ticks;
    uint32_t fullscreen_blit_present_ticks;
    uint32_t microkernel_frame_ticks;
    uint32_t microkernel_flip_ticks;
    uint32_t microkernel_blit_ticks;
    uint32_t microkernel_stretch_ticks;
    uint32_t frame_bytes;
    uint32_t backbuffer_bytes;
    uint32_t heap_free_before;
    uint32_t heap_free_after;
    uint32_t cpu_has_pat;
    uint32_t cpu_has_sse2;
    uint32_t wc_enabled;
    uint32_t backend_kind;
    uint32_t present_copy_kind;
    uint32_t present_copy_override_kind;
    uint32_t native_backend_kind;
    uint32_t detected_native_backend_kind;
    char cpu_vendor[13];
};

struct userland_launch_info {
    uint32_t abi_version;
    uint32_t pid;
    uint32_t kind;
    uint32_t service_type;
    uint32_t flags;
    uint32_t task_class;
    uint32_t boot_flags;
    uint32_t boot_partition_lba;
    uint32_t boot_partition_sectors;
    uint32_t data_partition_lba;
    uint32_t data_partition_sectors;
    uint32_t argc;
    char name[USERLAND_LAUNCH_NAME_MAX];
    char argv_data[USERLAND_LAUNCH_ARGV_BYTES];
};

struct mk_service_health_stats {
    uint32_t abi_version;
    uint32_t service_type;
    uint32_t online;
    uint32_t degraded;
    uint32_t uptime_ticks;
    uint32_t last_error;
    uint32_t pending_requests;
    uint32_t restart_count;
    uint32_t retry_attempts;
};

enum mk_service_event_type {
    MK_SERVICE_EVENT_NONE = 0,
    MK_SERVICE_EVENT_ONLINE = 1,
    MK_SERVICE_EVENT_OFFLINE = 2,
    MK_SERVICE_EVENT_DEGRADED = 3,
    MK_SERVICE_EVENT_RECOVERED = 4,
    MK_SERVICE_EVENT_RESTARTED = 5
};

struct mk_service_event {
    uint32_t abi_version;
    uint32_t service_type;
    uint32_t event_type;
    uint32_t pid;
    uint32_t restart_count;
    uint32_t transport_degraded;
    uint32_t tick;
};

enum mk_audio_feature_flags {
    MK_AUDIO_FEATURE_COMPAT_AUICH = 0x00000001u,
    MK_AUDIO_FEATURE_IRQ_REGISTERED = 0x00000002u,
    MK_AUDIO_FEATURE_NO_VALID_IRQ = 0x00000004u,
    MK_AUDIO_FEATURE_CAPTURE = 0x00000008u,
    MK_AUDIO_FEATURE_IRQ_SEEN = 0x00000010u,
    MK_AUDIO_FEATURE_STARVATION = 0x00000020u,
    MK_AUDIO_FEATURE_UNDERRUN = 0x00000040u,
    MK_AUDIO_FEATURE_MMIO_TRANSPORT = 0x00000080u,
    MK_AUDIO_FEATURE_CODECREADY_QUIRK = 0x00000100u,
    MK_AUDIO_FEATURE_MULTICHANNEL = 0x00000200u,
    MK_AUDIO_FEATURE_CAPTURE_DMA = 0x00000400u,
    MK_AUDIO_FEATURE_CORB_RIRB = 0x00000800u,
    MK_AUDIO_FEATURE_CODEC_PROBE = 0x00001000u,
    MK_AUDIO_FEATURE_WIDGET_PROBE = 0x00002000u,
    MK_AUDIO_FEATURE_PATH_PROGRAMMED = 0x00004000u,
    MK_AUDIO_FEATURE_USB_CLASS_DETECTED = 0x00008000u,
    MK_AUDIO_FEATURE_USB_CONFIGURED_READY = 0x00010000u,
    MK_AUDIO_FEATURE_USB_ATTACH_READY = 0x00020000u,
    MK_AUDIO_FEATURE_USB_ATTACHED_READY = 0x00040000u,
    MK_AUDIO_FEATURE_CONTROL_OWNER_AUDIOSVC = 0x00080000u,
    MK_AUDIO_FEATURE_KERNEL_BACKEND_EXECUTOR = 0x00100000u,
    MK_AUDIO_FEATURE_UI_PROGRESS_DECOUPLED = 0x00200000u
};

enum mk_audio_event_type {
    MK_AUDIO_EVENT_NONE = 0,
    MK_AUDIO_EVENT_QUEUED = 1,
    MK_AUDIO_EVENT_IDLE = 2,
    MK_AUDIO_EVENT_UNDERRUN = 3,
    MK_AUDIO_EVENT_OVERFLOW = 4,
    MK_AUDIO_EVENT_CAPTURE_READY = 5,
    MK_AUDIO_EVENT_CAPTURE_XRUN = 6
};

struct mk_audio_event {
    uint32_t abi_version;
    uint32_t event_type;
    uint32_t backend_kind;
    uint32_t queued_bytes;
    uint32_t underruns;
    uint32_t dropped_events;
    uint32_t tick;
};

#ifndef VIBE_SHARED_NETWORK_EVENT_ABI_DEFINED
#define VIBE_SHARED_NETWORK_EVENT_ABI_DEFINED
enum mk_network_event_type {
    MK_NETWORK_EVENT_NONE = 0,
    MK_NETWORK_EVENT_STATUS = 1,
    MK_NETWORK_EVENT_SOCKET_RECV = 2,
    MK_NETWORK_EVENT_SOCKET_ACCEPT = 3,
    MK_NETWORK_EVENT_SOCKET_SEND = 4,
    MK_NETWORK_EVENT_SOCKET_CLOSED = 5,
    MK_NETWORK_EVENT_BACKEND_RX = 6,
    MK_NETWORK_EVENT_BACKEND_TX = 7,
    MK_NETWORK_EVENT_OVERFLOW = 8,
    MK_NETWORK_EVENT_LEASE = 9,
    MK_NETWORK_EVENT_DNS = 10
};

struct mk_network_event {
    uint32_t abi_version;
    uint32_t event_type;
    int32_t handle;
    int32_t peer_handle;
    uint32_t sequence;
    uint32_t link_state;
    uint32_t byte_count;
    uint32_t dropped_events;
    uint32_t tick;
};
#endif

enum mk_task_class {
    MK_TASK_CLASS_NONE = 0,
    MK_TASK_CLASS_SUPERVISION = 1,
    MK_TASK_CLASS_DESKTOP = 2,
    MK_TASK_CLASS_SHELL = 3,
    MK_TASK_CLASS_APP_RUNTIME = 4,
    MK_TASK_CLASS_INPUT = 5,
    MK_TASK_CLASS_VIDEO_PRESENT = 6,
    MK_TASK_CLASS_STORAGE_IO = 7,
    MK_TASK_CLASS_FILESYSTEM_IO = 8,
    MK_TASK_CLASS_AUDIO_IO = 9,
    MK_TASK_CLASS_NETWORK_IO = 10,
    MK_TASK_CLASS_CONSOLE_IO = 11,
    MK_TASK_CLASS_VIDEO_CONTROL = 12
};

#define MK_TASK_CLASS_MASK(task_class) (1u << (task_class))
#define MK_TASK_CLASS_MASK_ALL 0xffffffffu

#define MK_MEMORY_STATUS_ABI_VERSION 1u
#define MK_MEMORY_BUDGET_ABI_VERSION 1u

enum mk_memory_pressure_level {
    MK_MEMORY_PRESSURE_IDLE = 0,
    MK_MEMORY_PRESSURE_NORMAL = 1,
    MK_MEMORY_PRESSURE_ELEVATED = 2,
    MK_MEMORY_PRESSURE_HIGH = 3,
    MK_MEMORY_PRESSURE_CRITICAL = 4
};

enum mk_memory_budget_flags {
    MK_MEMORY_BUDGET_ALLOW_OPPORTUNISTIC = 1u << 0,
    MK_MEMORY_BUDGET_PREFER_LOW_LATENCY = 1u << 1,
    MK_MEMORY_BUDGET_PREFER_RESIDENT_CACHE = 1u << 2
};

struct mk_memory_status {
    uint32_t abi_version;
    uint32_t current_pid;
    uint32_t current_task_class;
    uint32_t pressure_level;
    uint32_t physmem_total_bytes;
    uint32_t physmem_free_bytes;
    uint32_t kernel_heap_used_bytes;
    uint32_t kernel_heap_free_bytes;
    uint32_t app_arena_reserved_bytes;
    uint32_t current_arena_bytes;
    uint32_t current_stack_reserve_bytes;
    uint32_t current_heap_ceiling_bytes;
};

struct mk_memory_budget {
    uint32_t abi_version;
    uint32_t current_pid;
    uint32_t current_task_class;
    uint32_t pressure_level;
    uint32_t flags;
    uint32_t current_arena_bytes;
    uint32_t stack_reserve_bytes;
    uint32_t commit_floor_bytes;
    uint32_t working_set_target_bytes;
    uint32_t opportunistic_target_bytes;
    uint32_t absolute_ceiling_bytes;
    uint32_t concurrency_hint;
    uint32_t reclaim_batch_bytes;
};

#define MK_ASYNC_MESSAGE_ABI_VERSION 1u
#define MK_ASYNC_MESSAGE_PAYLOAD_MAX 256u
#define MK_FOXXO_MESSAGE_REQUEST 0x46580001u
#define MK_FOXXO_MESSAGE_REPLY 0x46580002u
#define MK_FOXXO_MESSAGE_EVENT 0x46580003u
#define MK_FOXXO_MESSAGE_ERROR 0x46580004u
#define MK_FOXXO_PACKET_ABI_VERSION 1u
#define MK_FOXXO_PACKET_BODY_MAX 224u

enum mk_async_message_flags {
    MK_ASYNC_MESSAGE_TO_PID = 1u << 0,
    MK_ASYNC_MESSAGE_TO_SERVICE = 1u << 1,
    MK_ASYNC_MESSAGE_DEFERRED = 1u << 2,
    MK_ASYNC_MESSAGE_REQUIRE_TARGET = 1u << 3
};

struct mk_async_message {
    uint32_t abi_version;
    uint32_t flags;
    uint32_t type;
    uint32_t source_pid;
    uint32_t target_pid;
    uint32_t target_service;
    uint32_t payload_size;
    uint8_t payload[MK_ASYNC_MESSAGE_PAYLOAD_MAX];
};

enum mk_foxxo_opcode {
    MK_FOXXO_OP_NONE = 0,
    MK_FOXXO_OP_PING = 1,
    MK_FOXXO_OP_ACK = 2,
    MK_FOXXO_OP_SERVICE_REQUEST = 3,
    MK_FOXXO_OP_SERVICE_REPLY = 4,
    MK_FOXXO_OP_EVENT = 5,
    MK_FOXXO_OP_ERROR = 6
};

struct mk_foxxo_packet {
    uint32_t abi_version;
    uint32_t opcode;
    uint32_t correlation_id;
    uint32_t route_service;
    uint32_t status;
    uint32_t body_size;
    uint8_t body[MK_FOXXO_PACKET_BODY_MAX];
};

enum mk_task_event_type {
    MK_TASK_EVENT_NONE = 0,
    MK_TASK_EVENT_LAUNCHED = 1,
    MK_TASK_EVENT_TERMINATED = 2,
    MK_TASK_EVENT_BLOCKED = 3,
    MK_TASK_EVENT_WOKE = 4,
    MK_TASK_EVENT_RESTART_REQUESTED = 5
};

struct mk_task_event {
    uint32_t abi_version;
    uint32_t event_type;
    uint32_t pid;
    uint32_t kind;
    uint32_t service_type;
    uint32_t task_class;
    uint32_t priority_tier;
    uint32_t flags;
    uint32_t sequence;
    uint32_t class_pending_depth;
    uint32_t class_dropped_events;
    uint32_t tick;
};

#define MK_TASK_EVENT_MASK_LAUNCHED (1u << MK_TASK_EVENT_LAUNCHED)
#define MK_TASK_EVENT_MASK_TERMINATED (1u << MK_TASK_EVENT_TERMINATED)
#define MK_TASK_EVENT_MASK_BLOCKED (1u << MK_TASK_EVENT_BLOCKED)
#define MK_TASK_EVENT_MASK_WOKE (1u << MK_TASK_EVENT_WOKE)
#define MK_TASK_EVENT_MASK_RESTART_REQUESTED (1u << MK_TASK_EVENT_RESTART_REQUESTED)
#define MK_TASK_EVENT_MASK_LIFECYCLE \
    (MK_TASK_EVENT_MASK_LAUNCHED | \
     MK_TASK_EVENT_MASK_TERMINATED | \
     MK_TASK_EVENT_MASK_RESTART_REQUESTED)
#define MK_TASK_EVENT_MASK_ALL \
    (MK_TASK_EVENT_MASK_LAUNCHED | \
     MK_TASK_EVENT_MASK_TERMINATED | \
     MK_TASK_EVENT_MASK_BLOCKED | \
     MK_TASK_EVENT_MASK_WOKE | \
     MK_TASK_EVENT_MASK_RESTART_REQUESTED)

#define MK_TASK_EVENT_WAIT_FOREVER 0xffffffffu

#define TASK_SNAPSHOT_ABI_VERSION 5u
#define TASK_SNAPSHOT_NAME_MAX 16u
#define TASK_SNAPSHOT_MAX 32u

#define TASK_SNAPSHOT_FLAG_BOOTSTRAP (1u << 0)
#define TASK_SNAPSHOT_FLAG_CRITICAL (1u << 1)
#define TASK_SNAPSHOT_FLAG_BUILTIN (1u << 2)
#define TASK_SNAPSHOT_FLAG_SERVICE_ONLINE (1u << 16)
#define TASK_SNAPSHOT_FLAG_SERVICE_DEGRADED (1u << 17)
#define TASK_SNAPSHOT_FLAG_SERVICE_RESTARTABLE (1u << 18)

enum task_wait_result {
    TASK_WAIT_RESULT_NONE = 0,
    TASK_WAIT_RESULT_SIGNALED = 1,
    TASK_WAIT_RESULT_TIMED_OUT = 2,
    TASK_WAIT_RESULT_CANCELED = 3
};

enum task_wait_event_kind {
    TASK_WAIT_EVENT_NONE = 0,
    TASK_WAIT_EVENT_WAITABLE = 1,
    TASK_WAIT_EVENT_QUEUE = 2,
    TASK_WAIT_EVENT_SIGNAL = 3,
    TASK_WAIT_EVENT_COMPLETION = 4
};

enum task_wait_event_class {
    TASK_WAIT_CLASS_NONE = 0,
    TASK_WAIT_CLASS_GENERIC = 1,
    TASK_WAIT_CLASS_IPC = 2,
    TASK_WAIT_CLASS_INPUT = 3,
    TASK_WAIT_CLASS_STORAGE = 4,
    TASK_WAIT_CLASS_FILESYSTEM = 5,
    TASK_WAIT_CLASS_VIDEO = 6,
    TASK_WAIT_CLASS_AUDIO = 7,
    TASK_WAIT_CLASS_NETWORK = 8,
    TASK_WAIT_CLASS_SUPERVISION = 9
};

struct task_snapshot_entry {
    uint32_t pid;
    uint32_t kind;
    uint32_t state;
    int32_t current_cpu;
    int32_t preferred_cpu;
    int32_t last_cpu;
    uint32_t stack_size;
    uint32_t runtime_ticks;
    uint32_t context_switches;
    uint32_t service_type;
    uint32_t priority_tier;
    uint32_t task_class;
    uint32_t service_restart_count;
    uint32_t flags;
    uint32_t wait_result;
    uint32_t wait_event_kind;
    uint32_t wait_event_class;
    uint32_t wait_owner_service;
    uint32_t wait_deadline;
    uint32_t wait_pending_signals;
    uint32_t last_task_event_sequence;
    uint32_t last_task_event_type;
    uint32_t last_task_event_tick;
    uint32_t task_class_pending_events;
    uint32_t task_class_dropped_events;
    char name[TASK_SNAPSHOT_NAME_MAX];
};

struct task_snapshot_summary {
    uint32_t abi_version;
    uint32_t uptime_ticks;
    uint32_t current_pid;
    uint32_t cpu_count;
    uint32_t started_cpu_count;
    uint32_t running_tasks;
    uint32_t ready_tasks;
    uint32_t blocked_tasks;
    uint32_t total_tasks;
    uint32_t kernel_heap_used;
    uint32_t kernel_heap_free;
    uint32_t physmem_total_kb;
    uint32_t physmem_free_kb;
    uint32_t timed_out_waits;
    uint32_t canceled_waits;
    uint32_t pending_event_signals;
    uint32_t latest_task_event_sequence;
    uint32_t task_class_pending_events;
    uint32_t task_class_dropped_events;
};

typedef void (*userland_entry_t)(void);

#endif
