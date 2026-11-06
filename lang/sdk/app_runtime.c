#include <lang/include/vibe_app_runtime.h>
#include <stdarg.h>

#define VIBE_APP_SYSCALL_INPUT_KEY 5
#define VIBE_APP_SYSCALL_TIME_TICKS 7
#define VIBE_APP_SYSCALL_YIELD 10
#define VIBE_APP_SYSCALL_WRITE_DEBUG 11
#define VIBE_APP_SYSCALL_TEXT_PUTC 12
#define VIBE_APP_SYSCALL_AUDIO_GETINFO 51
#define VIBE_APP_SYSCALL_AUDIO_MIXER_READ 52
#define VIBE_APP_SYSCALL_AUDIO_MIXER_WRITE 53
#define VIBE_APP_SYSCALL_AUDIO_CONTROL_INFO 54
#define VIBE_APP_SYSCALL_AUDIO_SET_PARAMS 60
#define VIBE_APP_SYSCALL_AUDIO_START 61
#define VIBE_APP_SYSCALL_AUDIO_STOP 62
#define VIBE_APP_SYSCALL_AUDIO_WRITE 63
#define VIBE_APP_SYSCALL_AUDIO_GET_STATUS 64
#define VIBE_APP_SYSCALL_AUDIO_READ 75
#define VIBE_APP_SYSCALL_AUDIO_EVENT_SUBSCRIBE 81
#define VIBE_APP_SYSCALL_AUDIO_EVENT_RECV 82
#define VIBE_APP_SYSCALL_NETWORK_EVENT_SUBSCRIBE 86
#define VIBE_APP_SYSCALL_NETWORK_EVENT_RECV 87
#define VIBE_APP_SYSCALL_NETWORK_GETINFO 55
#define VIBE_APP_SYSCALL_NETWORK_GET_STATUS 56
#define VIBE_APP_SYSCALL_NETWORK_SCAN 57
#define VIBE_APP_SYSCALL_NETWORK_CONNECT_WIFI 58
#define VIBE_APP_SYSCALL_NETWORK_DISCONNECT 59
#define VIBE_APP_SYSCALL_NETWORK_SOCKET 65
#define VIBE_APP_SYSCALL_NETWORK_BIND 66
#define VIBE_APP_SYSCALL_NETWORK_CONNECT 67
#define VIBE_APP_SYSCALL_NETWORK_SEND 68
#define VIBE_APP_SYSCALL_NETWORK_RECV 69
#define VIBE_APP_SYSCALL_NETWORK_CLOSE 70
#define VIBE_APP_SYSCALL_NETWORK_LISTEN 71
#define VIBE_APP_SYSCALL_NETWORK_ACCEPT 72
#define VIBE_APP_SYSCALL_NETWORK_CONNECT_ETHERNET 73
#define VIBE_APP_SYSCALL_NETWORK_CONFIGURE_ETHERNET 74
#define EOF (-1)
#define VIBE_APP_CLOCK_HZ 100u

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
};

#define VIBE_APP_MAX_OPEN_FILES 16
#define VIBE_FILE_FLAG_READ 0x01
#define VIBE_FILE_FLAG_WRITE 0x02
#define VIBE_FILE_FLAG_APPEND 0x04
#define VIBE_FILE_FLAG_CONSOLE 0x08

typedef struct FILE {
    int fd;
    int flags;
    int error;
    int eof;
    long pos;
    unsigned char *buf;
    int size;
    int capacity;
    int dirty;
    int in_use;
    int ungot_valid;
    int ungot_char;
    char path[256];
} FILE;

static const struct vibe_app_context *g_app_ctx = 0;
static struct heap_block *g_heap_head = 0;
static volatile int g_heap_lock = 0;
static void (*g_atexit_handlers[16])(void);
static int g_atexit_count = 0;
static FILE g_file_pool[VIBE_APP_MAX_OPEN_FILES];
static int g_current_thread_id = 0;
static int g_next_file_fd = 3;

#define VIBE_APP_MAX_THREADS 8
static vibe_app_thread_t g_thread_pool[VIBE_APP_MAX_THREADS];

static int vibe_app_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

int syscall(int num, ...) {
    va_list ap;
    va_start(ap, num);
    int a = va_arg(ap, int);
    int b = va_arg(ap, int);
    int c = va_arg(ap, int);
    int d = va_arg(ap, int);
    int e = va_arg(ap, int);
    va_end(ap);
    return vibe_app_syscall5(num, a, b, c, d, e);
}

static void heap_init(void *base, size_t size) {
    g_heap_head = 0;
    if (base == 0 || size <= sizeof(struct heap_block)) {
        return;
    }

    g_heap_head = (struct heap_block *)base;
    g_heap_head->size = size - sizeof(struct heap_block);
    g_heap_head->free = 1;
    g_heap_head->next = 0;
}

static void *block_payload(struct heap_block *block) {
    return (void *)(block + 1);
}

static struct heap_block *payload_block(void *ptr) {
    return ((struct heap_block *)ptr) - 1;
}

static void split_block(struct heap_block *block, size_t size) {
    struct heap_block *next;

    if (block->size <= size + sizeof(struct heap_block) + 8u) {
        return;
    }

    next = (struct heap_block *)((uint8_t *)block_payload(block) + size);
    next->size = block->size - size - sizeof(struct heap_block);
    next->free = 1;
    next->next = block->next;
    block->size = size;
    block->next = next;
}

static void coalesce_blocks(void) {
    struct heap_block *block = g_heap_head;

    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += sizeof(struct heap_block) + block->next->size;
            block->next = block->next->next;
        } else {
            block = block->next;
        }
    }
}

static void heap_lock(void) {
    while (__sync_lock_test_and_set(&g_heap_lock, 1) != 0) {
        (void)vibe_app_syscall5(VIBE_APP_SYSCALL_YIELD, 0, 0, 0, 0, 0);
    }
}

static void heap_unlock(void) {
    __sync_synchronize();
    __sync_lock_release(&g_heap_lock);
}

