#include <userland/lua/include/lua_port.h>
#include <userland/applications/games/doom_port/doom_port.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#pragma push_macro("mkdir")
#undef mkdir
#include <userland/modules/include/syscalls.h>
#pragma pop_macro("mkdir")
#include <sys/types.h>
#pragma push_macro("mkdir")
#undef mkdir
#include <sys/stat.h>
#pragma pop_macro("mkdir")

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define DOOM_MAX_FDS 32
#define DOOM_O_ACCMODE 0x0003
#define DOOM_O_RDONLY 0x0000
#define DOOM_STDIN_FD 0
#define DOOM_STDOUT_FD 1
#define DOOM_STDERR_FD 2
#define DOOM_IWAD_PATH "/DOOM/DOOM.WAD"
#define DOOM_IWAD_ALIAS_COUNT 6
#define DOOM_EMBEDDED_FILE_NONE 0
#define DOOM_EMBEDDED_FILE_WAD 1

static FILE g_stdout_stream = { -2, 0, 0, 0, 0 };
static FILE g_stderr_stream = { -3, 0, 0, 0, 0 };

FILE *stdout = &g_stdout_stream;
FILE *stderr = &g_stderr_stream;

char *sndserver_filename = "sndserver";

struct doom_fd_entry {
    int used;
    int embedded_kind;
    int node;
    const char *data;
    int size;
    int pos;
};

static struct doom_fd_entry g_doom_fds[DOOM_MAX_FDS];
static const char *g_doom_iwad_aliases[DOOM_IWAD_ALIAS_COUNT] = {
    "doom.wad",
    "doom1.wad",
    "doomu.wad",
    "doom2.wad",
    "tnt.wad",
    "plutonia.wad"
};

static int doom_alloc_fd(void) {
    int i;
    for (i = 3; i < DOOM_MAX_FDS; ++i) {
        if (!g_doom_fds[i].used) {
            return i;
        }
    }
    return -1;
}

static const char *doom_path_basename(const char *path) {
    const char *name = path;
    if (!path) {
        return "";
    }
    while (*path) {
        if (*path == '/' || *path == '\\') {
            name = path + 1;
        }
        ++path;
    }
    return name;
}

static int doom_path_is_embedded_wad(const char *path) {
    const char *name = doom_path_basename(path);
    int i;

    for (i = 0; i < DOOM_IWAD_ALIAS_COUNT; ++i) {
        if (strcasecmp(name, g_doom_iwad_aliases[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int doom_try_resolve_path(const char *path) {
    if (!path) {
        return -1;
    }
    return fs_resolve(path);
}

static int doom_try_resolve_wad_candidate(const char *path, const char **resolved_path_out) {
    int node = doom_try_resolve_path(path);
    if (node >= 0) {
        if (resolved_path_out) {
            *resolved_path_out = path;
        }
        return node;
    }
    if (doom_path_is_embedded_wad(path)) {
        node = doom_try_resolve_path(DOOM_IWAD_PATH);
        if (node >= 0) {
            if (resolved_path_out) {
                *resolved_path_out = DOOM_IWAD_PATH;
            }
            return node;
        }
        {
            const char *name = doom_path_basename(path);
            int i;
            for (i = 0; i < DOOM_IWAD_ALIAS_COUNT; ++i) {
                char alt[128];
                int pos = 0;
                const char *src = g_doom_iwad_aliases[i];
                alt[pos++] = '/';
                while (*src && pos < (int)sizeof(alt) - 1) {
                    alt[pos++] = *src++;
                }
                alt[pos] = '\0';
                node = doom_try_resolve_path(alt);
                if (node >= 0) {
                    if (resolved_path_out) {
                        *resolved_path_out = DOOM_IWAD_PATH;
                    }
                    return node;
                }
            }
            (void)name;
        }
    }
    return -1;
}

static void doom_debug_path(const char *prefix, const char *path) {
    char msg[128];
    int pos = 0;

    while (prefix && *prefix && pos < (int)sizeof(msg) - 1) {
        msg[pos++] = *prefix++;
    }
    while (path && *path && pos < (int)sizeof(msg) - 2) {
        msg[pos++] = *path++;
    }
    msg[pos++] = '\n';
    msg[pos] = '\0';
    sys_write_debug(msg);
}

static void doom_debug_fd_event(const char *op, int fd, int a, int b, int c) {
    char msg[160];

    snprintf(msg,
             sizeof(msg),
             "doom: %s fd=%d a=%d b=%d c=%d\n",
             op ? op : "?",
             fd,
             a,
             b,
             c);
    sys_write_debug(msg);
    doom_port_debug_note(msg);
}

int doom_port_iwad_available(void) {
    int node = doom_try_resolve_path(DOOM_IWAD_PATH);
    return node >= 0 && !g_fs_nodes[node].is_dir;
}

int atoi(const char *s) {
    int sign = 1;
    int value = 0;

    if (!s) {
        return 0;
    }
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        ++s;
    }
    if (*s == '-') {
        sign = -1;
        ++s;
    } else if (*s == '+') {
        ++s;
    }
    while (*s >= '0' && *s <= '9') {
        value = (value * 10) + (*s - '0');
        ++s;
    }
    return value * sign;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) {
        ++d;
    }
    while ((*d++ = *src++) != '\0') {
    }
    return dst;
}

int strcasecmp(const char *a, const char *b) {
    unsigned char ca;
    unsigned char cb;

    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    while (1) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb + ('a' - 'A'));
        }
        if (ca != cb || ca == 0u || cb == 0u) {
            return (int)ca - (int)cb;
        }
    }
}

