#include <lang/include/vibe_app_runtime.h>
#include <stdarg.h>
#include <stdint.h>

#define VIBE_APP_SYSCALL_TIME_TICKS 7
#define VIBE_APP_SYSCALL_YIELD 10
#define VIBE_APP_CLOCK_HZ 100u
#define EOF (-1)

#define VIBE_APP_MAX_OPEN_FILES 16
#define VIBE_FILE_FLAG_READ 0x01
#define VIBE_FILE_FLAG_WRITE 0x02
#define VIBE_FILE_FLAG_APPEND 0x04
#define VIBE_FILE_FLAG_CONSOLE 0x08
/* fd-backed stream (fdopen): I/O goes straight through the host fd table
 * (read_fd/write_fd/close_fd/seek_fd) instead of the in-memory buffer
 * that fopen() loads. */
#define VIBE_FILE_FLAG_FD 0x10

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
    char path[256];
} FILE;

extern int doom_vprintf(const char *fmt, va_list ap);
extern int doom_vfprintf(FILE *stream, const char *fmt, va_list ap);
extern int doom_vsprintf(char *str, const char *fmt, va_list ap);
extern int doom_vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

static const struct vibe_app_context *g_desktop_app_ctx = 0;
struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
};

static struct heap_block *g_heap_head = 0;
static volatile int g_heap_lock = 0;
static void (*g_atexit_handlers[16])(void);
static int g_atexit_count = 0;
static FILE g_file_pool[VIBE_APP_MAX_OPEN_FILES];

static FILE g_stdin = {0, VIBE_FILE_FLAG_READ | VIBE_FILE_FLAG_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 1, {0}};
static FILE g_stdout = {1, VIBE_FILE_FLAG_WRITE | VIBE_FILE_FLAG_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 1, {0}};
static FILE g_stderr = {2, VIBE_FILE_FLAG_WRITE | VIBE_FILE_FLAG_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 1, {0}};

FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

static int vibe_app_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static void heap_init(void *base, size_t size) {
    g_heap_head = 0;
    if (!base || size <= sizeof(struct heap_block)) {
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

static FILE *vibe_alloc_file(void) {
    int i;

    for (i = 0; i < VIBE_APP_MAX_OPEN_FILES; ++i) {
        if (!g_file_pool[i].in_use) {
            memset(&g_file_pool[i], 0, sizeof(g_file_pool[i]));
            g_file_pool[i].in_use = 1;
            return &g_file_pool[i];
        }
    }
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
    if (f->flags & VIBE_FILE_FLAG_FD) {
        return 0; /* fd-backed streams are unbuffered; the host holds the bytes */
    }
    if ((f->flags & VIBE_FILE_FLAG_WRITE) == 0 || !f->dirty) {
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

const struct vibe_app_context *vibe_app_get_context(void) {
    return g_desktop_app_ctx;
}

void vibe_app_runtime_init(const struct vibe_app_context *ctx) {
    g_desktop_app_ctx = ctx;
    g_atexit_count = 0;
    memset(g_file_pool, 0, sizeof(g_file_pool));
    g_heap_lock = 0;
    if (ctx) {
        heap_init(ctx->heap_base, ctx->heap_size);
    } else {
        g_heap_head = 0;
    }
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

int vibe_app_write_file(const char *path, const void *data, int size) {
    if (!path || (!data && size > 0) || size < 0) {
        return -1;
    }
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->write_file) {
        return -1;
    }
    return g_desktop_app_ctx->host->write_file(path, data, size);
}

int vibe_app_create_dir(const char *path) {
    if (!path) {
        return -1;
    }
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->create_dir) {
        return -1;
    }
    return g_desktop_app_ctx->host->create_dir(path);
}

int vibe_app_open(const char *path, int flags) {
    if (!path) {
        return -1;
    }
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->open_file) {
        return -1;
    }
    return g_desktop_app_ctx->host->open_file(path, flags);
}

int vibe_app_read(int fd, void *buf, int size) {
    if (!buf || size < 0) {
        return -1;
    }
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->read_fd) {
        return -1;
    }
    return g_desktop_app_ctx->host->read_fd(fd, buf, size);
}

int vibe_app_write(int fd, const void *buf, int size) {
    if ((!buf && size > 0) || size < 0) {
        return -1;
    }
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->write_fd) {
        return -1;
    }
    return g_desktop_app_ctx->host->write_fd(fd, buf, size);
}

int vibe_app_close(int fd) {
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->close_fd) {
        return -1;
    }
    return g_desktop_app_ctx->host->close_fd(fd);
}

int vibe_app_lseek(int fd, int offset, int whence) {
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->seek_fd) {
        return -1;
    }
    return g_desktop_app_ctx->host->seek_fd(fd, offset, whence);
}