const struct vibe_app_context *vibe_app_get_context(void) {
    return g_app_ctx;
}

void vibe_app_runtime_init(const struct vibe_app_context *ctx) {
    g_app_ctx = ctx;
    g_current_thread_id = 0;
    g_atexit_count = 0;
    memset(g_atexit_handlers, 0, sizeof(g_atexit_handlers));
    memset(g_file_pool, 0, sizeof(g_file_pool));
    memset(g_thread_pool, 0, sizeof(g_thread_pool));
    g_next_file_fd = 3;
    g_heap_lock = 0;
    if (ctx) {
        heap_init(ctx->heap_base, ctx->heap_size);
        if (ctx->host && ctx->host->write_debug) {
            ctx->host->write_debug("vibe_app_runtime: heap ready\n");
        }
    } else {
        g_heap_head = 0;
    }
}

void vibe_app_console_putc(char c) {
    if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->console_putc) {
        g_app_ctx->host->console_putc(c);
        return;
    }
    (void)vibe_app_syscall5(VIBE_APP_SYSCALL_TEXT_PUTC, (int)(unsigned char)c, 0, 0, 0, 0);
}

void vibe_app_console_write(const char *text) {
    if (text == 0) {
        return;
    }
    if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->console_write) {
        g_app_ctx->host->console_write(text);
        return;
    }
    vibe_app_syscall5(39, (int)(uintptr_t)text, 0, 0, 0, 0);
}

int vibe_app_poll_key(void) {
    if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->poll_key) {
        return g_app_ctx->host->poll_key();
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_INPUT_KEY, 0, 0, 0, 0, 0);
}

static int vibe_app_run_one_pending_thread(void) {
    int i;

    for (i = 0; i < VIBE_APP_MAX_THREADS; ++i) {
        vibe_app_thread_t *thread = &g_thread_pool[i];
        int previous_id;

        if (!thread->in_use || thread->started || thread->finished || !thread->fn) {
            continue;
        }
        thread->started = 1;
        previous_id = g_current_thread_id;
        g_current_thread_id = thread->id;
        thread->result = thread->fn(thread->arg);
        thread->finished = 1;
        g_current_thread_id = previous_id;
        if (thread->detached) {
            memset(thread, 0, sizeof(*thread));
        }
        return 1;
    }
    return 0;
}

void vibe_app_yield(void) {
    if (vibe_app_run_one_pending_thread()) {
        return;
    }
    if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->yield) {
        g_app_ctx->host->yield();
        return;
    }
    (void)vibe_app_syscall5(VIBE_APP_SYSCALL_YIELD, 0, 0, 0, 0, 0);
}

int vibe_app_read_file(const char *path, const char **data_out, int *size_out) {
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->read_file) {
        return -1;
    }
    return g_app_ctx->host->read_file(path, data_out, size_out);
}

int vibe_app_write_file(const char *path, const void *data, int size) {
    if (!path || (!data && size > 0) || size < 0) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->write_file) {
        return -1;
    }
    return g_app_ctx->host->write_file(path, data, size);
}

int vibe_app_create_dir(const char *path) {
    if (!path) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->create_dir) {
        return -1;
    }
    return g_app_ctx->host->create_dir(path);
}

int vibe_app_open(const char *path, int flags) {
    if (!path) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->open_file) {
        return -1;
    }
    return g_app_ctx->host->open_file(path, flags);
}

int vibe_app_read(int fd, void *buf, int size) {
    if (!buf || size < 0) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->read_fd) {
        return -1;
    }
    return g_app_ctx->host->read_fd(fd, buf, size);
}

int vibe_app_write(int fd, const void *buf, int size) {
    if ((!buf && size > 0) || size < 0) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->write_fd) {
        return -1;
    }
    return g_app_ctx->host->write_fd(fd, buf, size);
}

int vibe_app_close(int fd) {
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->close_fd) {
        return -1;
    }
    return g_app_ctx->host->close_fd(fd);
}

int vibe_app_lseek(int fd, int offset, int whence) {
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->seek_fd) {
        return -1;
    }
    return g_app_ctx->host->seek_fd(fd, offset, whence);
}

int vibe_app_sync(void) {
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->sync_filesystem) {
        return -1;
    }
    return g_app_ctx->host->sync_filesystem();
}

int vibe_app_stat(const char *path, struct vibe_app_stat *stat_out) {
    if (!path || !stat_out) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->stat_path) {
        return -1;
    }
    return g_app_ctx->host->stat_path(path, stat_out);
}

int vibe_app_fstat(int fd, struct vibe_app_stat *stat_out) {
    if (!stat_out) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->fstat_fd) {
        return -1;
    }
    return g_app_ctx->host->fstat_fd(fd, stat_out);
}

const char *vibe_app_getenv(const char *name) {
    if (!name) {
        return 0;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->getenv_value) {
        return 0;
    }
    return g_app_ctx->host->getenv_value(name);
}

unsigned int vibe_app_ticks(void) {
    return (unsigned int)vibe_app_syscall5(VIBE_APP_SYSCALL_TIME_TICKS, 0, 0, 0, 0, 0);
}

unsigned int vibe_app_clock_hz(void) {
    return VIBE_APP_CLOCK_HZ;
}

unsigned long long vibe_app_millis(void) {
    return ((unsigned long long)vibe_app_ticks() * 1000ull) / (unsigned long long)VIBE_APP_CLOCK_HZ;
}

int vibe_app_sleep_ms(unsigned int ms) {
    unsigned int start = vibe_app_ticks();
    unsigned int wait_ticks = (ms * VIBE_APP_CLOCK_HZ + 999u) / 1000u;

    if (wait_ticks == 0u && ms > 0u) {
        wait_ticks = 1u;
    }
    while ((unsigned int)(vibe_app_ticks() - start) < wait_ticks) {
        vibe_app_yield();
    }
    return 0;
}

int vibe_app_getcwd(char *buf, int max_len) {
    if (!buf || max_len <= 0) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->getcwd) {
        return -1;
    }
    return g_app_ctx->host->getcwd(buf, max_len);
}