int strncasecmp(const char *a, const char *b, size_t n) {
    unsigned char ca;
    unsigned char cb;

    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    while (n-- > 0u) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb + ('a' - 'A'));
        }
        if (ca != cb || ca == 0u || cb == 0u) {
            return (int)ca - (int)cb;
        }
    }
    return 0;
}

static int parse_int_token(const char **src, int base, int *out_value) {
    int sign = 1;
    int value = 0;
    int parsed = 0;

    if (!src || !*src || !out_value) {
        return 0;
    }
    while (**src == ' ' || **src == '\t' || **src == '\n' || **src == '\r') {
        ++(*src);
    }
    if (**src == '-' || **src == '+') {
        if (**src == '-') {
            sign = -1;
        }
        ++(*src);
    }

    if (base == 16 && (**src == '0') && ((*src)[1] == 'x' || (*src)[1] == 'X')) {
        *src += 2;
    }
    if (base == 0 && (**src == '0') && ((*src)[1] == 'x' || (*src)[1] == 'X')) {
        base = 16;
        *src += 2;
    } else if (base == 0) {
        base = 10;
    }

    while (**src != '\0') {
        int digit = -1;
        char c = **src;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            digit = 10 + (c - 'A');
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        value = (value * base) + digit;
        parsed = 1;
        ++(*src);
    }

    if (!parsed) {
        return 0;
    }
    *out_value = value * sign;
    return 1;
}

static int vsnprintf_internal(char *str, size_t size, const char *fmt, va_list ap) {
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

            if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'x' || *fmt == 'X' || *fmt == 'p') {
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
                        unsigned int d = value_u % base;
                        tmp[pos++] = (char)(d < 10u ? ('0' + d) : ((upper ? 'A' : 'a') + (d - 10u)));
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

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    return vsnprintf_internal(str, size, fmt, ap);
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
    return vsnprintf(str, (size_t)-1, fmt, ap);
}

int sprintf(char *str, const char *fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsprintf(str, fmt, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *fmt, va_list ap) {
    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    if (len > 0) {
        console_write(buffer);
    }
    return len;
}

int printf(const char *fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    (void)stream;
    return vprintf(fmt, ap);
}

int fprintf(FILE *stream, const char *fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vfprintf(stream, fmt, ap);
    va_end(ap);
    return ret;
}

int putchar(int c) {
    console_putc((char)c);
    return c;
}

int getchar(void) {
    return sys_poll_key();
}

int puts(const char *s) {
    if (s) {
        console_write(s);
    }
    console_putc('\n');
    return 0;
}

int fputc(int c, FILE *stream) {
    (void)stream;
    return putchar(c);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    const char *p = (const char *)ptr;
    size_t total = size * nmemb;
    (void)stream;

    for (size_t i = 0; i < total; ++i) {
        console_putc(p[i]);
    }
    return nmemb;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    int base;
    int next;
    int size;

    if (!stream || stream->node < 0 || stream->node >= FS_MAX_NODES) {
        return -1;
    }
    size = g_fs_nodes[stream->node].size;
    if (whence == SEEK_SET) {
        base = 0;
    } else if (whence == SEEK_CUR) {
        base = stream->pos;
    } else if (whence == SEEK_END) {
        base = size;
    } else {
        return -1;
    }

    next = base + (int)offset;
    if (next < 0 || next > size) {
        return -1;
    }
    stream->pos = next;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) {
        return -1;
    }
    return (long)stream->pos;
}

void setbuf(FILE *stream, char *buf) {
    (void)stream;
    (void)buf;
}