int vibe_app_sync(void) {
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->sync_filesystem) {
        return -1;
    }
    return g_desktop_app_ctx->host->sync_filesystem();
}

int vibe_app_stat(const char *path, struct vibe_app_stat *stat_out) {
    if (!path || !stat_out) {
        return -1;
    }
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->stat_path) {
        return -1;
    }
    return g_desktop_app_ctx->host->stat_path(path, stat_out);
}

int vibe_app_fstat(int fd, struct vibe_app_stat *stat_out) {
    if (!stat_out) {
        return -1;
    }
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->fstat_fd) {
        return -1;
    }
    return g_desktop_app_ctx->host->fstat_fd(fd, stat_out);
}

const char *vibe_app_getenv(const char *name) {
    if (!name) {
        return 0;
    }
    if (!g_desktop_app_ctx || !g_desktop_app_ctx->host || !g_desktop_app_ctx->host->getenv_value) {
        return 0;
    }
    return g_desktop_app_ctx->host->getenv_value(name);
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

void *vibe_app_malloc(size_t size) {
    struct heap_block *block;
    void *result = 0;

    if (size == 0u) {
        return 0;
    }
    if (size & 7u) {
        size += 8u - (size & 7u);
    }

    heap_lock();
    for (block = g_heap_head; block; block = block->next) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = 0;
            result = block_payload(block);
            break;
        }
    }
    heap_unlock();
    return result;
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
    struct heap_block *block;
    void *new_ptr;

    if (!ptr) {
        return vibe_app_malloc(size);
    }
    if (size == 0u) {
        vibe_app_free(ptr);
        return 0;
    }

    heap_lock();
    block = payload_block(ptr);
    if (block->size >= size) {
        heap_unlock();
        return ptr;
    }
    heap_unlock();

    new_ptr = vibe_app_malloc(size);
    if (!new_ptr) {
        return 0;
    }
    memcpy(new_ptr, ptr, block->size);
    vibe_app_free(ptr);
    return new_ptr;
}

int printf(const char *fmt, ...) {
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = doom_vprintf(fmt, ap);
    va_end(ap);
    return rc;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = doom_vfprintf(stream, fmt, ap);
    va_end(ap);
    return rc;
}

int vprintf(const char *fmt, va_list ap) {
    return doom_vprintf(fmt, ap);
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    return doom_vfprintf(stream, fmt, ap);
}

int sprintf(char *str, const char *fmt, ...) {
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = doom_vsprintf(str, fmt, ap);
    va_end(ap);
    return rc;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = doom_vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return rc;
}

int vsprintf(char *str, const char *fmt, va_list ap) {
    return doom_vsprintf(str, fmt, ap);
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    return doom_vsnprintf(str, size, fmt, ap);
}

FILE *fopen(const char *filename, const char *mode) {
    FILE *f;
    int flags = 0;
    const char *src = 0;
    int size = 0;

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
        if (vibe_app_read_file(filename, &src, &size) == 0 && src && size >= 0) {
            if (size > 0 && vibe_file_reserve(f, size) != 0) {
                f->in_use = 0;
                return 0;
            }
            if (size > 0) {
                memcpy(f->buf, src, (size_t)size);
            }
            f->size = size;
            if (flags & VIBE_FILE_FLAG_APPEND) {
                f->pos = size;
            }
        } else if (mode[0] == 'r') {
            f->in_use = 0;
            return 0;
        }
    }
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    int flags;
    FILE *f;

    if (!mode || vibe_parse_mode(mode, &flags) != 0) {
        return 0;
    }
    /* fds 0/1/2 belong to the host console (its own key/putc loop); return
     * the canonical streams rather than a second handle on the same fd. */
    if (fd == 0) {
        return stdin;
    }
    if (fd == 1) {
        return stdout;
    }
    if (fd == 2) {
        return stderr;
    }
    if (fd < 3) {
        return 0;
    }
    f = vibe_alloc_file();
    if (!f) {
        return 0;
    }
    f->fd = fd;
    f->flags = flags | VIBE_FILE_FLAG_FD;
    f->pos = 0;
    return f;
}