int vibe_app_remove_dir(const char *path) {
    if (!path) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->remove_dir) {
        return -1;
    }
    return g_app_ctx->host->remove_dir(path);
}

int vibe_app_keyboard_set_layout(const char *name) {
    if (!name) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->keyboard_set_layout) {
        return -1;
    }
    return g_app_ctx->host->keyboard_set_layout(name);
}

int vibe_app_keyboard_get_layout(char *buf, int max_len) {
    if (!buf || max_len <= 0) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->keyboard_get_layout) {
        return -1;
    }
    return g_app_ctx->host->keyboard_get_layout(buf, max_len);
}

int vibe_app_keyboard_get_available_layouts(char *buf, int max_len) {
    if (!buf || max_len <= 0) {
        return -1;
    }
    if (!g_app_ctx || !g_app_ctx->host || !g_app_ctx->host->keyboard_get_available_layouts) {
        return -1;
    }
    return g_app_ctx->host->keyboard_get_available_layouts(buf, max_len);
}

int vibe_app_read_line(char *buf, int max_len, const char *prompt) {
    int len = 0;

    if (!buf || max_len <= 0) {
        return -1;
    }

    if (prompt) {
        vibe_app_console_write(prompt);
    }

    for (;;) {
        int c = vibe_app_poll_key();

        if (c == 0) {
            /*
             * No yield here (bug #19, 31/10, same fix as the built-in
             * shell's read_line): vibe_app_poll_key is a blocking IPC wait
             * (service reply budget) and paces the loop by itself. The
             * yield dropped the caller out of the SIGNALED wake cycle into
             * the tier-6 aging pool — the next poll only ran after the
             * starvation ramp while keys already sat in the driver queue.
             */
            continue;
        }

        if (c == '\r') {
            c = '\n';
        }

        if (c == 3) {
            buf[0] = '\0';
            vibe_app_console_write("^C\n");
            return -1;
        }

        if (c == '\n') {
            buf[len] = '\0';
            vibe_app_console_putc('\n');
            return len;
        }

        if ((c == '\b' || c == 127) && len > 0) {
            --len;
            buf[len] = '\0';
            vibe_app_console_write("\b \b");
            continue;
        }

        if (c >= 32 && c < 127 && len + 1 < max_len) {
            buf[len++] = (char)c;
            buf[len] = '\0';
            vibe_app_console_putc((char)c);
        }
    }
}

void *vibe_app_malloc(size_t size) {
    struct heap_block *block;
    int guard = 0;
    int trace = size >= 65536u;
    void *result = 0;

    if (size == 0u) {
        return 0;
    }
    if (size & 7u) {
        size += 8u - (size & 7u);
    }

    if (trace && g_app_ctx && g_app_ctx->host && g_app_ctx->host->write_debug) {
        g_app_ctx->host->write_debug("vibe_app_malloc: begin\n");
    }

    heap_lock();
    block = g_heap_head;
    while (block) {
        if (++guard > 16384) {
            if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->write_debug) {
                g_app_ctx->host->write_debug("vibe_app_malloc: heap loop detected\n");
            }
            heap_unlock();
            return 0;
        }
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = 0;
            result = block_payload(block);
            heap_unlock();
            if (trace && g_app_ctx && g_app_ctx->host && g_app_ctx->host->write_debug) {
                g_app_ctx->host->write_debug("vibe_app_malloc: ok\n");
            }
            return result;
        }
        block = block->next;
    }
    heap_unlock();

    if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->write_debug) {
        g_app_ctx->host->write_debug("vibe_app_malloc: out of memory\n");
    } else {
        (void)vibe_app_syscall5(VIBE_APP_SYSCALL_WRITE_DEBUG,
                                (int)(uintptr_t)"vibe_app_malloc: out of memory\n",
                                0,
                                0,
                                0,
                                0);
    }
    return 0;
}

void vibe_app_free(void *ptr) {
    if (!ptr) {
        return;
    }
    heap_lock();
    payload_block(ptr)->free = 1;
    coalesce_blocks();
    heap_unlock();
}

void *vibe_app_realloc(void *ptr, size_t size) {
    void *new_ptr;
    struct heap_block *block;
    size_t old_size;

    if (ptr == 0) {
        return vibe_app_malloc(size);
    }
    if (size == 0u) {
        vibe_app_free(ptr);
        return 0;
    }

    heap_lock();
    block = payload_block(ptr);
    old_size = block->size;
    if (old_size >= size) {
        heap_unlock();
        return ptr;
    }
    heap_unlock();

    new_ptr = vibe_app_malloc(size);
    if (!new_ptr) {
        return 0;
    }
    memcpy(new_ptr, ptr, old_size);
    vibe_app_free(ptr);
    return new_ptr;
}

int atexit(void (*fn)(void)) {
    if (!fn || g_atexit_count >= (int)(sizeof(g_atexit_handlers) / sizeof(g_atexit_handlers[0]))) {
        return -1;
    }
    g_atexit_handlers[g_atexit_count++] = fn;
    return 0;
}

void vibe_app_run_atexit(void) {
    while (g_atexit_count > 0) {
        void (*fn)(void) = g_atexit_handlers[--g_atexit_count];
        if (fn) {
            fn();
        }
    }
}

void exit(int status) {
    (void)status;
    vibe_app_run_atexit();
    for (;;) {
        vibe_app_yield();
    }
}

void abort(void) {
    if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->write_debug) {
        g_app_ctx->host->write_debug("vibe app abort\n");
    }
    exit(1);
}

/*
 * GCC stack protector support for userland apps (P4 classe #5 probe): the
 * shared CFLAGS enable -fstack-protector for every object including the app
 * SDK build, so the guard symbol and fail path live in the runtime linked
 * into every app — same naming channel as abort() (host write_debug), then
 * the normal exit path. The kernel-side counterpart is kernel/stackguard.c.
 */
unsigned long __stack_chk_guard = 0x56494245UL; /* "VIBE" */