int sscanf(const char *str, const char *fmt, ...) {
    int assigned = 0;
    va_list ap;
    const char *s = str;
    const char *f = fmt;

    if (!str || !fmt) {
        return 0;
    }

    va_start(ap, fmt);
    while (*f != '\0') {
        if (*f == ' ' || *f == '\t' || *f == '\n' || *f == '\r') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
                ++s;
            }
            ++f;
            continue;
        }
        if (*f != '%') {
            if (*s != *f) {
                break;
            }
            ++s;
            ++f;
            continue;
        }

        ++f;
        if (*f == 'd' || *f == 'i') {
            int *out = va_arg(ap, int *);
            if (!parse_int_token(&s, 0, out)) {
                break;
            }
            ++assigned;
            ++f;
            continue;
        }
        if (*f == 'x' || *f == 'X') {
            int *out = va_arg(ap, int *);
            if (!parse_int_token(&s, 16, out)) {
                break;
            }
            ++assigned;
            ++f;
            continue;
        }
        if (*f == 'u') {
            int *out = va_arg(ap, int *);
            if (!parse_int_token(&s, 10, out)) {
                break;
            }
            ++assigned;
            ++f;
            continue;
        }
        if (*f == 'c') {
            char *out = va_arg(ap, char *);
            if (*s == '\0') {
                break;
            }
            *out = *s++;
            ++assigned;
            ++f;
            continue;
        }
        break;
    }
    va_end(ap);
    return assigned;
}

int fscanf(FILE *stream, const char *fmt, ...) {
    (void)stream;
    (void)fmt;
    return EOF;
}

int access(const char *path, int mode) {
    int node;
    (void)mode;
    node = doom_try_resolve_wad_candidate(path, 0);
    if (node >= 0 && !g_fs_nodes[node].is_dir) {
        return 0;
    }
    return -1;
}

void vibe_app_console_putc(char c) {
    console_putc(c);
}

void vibe_app_console_write(const char *text) {
    if (text) {
        console_write(text);
    }
}

int vibe_app_poll_key(void) {
    return sys_poll_key();
}

void vibe_app_yield(void) {
    sys_yield();
}

int vibe_app_read_file(const char *path, const char **data_out, int *size_out) {
    int node;

    if (!path || !data_out || !size_out) {
        return -1;
    }
    node = fs_resolve(path);
    if (node < 0 || g_fs_nodes[node].is_dir) {
        return -1;
    }

    *data_out = g_fs_nodes[node].data;
    *size_out = g_fs_nodes[node].size;
    return 0;
}

int vibe_app_getcwd(char *buf, int max_len) {
    if (!buf || max_len <= 0) {
        return -1;
    }
    fs_build_path(g_fs_cwd, buf, max_len);
    return 0;
}

int vibe_app_remove_dir(const char *path) {
    int node;
    int rc;

    if (!path || path[0] == '\0') {
        return -1;
    }
    node = fs_resolve(path);
    if (node < 0) {
        return -1;
    }
    if (!g_fs_nodes[node].is_dir) {
        return -3;
    }

    rc = fs_remove(path);
    if (rc == -2) {
        return -2;
    }
    return (rc == 0) ? 0 : -1;
}

int mkdir(const char *path, mode_t mode) {
    int rc;
    (void)mode;
    if (path[0] == '\0') {
        return -1;
    }
    rc = fs_create(path, 1);
    return (rc >= 0) ? 0 : -1;
}

int open(const char *path, int flags, ...) {
    int fd;
    int node;
    const char *resolved_path = path;

    if ((flags & DOOM_O_ACCMODE) != DOOM_O_RDONLY) {
        return -1;
    }
    node = doom_try_resolve_wad_candidate(path, &resolved_path);
    if (node >= 0 && !g_fs_nodes[node].is_dir) {
        fd = doom_alloc_fd();
        if (fd < 0) {
            return -1;
        }
        g_doom_fds[fd].used = 1;
        g_doom_fds[fd].embedded_kind = DOOM_EMBEDDED_FILE_NONE;
        g_doom_fds[fd].node = node;
        g_doom_fds[fd].data = g_fs_nodes[node].data;
        g_doom_fds[fd].size = g_fs_nodes[node].size;
        g_doom_fds[fd].pos = 0;
        if (g_fs_nodes[node].storage_kind == FS_NODE_STORAGE_IMAGE) {
            g_doom_fds[fd].embedded_kind = DOOM_EMBEDDED_FILE_WAD;
            g_doom_fds[fd].data = 0;
        }
        doom_debug_path("doom: wad open ", resolved_path);
        doom_debug_fd_event("open", fd, g_doom_fds[fd].size, node, g_fs_nodes[node].storage_kind);
        return fd;
    }
    return -1;
}