int fclose(FILE *stream) {
    if (!stream) {
        return -1;
    }
    if (vibe_is_console_file(stream)) {
        return 0;
    }
    if (stream->flags & VIBE_FILE_FLAG_FD) {
        int rc = vibe_app_close(stream->fd);

        free(stream->buf);
        stream->buf = 0;
        stream->in_use = 0;
        return rc == 0 ? 0 : -1;
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
    return 0;
}

int fflush(FILE *stream) {
    if (!stream) {
        return -1;
    }
    return vibe_file_flush(stream);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    size_t available;

    if (!ptr || !stream || size == 0u || nmemb == 0u || vibe_is_console_file(stream)) {
        return 0;
    }
    total = size * nmemb;
    if (stream->flags & VIBE_FILE_FLAG_FD) {
        int rc;
        size_t done = 0u;

        if ((stream->flags & VIBE_FILE_FLAG_READ) == 0) {
            stream->error = 1;
            return 0;
        }
        while (done < total) {
            rc = vibe_app_read(stream->fd, (unsigned char *)ptr + done,
                               (int)(total - done));
            if (rc > 0) {
                done += (size_t)rc;
                stream->pos += rc;
                continue;
            }
            if (rc < 0) {
                stream->error = 1;
            } else {
                stream->eof = 1;
            }
            break;
        }
        return done / size;
    }
    if (stream->pos >= stream->size) {
        stream->eof = 1;
        return 0;
    }
    available = (size_t)(stream->size - stream->pos);
    if (total > available) {
        total = available;
        stream->eof = 1;
    }
    memcpy(ptr, stream->buf + stream->pos, total);
    stream->pos += (long)total;
    return total / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;

    if (!ptr || !stream || size == 0u || nmemb == 0u) {
        return 0;
    }
    if (stream == stdout || stream == stderr) {
        const unsigned char *bytes = (const unsigned char *)ptr;
        size_t i;

        total = size * nmemb;
        for (i = 0; i < total; ++i) {
            vibe_app_console_putc((char)bytes[i]);
        }
        return nmemb;
    }
    if ((stream->flags & VIBE_FILE_FLAG_WRITE) == 0) {
        stream->error = 1;
        return 0;
    }
    total = size * nmemb;
    if (stream->flags & VIBE_FILE_FLAG_FD) {
        int rc;

        if ((stream->flags & VIBE_FILE_FLAG_WRITE) == 0) {
            stream->error = 1;
            return 0;
        }
        rc = vibe_app_write(stream->fd, ptr, (int)total);
        if (rc < 0) {
            stream->error = 1;
            return 0;
        }
        stream->pos += rc;
        return (size_t)rc / size;
    }
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

    if (stream == stdin) {
        return vibe_app_poll_key();
    }
    if (fread(&c, 1u, 1u, stream) != 1u) {
        return EOF;
    }
    return (int)c;
}

int fputc(int c, FILE *stream) {
    if (stream == stdout || stream == stderr) {
        vibe_app_console_putc((char)c);
        return c;
    }
    if (stream && (stream->flags & VIBE_FILE_FLAG_FD)) {
        unsigned char ch = (unsigned char)c;

        return fwrite(&ch, 1u, 1u, stream) == 1u ? c : EOF;
    }
    return EOF;
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
    if (!s || !stream) {
        return EOF;
    }
    if (stream == stdout || stream == stderr) {
        while (*s) {
            vibe_app_console_putc(*s++);
        }
        return 0;
    }
    if (stream->flags & VIBE_FILE_FLAG_FD) {
        int len = 0;

        while (s[len] != '\0') {
            ++len;
        }
        return fwrite(s, 1u, (size_t)len, stream) == (size_t)len ? 0 : EOF;
    }
    return EOF;
}

int puts(const char *s) {
    if (!s) {
        return EOF;
    }
    while (*s) {
        vibe_app_console_putc(*s++);
    }
    vibe_app_console_putc('\n');
    return 0;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->error = 0;
    }
}

int feof(FILE *stream) {
    return stream ? stream->eof : 0;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 0;
}

int fseek(FILE *stream, long offset, int whence) {
    long base;
    long next;

    if (!stream || vibe_is_console_file(stream)) {
        return -1;
    }
    if (stream->flags & VIBE_FILE_FLAG_FD) {
        int rc = vibe_app_lseek(stream->fd, (int)offset, whence);

        if (rc < 0) {
            stream->error = 1;
            return -1;
        }
        stream->pos = rc;
        stream->eof = 0;
        return 0;
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
    return 0;
}

long ftell(FILE *stream) {
    return stream ? stream->pos : -1;
}

void rewind(FILE *stream) {
    if (stream) {
        (void)fseek(stream, 0, 0);
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

void setbuf(FILE *stream, char *buf) {
    (void)stream;
    (void)buf;
}

/*
 * Stack-protector support for the desktop launcher link group (startx et
 * al. link THIS runtime instead of lang/sdk/app_runtime.c, and
 * userland/modules/ssp.c only rides USERLAND_SRCS). Shared CFLAGS enable
 * -fstack-protector for every object, so this group needs its own guard
 * provider: name the event through the module syscalls' debug channel, then
 * park. P4 classe #5 probe.
 */
extern void sys_write_debug(const char *msg);
extern void sys_yield(void);

unsigned long __stack_chk_guard = 0x56494245UL; /* "VIBE" */

__attribute__((noreturn)) void __stack_chk_fail(void) {
    sys_write_debug("desktop runtime: stack check fail (local buffer overflow)\n");
    for (;;) {
        sys_yield();
    }
}