__attribute__((noreturn)) void __stack_chk_fail(void) {
    if (g_app_ctx && g_app_ctx->host && g_app_ctx->host->write_debug) {
        g_app_ctx->host->write_debug("vibe app: stack check fail (local buffer overflow)\n");
    }
    exit(1);
    for (;;) {
        vibe_app_yield();
    }
}

static vibe_app_thread_t *vibe_find_thread(vibe_app_thread_t *thread) {
    int i;

    if (!thread || thread->id <= 0) {
        return 0;
    }
    for (i = 0; i < VIBE_APP_MAX_THREADS; ++i) {
        if (g_thread_pool[i].in_use && g_thread_pool[i].id == thread->id) {
            return &g_thread_pool[i];
        }
    }
    return 0;
}

int vibe_app_thread_create(vibe_app_thread_t *thread, vibe_app_thread_fn fn, void *arg) {
    int i;

    if (!thread || !fn) {
        return -1;
    }
    for (i = 0; i < VIBE_APP_MAX_THREADS; ++i) {
        if (!g_thread_pool[i].in_use) {
            memset(&g_thread_pool[i], 0, sizeof(g_thread_pool[i]));
            g_thread_pool[i].in_use = 1;
            g_thread_pool[i].id = i + 1;
            g_thread_pool[i].fn = fn;
            g_thread_pool[i].arg = arg;
            *thread = g_thread_pool[i];
            return 0;
        }
    }
    return -1;
}

int vibe_app_thread_join(vibe_app_thread_t *thread, int *result_out) {
    vibe_app_thread_t *real = vibe_find_thread(thread);

    if (!real) {
        return -1;
    }
    while (!real->finished) {
        vibe_app_yield();
    }
    if (result_out) {
        *result_out = real->result;
    }
    memset(real, 0, sizeof(*real));
    if (thread) {
        memset(thread, 0, sizeof(*thread));
    }
    return 0;
}

int vibe_app_thread_detach(vibe_app_thread_t *thread) {
    vibe_app_thread_t *real = vibe_find_thread(thread);

    if (!real) {
        return -1;
    }
    real->detached = 1;
    if (real->finished) {
        memset(real, 0, sizeof(*real));
    }
    return 0;
}

int vibe_app_thread_yield(void) {
    vibe_app_yield();
    return 0;
}

int vibe_app_mutex_init(vibe_app_mutex_t *mutex) {
    if (!mutex) {
        return -1;
    }
    memset(mutex, 0, sizeof(*mutex));
    mutex->initialized = 1;
    return 0;
}

int vibe_app_mutex_trylock(vibe_app_mutex_t *mutex) {
    if (!mutex || !mutex->initialized) {
        return -1;
    }
    if (!mutex->locked) {
        mutex->locked = 1;
        mutex->owner = g_current_thread_id;
        return 0;
    }
    if (mutex->owner == g_current_thread_id) {
        return 0;
    }
    return 1;
}

int vibe_app_mutex_lock(vibe_app_mutex_t *mutex) {
    int rc;

    if (!mutex || !mutex->initialized) {
        return -1;
    }
    for (;;) {
        rc = vibe_app_mutex_trylock(mutex);
        if (rc == 0) {
            return 0;
        }
        if (rc < 0) {
            return -1;
        }
        vibe_app_yield();
    }
}

int vibe_app_mutex_unlock(vibe_app_mutex_t *mutex) {
    if (!mutex || !mutex->initialized || !mutex->locked) {
        return -1;
    }
    if (mutex->owner != g_current_thread_id) {
        return -1;
    }
    mutex->locked = 0;
    mutex->owner = 0;
    return 0;
}

int vibe_app_cond_init(vibe_app_cond_t *cond) {
    if (!cond) {
        return -1;
    }
    memset(cond, 0, sizeof(*cond));
    cond->initialized = 1;
    return 0;
}

int vibe_app_cond_signal(vibe_app_cond_t *cond) {
    if (!cond || !cond->initialized) {
        return -1;
    }
    cond->sequence += 1u;
    return 0;
}

int vibe_app_cond_broadcast(vibe_app_cond_t *cond) {
    return vibe_app_cond_signal(cond);
}

int vibe_app_cond_timedwait_ms(vibe_app_cond_t *cond, vibe_app_mutex_t *mutex, unsigned int timeout_ms) {
    unsigned int start;
    unsigned int target_ticks;
    unsigned int observed;

    if (!cond || !cond->initialized || !mutex) {
        return -1;
    }
    observed = cond->sequence;
    if (vibe_app_mutex_unlock(mutex) != 0) {
        return -1;
    }
    start = vibe_app_ticks();
    target_ticks = (timeout_ms * VIBE_APP_CLOCK_HZ + 999u) / 1000u;
    if (timeout_ms > 0u && target_ticks == 0u) {
        target_ticks = 1u;
    }
    while (cond->sequence == observed) {
        if (timeout_ms > 0u && (unsigned int)(vibe_app_ticks() - start) >= target_ticks) {
            break;
        }
        vibe_app_yield();
    }
    if (vibe_app_mutex_lock(mutex) != 0) {
        return -1;
    }
    return cond->sequence != observed ? 0 : 1;
}

int vibe_app_cond_wait(vibe_app_cond_t *cond, vibe_app_mutex_t *mutex) {
    return vibe_app_cond_timedwait_ms(cond, mutex, 0u);
}