int close(int fd) {
    if (fd >= DOOM_STDIN_FD && fd <= DOOM_STDERR_FD) {
        return 0;
    }
    if (fd < 0 || fd >= DOOM_MAX_FDS || !g_doom_fds[fd].used) {
        return -1;
    }
    g_doom_fds[fd].used = 0;
    g_doom_fds[fd].embedded_kind = DOOM_EMBEDDED_FILE_NONE;
    g_doom_fds[fd].node = 0;
    g_doom_fds[fd].data = 0;
    g_doom_fds[fd].size = 0;
    g_doom_fds[fd].pos = 0;
    return 0;
}

ssize_t read(int fd, void *buf, size_t count) {
    struct doom_fd_entry *entry;
    size_t i;
    size_t available;
    int bytes_read;

    if (fd == DOOM_STDIN_FD) {
        if (count == 0u) {
            return 0;
        }
        ((char *)buf)[0] = (char)sys_poll_key();
        return 1;
    }
    if (fd < 0 || fd >= DOOM_MAX_FDS || !g_doom_fds[fd].used) {
        return -1;
    }

    entry = &g_doom_fds[fd];
    if (entry->embedded_kind == DOOM_EMBEDDED_FILE_WAD && count >= 1024u) {
        doom_debug_fd_event("read-begin", fd, entry->pos, (int)count, entry->size);
    }
    if (entry->pos >= entry->size) {
        return 0;
    }
    if (entry->embedded_kind == DOOM_EMBEDDED_FILE_WAD) {
        available = (size_t)(entry->size - entry->pos);
        if (count > available) {
            count = available;
        }
        bytes_read = fs_read_node_bytes(entry->node, entry->pos, buf, (int)count);
        if (bytes_read < 0) {
            doom_debug_fd_event("read-fail", fd, entry->pos, (int)count, entry->size);
            return -1;
        }
        entry->pos += bytes_read;
        if (bytes_read != (int)count || count >= 1024u) {
            doom_debug_fd_event("read-end", fd, entry->pos, bytes_read, (int)count);
        }
        return (ssize_t)bytes_read;
    }
    available = (size_t)(entry->size - entry->pos);
    if (count > available) {
        count = available;
    }
    for (i = 0; i < count; ++i) {
        ((char *)buf)[i] = entry->data[entry->pos + (int)i];
    }
    entry->pos += (int)count;
    return (ssize_t)count;
}

ssize_t write(int fd, const void *buf, size_t count) {
    size_t i;
    const char *text = (const char *)buf;

    if (fd != DOOM_STDOUT_FD && fd != DOOM_STDERR_FD) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        console_putc(text[i]);
    }
    return (ssize_t)count;
}

off_t lseek(int fd, off_t offset, int whence) {
    int base;
    int next;
    struct doom_fd_entry *entry;

    if (fd < 0 || fd >= DOOM_MAX_FDS || !g_doom_fds[fd].used) {
        return -1;
    }
    entry = &g_doom_fds[fd];
    if (whence == SEEK_SET) {
        base = 0;
    } else if (whence == SEEK_CUR) {
        base = entry->pos;
    } else if (whence == SEEK_END) {
        base = entry->size;
    } else {
        return -1;
    }
    next = base + (int)offset;
    if (next < 0 || next > entry->size) {
        doom_debug_fd_event("lseek-bad", fd, base, (int)offset, entry->size);
        return -1;
    }
    entry->pos = next;
    if (entry->embedded_kind == DOOM_EMBEDDED_FILE_WAD) {
        doom_debug_fd_event("lseek", fd, base, (int)offset, next);
    }
    return (off_t)next;
}

int fstat(int fd, struct stat *buf) {
    memset(buf, 0, sizeof(*buf));
    if (fd >= DOOM_STDIN_FD && fd <= DOOM_STDERR_FD) {
        return 0;
    }
    if (fd < 0 || fd >= DOOM_MAX_FDS || !g_doom_fds[fd].used) {
        return -1;
    }
    buf->st_size = (off_t)g_doom_fds[fd].size;
    return 0;
}

__attribute__((noreturn)) void exit(int status) {
    doom_port_abort_run(status == 0 ? "DOOM finalizado" : "DOOM abortado", status);
}

void __stack_chk_fail(void) {
    console_write("doom: stack check fail\n");
    for (;;) {
        sys_yield();
    }
}