void *malloc(size_t size) {
    return vibe_app_malloc(size);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = vibe_app_malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void free(void *ptr) {
    vibe_app_free(ptr);
}

void *realloc(void *ptr, size_t size) {
    return vibe_app_realloc(ptr, size);
}

void *memcpy(void *dst, const void *src, size_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    size_t i;

    for (i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    if (out < in) {
        return memcpy(dst, src, size);
    }

    while (size > 0u) {
        --size;
        out[size] = in[size];
    }
    return dst;
}

void *memset(void *dst, int value, size_t size) {
    uint8_t *out = (uint8_t *)dst;
    size_t i;

    for (i = 0; i < size; ++i) {
        out[i] = (uint8_t)value;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t size) {
    const uint8_t *lhs = (const uint8_t *)a;
    const uint8_t *rhs = (const uint8_t *)b;
    size_t i;

    for (i = 0; i < size; ++i) {
        if (lhs[i] != rhs[i]) {
            return (int)lhs[i] - (int)rhs[i];
        }
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t len = 0;

    while (text && text[len] != '\0') {
        ++len;
    }
    return len;
}

int vibe_app_audio_get_info(struct mk_audio_info *info) {
    if (info == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_GETINFO,
                             (int)(uintptr_t)info,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_audio_get_status(struct audio_status *status) {
    if (status == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_GET_STATUS,
                             (int)(uintptr_t)status,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_audio_set_params(const struct audio_swpar *params) {
    if (params == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_SET_PARAMS,
                             (int)(uintptr_t)params,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_audio_start(void) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_START, 0, 0, 0, 0, 0);
}

int vibe_app_audio_stop(void) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_STOP, 0, 0, 0, 0, 0);
}

int vibe_app_audio_write(const void *data, uint32_t size) {
    if (data == 0 || size == 0u) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_WRITE,
                             (int)(uintptr_t)data,
                             (int)size,
                             0,
                             0,
                             0);
}

int vibe_app_audio_read(void *data, uint32_t size) {
    if (data == 0 || size == 0u) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_READ,
                             (int)(uintptr_t)data,
                             (int)size,
                             0,
                             0,
                             0);
}

int vibe_app_audio_event_subscribe(void) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_EVENT_SUBSCRIBE, 0, 0, 0, 0, 0);
}

int vibe_app_audio_event_receive(struct mk_audio_event *event, uint32_t timeout_ticks) {
    if (event == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_EVENT_RECV,
                             (int)(uintptr_t)event,
                             (int)timeout_ticks,
                             0,
                             0,
                             0);
}

int vibe_app_audio_get_control_info(uint32_t index, struct mk_audio_control_info *info) {
    if (info == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_CONTROL_INFO,
                             (int)index,
                             (int)(uintptr_t)info,
                             0,
                             0,
                             0);
}

int vibe_app_audio_mixer_read(mixer_ctrl_t *control) {
    if (control == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_MIXER_READ,
                             (int)(uintptr_t)control,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_audio_mixer_write(const mixer_ctrl_t *control) {
    if (control == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_AUDIO_MIXER_WRITE,
                             (int)(uintptr_t)control,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_network_get_info(struct mk_network_info *info) {
    if (info == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_GETINFO,
                             (int)(uintptr_t)info,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_network_get_status(struct mk_network_status *status) {
    if (status == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_GET_STATUS,
                             (int)(uintptr_t)status,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_network_scan(uint32_t index, struct mk_network_scan_info *info) {
    if (info == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_SCAN,
                             (int)index,
                             (int)(uintptr_t)info,
                             0,
                             0,
                             0);
}

int vibe_app_network_connect_wifi(const struct mk_network_connect_request *request) {
    if (request == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_CONNECT_WIFI,
                             (int)(uintptr_t)request,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_network_connect_ethernet(const char *if_name) {
    if (if_name == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_CONNECT_ETHERNET,
                             (int)(uintptr_t)if_name,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_network_configure_ethernet(const struct mk_network_ethernet_config *config) {
    if (config == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_CONFIGURE_ETHERNET,
                             (int)(uintptr_t)config,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_network_disconnect(const char *if_name) {
    if (if_name == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_DISCONNECT,
                             (int)(uintptr_t)if_name,
                             0,
                             0,
                             0,
                             0);
}

int vibe_app_network_socket(uint32_t domain, uint32_t type, uint32_t protocol) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_SOCKET,
                             (int)domain,
                             (int)type,
                             (int)protocol,
                             0,
                             0);
}

int vibe_app_network_bind(int handle, const struct sockaddr *address, uint32_t address_length) {
    if (address == 0 || address_length == 0u) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_BIND,
                             handle,
                             (int)(uintptr_t)address,
                             (int)address_length,
                             0,
                             0);
}

int vibe_app_network_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length) {
    if (address == 0 || address_length == 0u) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_CONNECT,
                             handle,
                             (int)(uintptr_t)address,
                             (int)address_length,
                             0,
                             0);
}

int vibe_app_network_send(int handle, const void *data, uint32_t size) {
    if (data == 0 || size == 0u) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_SEND,
                             handle,
                             (int)(uintptr_t)data,
                             (int)size,
                             0,
                             0);
}

int vibe_app_network_recv(int handle, void *buffer, uint32_t size) {
    if (buffer == 0 || size == 0u) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_RECV,
                             handle,
                             (int)(uintptr_t)buffer,
                             (int)size,
                             0,
                             0);
}

int vibe_app_network_close(int handle) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_CLOSE, handle, 0, 0, 0, 0);
}

int vibe_app_network_listen(int handle, int backlog) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_LISTEN, handle, backlog, 0, 0, 0);
}

int vibe_app_network_accept(int handle) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_ACCEPT, handle, 0, 0, 0, 0);
}

int vibe_app_network_event_subscribe(void) {
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_EVENT_SUBSCRIBE, 0, 0, 0, 0, 0);
}

int vibe_app_network_event_receive(struct mk_network_event *event, uint32_t timeout_ticks) {
    if (event == 0) {
        return -1;
    }
    return vibe_app_syscall5(VIBE_APP_SYSCALL_NETWORK_EVENT_RECV,
                             (int)(uintptr_t)event,
                             (int)timeout_ticks,
                             0,
                             0,
                             0);
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return (unsigned char)*a - (unsigned char)*b;
        }
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t size) {
    size_t i;

    for (i = 0; i < size; ++i) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    size_t i = 0;

    do {
        dst[i] = src[i];
    } while (src[i++] != '\0');
    return dst;
}

char *strchr(const char *text, int c) {
    while (*text != '\0') {
        if (*text == (char)c) {
            return (char *)text;
        }
        ++text;
    }
    return c == 0 ? (char *)text : 0;
}

/* ============ STDIO Implementation ============ */

static FILE _stdin = {0, VIBE_FILE_FLAG_READ | VIBE_FILE_FLAG_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, {0}};
static FILE _stdout = {1, VIBE_FILE_FLAG_WRITE | VIBE_FILE_FLAG_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, {0}};
static FILE _stderr = {2, VIBE_FILE_FLAG_WRITE | VIBE_FILE_FLAG_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, {0}};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

static int vibe_is_console_file(FILE *f) {
    return f == stdin || f == stdout || f == stderr || (f && (f->flags & VIBE_FILE_FLAG_CONSOLE));
}

static int vibe_copy_limited(char *dst, int max_len, const char *src) {
    int i = 0;
    if (!dst || max_len <= 0) {
        return 0;
    }
    while (src && src[i] != '\0' && i < (max_len - 1)) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
    return i;
}

static int vibe_file_reserve(FILE *f, int needed);

static FILE *vibe_alloc_file(void) {
    for (int i = 0; i < VIBE_APP_MAX_OPEN_FILES; ++i) {
        if (!g_file_pool[i].in_use) {
            memset(&g_file_pool[i], 0, sizeof(g_file_pool[i]));
            g_file_pool[i].in_use = 1;
            g_file_pool[i].fd = g_next_file_fd++;
            return &g_file_pool[i];
        }
    }
    return 0;
}

static void vibe_file_clear_pushback(FILE *f) {
    if (!f) {
        return;
    }
    f->ungot_valid = 0;
    f->ungot_char = 0;
}

static int vibe_load_file_contents(FILE *f, const char *filename) {
    struct vibe_app_stat stat_buf;
    int fd;

    if (!f || !filename) {
        return -1;
    }
    if (vibe_app_stat(filename, &stat_buf) != 0) {
        return -1;
    }
    if (stat_buf.is_dir) {
        return -1;
    }
    if (stat_buf.size > 0 && vibe_file_reserve(f, stat_buf.size) != 0) {
        return -1;
    }

    fd = vibe_app_open(filename, 0);
    if (fd < 0) {
        return -1;
    }

    f->size = 0;
    while (f->size < stat_buf.size) {
        int chunk = stat_buf.size - f->size;
        int rc;

        if (chunk > 256) {
            chunk = 256;
        }
        rc = vibe_app_read(fd, f->buf + f->size, chunk);
        if (rc <= 0) {
            (void)vibe_app_close(fd);
            return -1;
        }
        f->size += rc;
    }
    (void)vibe_app_close(fd);
    return 0;
}

static int vibe_file_reserve(FILE *f, int needed) {
    int new_capacity;
    unsigned char *new_buf;

    if (!f || needed < 0) {
        return -1;
    }
    if (needed <= f->capacity) {
        return 0;
    }
    new_capacity = f->capacity > 0 ? f->capacity : 256;
    while (new_capacity < needed) {
        if (new_capacity > (1 << 29)) {
            return -1;
        }
        new_capacity *= 2;
    }
    new_buf = (unsigned char *)realloc(f->buf, (size_t)new_capacity);
    if (!new_buf) {
        return -1;
    }
    f->buf = new_buf;
    f->capacity = new_capacity;
    return 0;
}

static int vibe_file_flush(FILE *f) {
    if (!f) {
        return -1;
    }
    if (vibe_is_console_file(f)) {
        return 0;
    }
    if ((f->flags & VIBE_FILE_FLAG_WRITE) == 0) {
        return 0;
    }
    if (!f->dirty) {
        return 0;
    }
    if (vibe_app_write_file(f->path, f->buf, f->size) != 0) {
        f->error = 1;
        return -1;
    }
    f->dirty = 0;
    return 0;
}

static int vibe_parse_mode(const char *mode, int *flags_out) {
    int flags = 0;

    if (!mode || !flags_out) {
        return -1;
    }
    switch (mode[0]) {
        case 'r':
            flags = VIBE_FILE_FLAG_READ;
            break;
        case 'w':
            flags = VIBE_FILE_FLAG_WRITE;
            break;
        case 'a':
            flags = VIBE_FILE_FLAG_WRITE | VIBE_FILE_FLAG_APPEND;
            break;
        default:
            return -1;
    }
    if (strchr(mode, '+')) {
        flags |= VIBE_FILE_FLAG_READ | VIBE_FILE_FLAG_WRITE;
    }
    *flags_out = flags;
    return 0;
}

static int vibe_vsnprintf_internal(char *str, size_t size, const char *fmt, va_list ap) {
    size_t written = 0u;

    if (!fmt) {
        return -1;
    }
    if (size == 0u) {
        str = 0;
        size = (size_t)-1;
    }

    while (*fmt && written + 1u < size) {
        if (*fmt != '%') {
            if (str) {
                str[written] = *fmt;
            }
            ++written;
            ++fmt;
            continue;
        }

        ++fmt;
        if (*fmt == '\0') {
            break;
        }
        if (*fmt == '%') {
            if (str) {
                str[written] = '%';
            }
            ++written;
            ++fmt;
            continue;
        }

        {
            int zero_pad = 0;
            int width = 0;
            int precision = -1;

            if (*fmt == '0') {
                zero_pad = 1;
                ++fmt;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = (width * 10) + (*fmt - '0');
                ++fmt;
            }
            if (*fmt == '.') {
                precision = 0;
                ++fmt;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = (precision * 10) + (*fmt - '0');
                    ++fmt;
                }
            }

            if (*fmt == 's') {
                const char *s = va_arg(ap, const char *);

                if (!s) {
                    s = "(null)";
                }
                while (*s && written + 1u < size) {
                    if (str) {
                        str[written] = *s;
                    }
                    ++written;
                    ++s;
                }
                ++fmt;
                continue;
            }

            if (*fmt == 'c') {
                int c = va_arg(ap, int);

                if (str) {
                    str[written] = (char)c;
                }
                ++written;
                ++fmt;
                continue;
            }

            if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' ||
                *fmt == 'x' || *fmt == 'X' || *fmt == 'p') {
                unsigned int base = 10u;
                unsigned int value_u = 0u;
                char tmp[32];
                int pos = 0;
                int upper = (*fmt == 'X');
                int negative = 0;
                int digits;
                int pad_count;

                if (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') {
                    base = 16u;
                }

                if (*fmt == 'p') {
                    uintptr_t p = (uintptr_t)va_arg(ap, void *);
                    value_u = (unsigned int)p;
                } else if (*fmt == 'd' || *fmt == 'i') {
                    int value_s = va_arg(ap, int);

                    if (value_s < 0) {
                        negative = 1;
                        value_u = (unsigned int)(-value_s);
                    } else {
                        value_u = (unsigned int)value_s;
                    }
                } else {
                    value_u = va_arg(ap, unsigned int);
                }

                if (value_u == 0u) {
                    tmp[pos++] = '0';
                } else {
                    while (value_u > 0u && pos < (int)sizeof(tmp)) {
                        unsigned int digit = value_u % base;
                        tmp[pos++] = (char)(digit < 10u ? ('0' + digit)
                                                         : ((upper ? 'A' : 'a') + (digit - 10u)));
                        value_u /= base;
                    }
                }

                digits = pos;
                if (precision > digits) {
                    pad_count = precision - digits;
                } else if (zero_pad && width > digits + negative) {
                    pad_count = width - digits - negative;
                } else {
                    pad_count = 0;
                }

                if (negative) {
                    if (str && written + 1u < size) {
                        str[written] = '-';
                    }
                    ++written;
                }

                if (*fmt == 'p') {
                    if (str && written + 1u < size) {
                        str[written] = '0';
                    }
                    ++written;
                    if (str && written + 1u < size) {
                        str[written] = 'x';
                    }
                    ++written;
                }

                while (pad_count-- > 0 && written + 1u < size) {
                    if (str) {
                        str[written] = '0';
                    }
                    ++written;
                }
                while (pos > 0 && written + 1u < size) {
                    if (str) {
                        str[written] = tmp[--pos];
                    }
                    ++written;
                }
                ++fmt;
                continue;
            }

            if (str) {
                str[written] = '%';
            }
            ++written;
            if (written + 1u < size && str) {
                str[written] = *fmt;
            }
            ++written;
            ++fmt;
        }
    }

    if (str && size > 0u) {
        str[written < size ? written : (size - 1u)] = '\0';
    }
    return (int)written;
}

/* Helper: write formatted output to file */
static int vibe_vfprintf_helper(FILE *f, const char *fmt, va_list ap) {
    char stack_buf[256];
    char *buffer = stack_buf;
    va_list measure_ap;
    va_list write_ap;
    int needed;
    int wrote = -1;

    if (!f || !fmt) {
        return -1;
    }

    va_copy(measure_ap, ap);
    needed = vibe_vsnprintf_internal(0, 0u, fmt, measure_ap);
    va_end(measure_ap);
    if (needed < 0) {
        return -1;
    }

    if ((size_t)needed >= sizeof(stack_buf)) {
        buffer = (char *)malloc((size_t)needed + 1u);
        if (!buffer) {
            return -1;
        }
    }

    va_copy(write_ap, ap);
    wrote = vibe_vsnprintf_internal(buffer, (size_t)needed + 1u, fmt, write_ap);
    va_end(write_ap);
    if (wrote >= 0) {
        if (vibe_is_console_file(f)) {
            vibe_app_console_write(buffer);
        } else if (fwrite(buffer, 1u, (size_t)wrote, f) != (size_t)wrote) {
            wrote = -1;
        }
    }

    if (buffer != stack_buf) {
        free(buffer);
    }
    return wrote;
}

int printf(const char *fmt, ...) {
    va_list ap;
    int ret;
    
    va_start(ap, fmt);
    ret = vibe_vfprintf_helper(stdout, fmt, ap);
    va_end(ap);
    return ret;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    int ret;
    
    if (!f) {
        return -1;
    }
    
    va_start(ap, fmt);
    ret = vibe_vfprintf_helper(f, fmt, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *fmt, va_list ap) {
    return vibe_vfprintf_helper(stdout, fmt, ap);
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    if (!f) {
        return -1;
    }
    return vibe_vfprintf_helper(f, fmt, ap);
}

int sprintf(char *str, const char *fmt, ...) {
    int ret;
    va_list ap;

    va_start(ap, fmt);
    ret = vsprintf(str, fmt, ap);
    va_end(ap);
    return ret;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    int ret;
    va_list ap;

    va_start(ap, fmt);
    ret = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *str, const char *fmt, va_list ap) {
    return vibe_vsnprintf_internal(str, (size_t)-1, fmt, ap);
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    return vibe_vsnprintf_internal(str, size, fmt, ap);
}

/* File operations - minimal stubs */
FILE *fopen(const char *filename, const char *mode) {
    FILE *f;
    int flags = 0;

    if (!filename || vibe_parse_mode(mode, &flags) != 0) {
        return 0;
    }
    f = vibe_alloc_file();
    if (!f) {
        return 0;
    }
    f->flags = flags;
    vibe_copy_limited(f->path, (int)sizeof(f->path), filename);

    if ((flags & VIBE_FILE_FLAG_APPEND) || mode[0] == 'r' || strchr(mode, '+')) {
        if (vibe_load_file_contents(f, filename) == 0) {
            if (flags & VIBE_FILE_FLAG_APPEND) {
                f->pos = f->size;
            }
        } else if (mode[0] == 'r') {
            f->in_use = 0;
            return 0;
        }
    }
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    /* Not implemented in this embedded context */
    (void)fd;
    (void)mode;
    return 0;
}

int fclose(FILE *stream) {
    if (!stream) {
        return -1;
    }
    if (vibe_is_console_file(stream)) {
        return 0;
    }
    if (vibe_file_flush(stream) != 0) {
        free(stream->buf);
        stream->buf = 0;
        stream->in_use = 0;
        return -1;
    }
    free(stream->buf);
    stream->buf = 0;
    stream->in_use = 0;
    vibe_file_clear_pushback(stream);
    return 0;
}

int fflush(FILE *stream) {
    if (!stream) {
        for (int i = 0; i < VIBE_APP_MAX_OPEN_FILES; ++i) {
            if (g_file_pool[i].in_use && vibe_file_flush(&g_file_pool[i]) != 0) {
                return -1;
            }
        }
        return 0;
    }
    return vibe_file_flush(stream);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    size_t available;
    unsigned char *out = (unsigned char *)ptr;
    size_t copied = 0u;

    if (!ptr || !stream || size == 0u || nmemb == 0u || vibe_is_console_file(stream)) {
        return 0;
    }
    total = size * nmemb;
    if (stream->ungot_valid && total > 0u) {
        out[copied++] = (unsigned char)stream->ungot_char;
        stream->ungot_valid = 0;
        if (copied == total) {
            stream->eof = 0;
            return copied / size;
        }
    }
    if (stream->pos >= stream->size) {
        stream->eof = 1;
        return copied / size;
    }
    available = (size_t)(stream->size - stream->pos);
    total -= copied;
    if (total > available) {
        total = available;
        stream->eof = 1;
    } else {
        stream->eof = 0;
    }
    memcpy(out + copied, stream->buf + stream->pos, total);
    stream->pos += (long)total;
    copied += total;
    return copied / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;

    if (!ptr || !stream || size == 0u || nmemb == 0u) {
        return 0;
    }
    if (stream == stdout || stream == stderr) {
        const unsigned char *bytes = (const unsigned char *)ptr;
        total = size * nmemb;
        for (size_t i = 0; i < total; ++i) {
            vibe_app_console_putc((char)bytes[i]);
        }
        return nmemb;
    }
    if ((stream->flags & VIBE_FILE_FLAG_WRITE) == 0) {
        stream->error = 1;
        return 0;
    }
    total = size * nmemb;
    if (stream->flags & VIBE_FILE_FLAG_APPEND) {
        stream->pos = stream->size;
    }
    if (vibe_file_reserve(stream, (int)(stream->pos + (long)total)) != 0) {
        stream->error = 1;
        return 0;
    }
    memcpy(stream->buf + stream->pos, ptr, total);
    stream->pos += (long)total;
    if (stream->pos > stream->size) {
        stream->size = (int)stream->pos;
    }
    stream->dirty = 1;
    return nmemb;
}

int fgetc(FILE *stream) {
    unsigned char c;

    if (!stream) {
        return EOF;
    }
    if (stream->ungot_valid) {
        stream->ungot_valid = 0;
        stream->eof = 0;
        return stream->ungot_char;
    }
    if (stream == stdin) {
        return vibe_app_poll_key();
    }
    if (fread(&c, 1u, 1u, stream) != 1u) {
        return EOF;
    }
    return (int)c;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;

    if (!stream) {
        return EOF;
    }
    if (stream == stdout || stream == stderr) {
        vibe_app_console_putc((char)c);
        return c;
    }
    return fwrite(&ch, 1u, 1u, stream) == 1u ? c : EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    int i = 0;
    int c;

    if (!s || size <= 1 || !stream) {
        return 0;
    }
    while (i < size - 1) {
        c = fgetc(stream);
        if (c == EOF) {
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    if (i == 0) {
        return 0;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    size_t len;

    if (!s || !stream) {
        return EOF;
    }
    if (stream == stdout || stream == stderr) {
        vibe_app_console_write(s);
        return 0;
    }
    len = strlen(s);
    return fwrite(s, 1u, len, stream) == len ? 0 : EOF;
}

int puts(const char *s) {
    if (!s) {
        return EOF;
    }
    vibe_app_console_write(s);
    vibe_app_console_putc('\n');
    return 0;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}

int feof(FILE *stream) {
    if (stream) {
        return stream->eof;
    }
    return 0;
}

int ferror(FILE *stream) {
    if (stream) {
        return stream->error;
    }
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    long base;
    long next;

    if (!stream || vibe_is_console_file(stream)) {
        return -1;
    }
    switch (whence) {
        case 0:
            base = 0;
            break;
        case 1:
            base = stream->pos;
            break;
        case 2:
            base = stream->size;
            break;
        default:
            return -1;
    }
    next = base + offset;
    if (next < 0) {
        return -1;
    }
    stream->pos = next;
    stream->eof = 0;
    vibe_file_clear_pushback(stream);
    return 0;
}

long ftell(FILE *stream) {
    if (stream) {
        return stream->ungot_valid ? (stream->pos - 1) : stream->pos;
    }
    return -1;
}

void rewind(FILE *stream) {
    if (stream) {
        fseek(stream, 0, 0);  /* SEEK_SET */
    }
}

int getchar(void) {
    return vibe_app_poll_key();
}

int putchar(int c) {
    vibe_app_console_putc((char)c);
    return c;
}

int getc(FILE *stream) {
    return fgetc(stream);
}

int putc(int c, FILE *stream) {
    return fputc(c, stream);
}

int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF || stream->ungot_valid) {
        return EOF;
    }
    stream->ungot_valid = 1;
    stream->ungot_char = (unsigned char)c;
    stream->eof = 0;
    return c;
}

int fileno(FILE *stream) {
    if (!stream) {
        return -1;
    }
    return stream->fd;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    (void)stream;
    (void)buf;
    (void)mode;
    (void)size;
    return 0;
}

int fpurge(FILE *stream) {
    if (!stream) {
        return -1;
    }
    vibe_file_clear_pushback(stream);
    stream->eof = 0;
    return 0;
}

unsigned int sleep(unsigned int seconds) {
    (void)vibe_app_sleep_ms(seconds * 1000u);
    return 0;
}

int usleep(unsigned int microseconds) {
    unsigned int ms = microseconds / 1000u;
    if (microseconds > 0u && ms == 0u) {
        ms = 1u;
    }
    return vibe_app_sleep_ms(ms);
}
