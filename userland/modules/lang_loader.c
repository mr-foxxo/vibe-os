#include <lang/include/vibe_app.h>
#include <lang/include/vibe_appfs.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include "app_catalog.h"

#include <stdint.h>

static struct vibe_app_context g_app_ctx;
static struct vibe_appfs_directory g_cached_directory;
static int g_cached_directory_valid = 0;
static unsigned char *g_host_read_file_buf = 0;
static int g_host_read_file_buf_capacity = 0;
static volatile uint32_t g_lang_arena_owner[3];
static void lang_debug_vga(int row, const char *text);
static int lang_has_runtime_stub(const char *name);
static int lang_catalog_command_exists(const char *name);
static const char *lang_command_alias_target(const char *name);
static int lang_load_directory(struct vibe_appfs_directory *directory);
static const struct vibe_appfs_entry *lang_find_entry(const struct vibe_appfs_directory *directory,
                                                      const char *name);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
static int host_getcwd(char *buf, int max_len);

#define LANG_HOST_MAX_FDS 16
#define LANG_HOST_O_RDONLY 0
#define LANG_HOST_O_WRONLY 1
#define LANG_HOST_O_RDWR 2
#define LANG_HOST_O_ACCMODE 0x0003
#define LANG_HOST_O_CREAT 0x0040
#define LANG_HOST_O_TRUNC 0x0200
#define LANG_HOST_O_APPEND 0x0400
#define LANG_HOST_SEEK_SET 0
#define LANG_HOST_SEEK_CUR 1
#define LANG_HOST_SEEK_END 2
#define LANG_STORAGE_RETRY_COUNT 4
#define LANG_DIRECTORY_RETRY_COUNT 16
#define LANG_SECTOR_SIZE 512u
#define LANG_STORAGE_VERIFY_SECTORS 1u
#define LANG_STORAGE_BULK_SECTORS 128u

struct lang_host_fd {
    int used;
    int flags;
    int pos;
    int size;
    int capacity;
    int dirty;
    unsigned char *buf;
    char path[256];
};

static struct lang_host_fd g_lang_host_fds[LANG_HOST_MAX_FDS];

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static uintptr_t align_down_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return value & ~(align - 1u);
}

static uint32_t lang_app_stack_size(uint32_t load_address) {
    switch (load_address) {
    case VIBE_APP_DESKTOP_LOAD_ADDR:
    case VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325:
        return VIBE_APP_DESKTOP_STACK_SIZE;
    case VIBE_APP_BOOT_LOAD_ADDR:
    case VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325:
        return VIBE_APP_BOOT_STACK_SIZE;
    default:
        return VIBE_APP_STACK_SIZE;
    }
}

static uint32_t lang_app_arena_size(uint32_t load_address) {
    switch (load_address) {
    case VIBE_APP_DESKTOP_LOAD_ADDR:
    case VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325:
        return VIBE_APP_DESKTOP_ARENA_SIZE;
    case VIBE_APP_BOOT_LOAD_ADDR:
    case VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325:
        return VIBE_APP_BOOT_ARENA_SIZE;
    default:
        return VIBE_APP_ARENA_SIZE;
    }
}

static int lang_arena_slot_for_load_address(uint32_t load_address) {
    switch (load_address) {
    case VIBE_APP_LOAD_ADDR:
    case VIBE_APP_COMPAT_LOAD_ADDR_20260325:
        return 0;
    case VIBE_APP_DESKTOP_LOAD_ADDR:
    case VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325:
        return 1;
    case VIBE_APP_BOOT_LOAD_ADDR:
    case VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325:
        return 2;
    default:
        return -1;
    }
}

static int lang_arena_acquire(uint32_t load_address) {
    uint32_t pid;
    int slot;
    int emitted_wait_trace = 0;

    slot = lang_arena_slot_for_load_address(load_address);
    pid = (uint32_t)sys_getpid();
    if (slot < 0 || pid == 0u) {
        return -1;
    }

    for (;;) {
        if (g_lang_arena_owner[slot] == pid) {
            return 0;
        }
        if (g_lang_arena_owner[slot] == 0u &&
            __sync_bool_compare_and_swap(&g_lang_arena_owner[slot], 0u, pid)) {
            return 0;
        }
        if (!emitted_wait_trace) {
            sys_write_debug("lang: waiting for app arena\n");
            emitted_wait_trace = 1;
        }
        sys_yield();
    }
}

static void lang_arena_release(uint32_t load_address) {
    uint32_t pid;
    int slot;

    slot = lang_arena_slot_for_load_address(load_address);
    pid = (uint32_t)sys_getpid();
    if (slot < 0 || pid == 0u) {
        return;
    }
    (void)__sync_bool_compare_and_swap(&g_lang_arena_owner[slot], pid, 0u);
}

static void *lang_memcpy(void *dst, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    for (uint32_t i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return dst;
}

static int lang_memcmp(const void *a, const void *b, uint32_t size) {
    const uint8_t *lhs = (const uint8_t *)a;
    const uint8_t *rhs = (const uint8_t *)b;

    for (uint32_t i = 0; i < size; ++i) {
        if (lhs[i] != rhs[i]) {
            return (int)lhs[i] - (int)rhs[i];
        }
    }
    return 0;
}

/*
 * 0.6 (30/10) witness: "header invalid after read" failed at the magic
 * check with no detail. One line per 16 bytes so the serial overrun rule
 * (~80 chars) holds: tag + 32 hex chars + 3 spaces.
 */
static void lang_hex_dump_dbg(const char *tag, const uint8_t *p) {
    static const char digits[] = "0123456789abcdef";
    char line[48];
    int pos = 0;
    int i;

    for (i = 0; i < 8 && tag[i] != '\0'; ++i) {
        line[pos++] = tag[i];
    }
    line[pos++] = ' ';
    for (i = 0; i < 16 && pos < 44; ++i) {
        line[pos++] = digits[p[i] >> 4];
        line[pos++] = digits[p[i] & 0x0f];
        if ((i & 1) == 1) {
            line[pos++] = ' ';
        }
    }
    line[pos] = '\0';
    sys_write_debug(line);
    sys_write_debug("\n");
}

static void lang_memset(void *dst, int value, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;

    for (uint32_t i = 0; i < size; ++i) {
        out[i] = (uint8_t)value;
    }
}

static void lang_reset_host_fd(struct lang_host_fd *fd) {
    if (!fd) {
        return;
    }
    if (fd->buf) {
        free(fd->buf);
    }
    lang_memset(fd, 0, (uint32_t)sizeof(*fd));
}

static void lang_reset_host_fds(void) {
    int i;

    for (i = 0; i < LANG_HOST_MAX_FDS; ++i) {
        lang_reset_host_fd(&g_lang_host_fds[i]);
    }
}

static int lang_has_slash(const char *text) {
    if (text == 0) {
        return 0;
    }

    while (*text != '\0') {
        if (*text == '/') {
            return 1;
        }
        ++text;
    }

    return 0;
}

static const char *lang_path_basename(const char *path) {
    const char *basename = path;

    if (path == 0) {
        return 0;
    }

    while (*path != '\0') {
        if (*path == '/') {
            basename = path + 1;
        }
        ++path;
    }

    return basename;
}

static int lang_host_reserve(struct lang_host_fd *fd, int needed) {
    int new_capacity;
    unsigned char *new_buf;

    if (!fd || needed < 0) {
        return -1;
    }
    if (needed <= fd->capacity) {
        return 0;
    }

    new_capacity = fd->capacity > 0 ? fd->capacity : 256;
    while (new_capacity < needed) {
        if (new_capacity > (1 << 29)) {
            return -1;
        }
        new_capacity *= 2;
    }

    new_buf = (unsigned char *)realloc(fd->buf, (size_t)new_capacity);
    if (!new_buf) {
        return -1;
    }
    fd->buf = new_buf;
    fd->capacity = new_capacity;
    return 0;
}

static int lang_host_load_file(struct lang_host_fd *fd, int node) {
    if (!fd || node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return -1;
    }
    if (g_fs_nodes[node].size <= 0) {
        fd->size = 0;
        return 0;
    }
    if (lang_host_reserve(fd, g_fs_nodes[node].size) != 0) {
        return -1;
    }
    if (fs_read_node_bytes(node, 0, fd->buf, g_fs_nodes[node].size) != g_fs_nodes[node].size) {
        return -1;
    }
    fd->size = g_fs_nodes[node].size;
    return 0;
}

static int lang_host_flush_fd(struct lang_host_fd *fd) {
    if (!fd || !fd->used) {
        return -1;
    }
    if ((fd->flags & LANG_HOST_O_ACCMODE) == LANG_HOST_O_RDONLY || !fd->dirty) {
        return 0;
    }
    if (fs_write_bytes(fd->path, fd->buf, fd->size) != 0) {
        return -1;
    }
    fd->dirty = 0;
    return 0;
}

static int lang_alloc_host_fd(void) {
    int i;

    for (i = 3; i < LANG_HOST_MAX_FDS; ++i) {
        if (!g_lang_host_fds[i].used) {
            return i;
        }
    }
    return -1;
}

static uint32_t lang_checksum_bytes(const uint8_t *data, uint32_t size) {
    uint32_t hash = 2166136261u;

    for (uint32_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static int lang_storage_read_retry(uint32_t lba, void *dst, uint32_t sector_count) {
    for (int attempt = 0; attempt < LANG_STORAGE_RETRY_COUNT; ++attempt) {
        if (sys_storage_read_sectors(lba, dst, sector_count) == 0) {
            return 0;
        }
        sys_sleep();
        sys_yield();
    }
    return -1;
}

static int lang_storage_read_bytes(uint32_t lba_start, void *dst, uint32_t sector_count) {
    uint8_t *out = (uint8_t *)dst;
    uint32_t remaining = sector_count;
    uint32_t current_lba = lba_start;

    while (remaining != 0u) {
        uint32_t chunk_sectors = remaining > LANG_STORAGE_BULK_SECTORS
                                     ? LANG_STORAGE_BULK_SECTORS
                                     : remaining;
        uint32_t chunk_bytes = chunk_sectors * LANG_SECTOR_SIZE;

        /*
         * Large modular apps should not burn the tiny 4 KiB task stack on
         * transient verify buffers or force thousands of synchronous round
         * trips. Metadata paths still do explicit stable double-reads; the app
         * image itself uses retried bulk reads and is validated structurally
         * after landing in the arena.
         */
        if (lang_storage_read_retry(current_lba, out, chunk_sectors) != 0) {
            return -1;
        }

        out += chunk_bytes;
        current_lba += chunk_sectors;
        remaining -= chunk_sectors;
    }

    return 0;
}

static int host_read_file(const char *path, const char **data_out, int *size_out) {
    int node;
    unsigned char *new_buf;

    if (!path || !data_out || !size_out) {
        return -1;
    }

    node = fs_resolve(path);
    if (node < 0 || g_fs_nodes[node].is_dir) {
        return -1;
    }

    if (g_fs_nodes[node].storage_kind == FS_NODE_STORAGE_IMAGE) {
        if (g_fs_nodes[node].size < 0) {
            return -1;
        }
        if (g_fs_nodes[node].size > g_host_read_file_buf_capacity) {
            new_buf = (unsigned char *)realloc(g_host_read_file_buf, (size_t)g_fs_nodes[node].size);
            if (!new_buf) {
                return -1;
            }
            g_host_read_file_buf = new_buf;
            g_host_read_file_buf_capacity = g_fs_nodes[node].size;
        }
        if (g_fs_nodes[node].size > 0 &&
            fs_read_node_bytes(node, 0, g_host_read_file_buf, g_fs_nodes[node].size) != g_fs_nodes[node].size) {
            return -1;
        }
        *data_out = (const char *)g_host_read_file_buf;
        *size_out = g_fs_nodes[node].size;
        return 0;
    }

    *data_out = g_fs_nodes[node].data;
    *size_out = g_fs_nodes[node].size;
    return 0;
}

static int host_write_file(const char *path, const void *data, int size) {
    if (!path || (!data && size > 0) || size < 0) {
        return -1;
    }
    return fs_write_bytes(path, (const uint8_t *)data, size);
}

static int host_create_dir(const char *path) {
    if (!path || path[0] == '\0') {
        return -1;
    }
    return fs_create(path, 1);
}

static int host_open_file(const char *path, int flags) {
    int fd_index;
    int node;
    int accmode;
    struct lang_host_fd *fd;

    if (!path || path[0] == '\0') {
        return -1;
    }

    fd_index = lang_alloc_host_fd();
    if (fd_index < 0) {
        return -1;
    }

    fd = &g_lang_host_fds[fd_index];
    lang_memset(fd, 0, (uint32_t)sizeof(*fd));
    fd->used = 1;
    fd->flags = flags;
    str_copy_limited(fd->path, path, (int)sizeof(fd->path));

    accmode = flags & LANG_HOST_O_ACCMODE;
    node = fs_resolve(path);
    if (node >= 0) {
        if (g_fs_nodes[node].is_dir) {
            lang_reset_host_fd(fd);
            return -1;
        }
        if (lang_host_load_file(fd, node) != 0) {
            lang_reset_host_fd(fd);
            return -1;
        }
    } else if ((flags & LANG_HOST_O_CREAT) == 0) {
        lang_reset_host_fd(fd);
        return -1;
    }

    if ((flags & LANG_HOST_O_TRUNC) && accmode != LANG_HOST_O_RDONLY) {
        fd->size = 0;
        fd->pos = 0;
        fd->dirty = 1;
    }
    if (flags & LANG_HOST_O_APPEND) {
        fd->pos = fd->size;
    }
    return fd_index;
}

static int host_read_fd(int fd_index, void *buf, int size) {
    struct lang_host_fd *fd;

    if (!buf || size < 0 || fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used || ((fd->flags & LANG_HOST_O_ACCMODE) == LANG_HOST_O_WRONLY)) {
        return -1;
    }
    if (fd->pos >= fd->size) {
        return 0;
    }
    if (fd->pos + size > fd->size) {
        size = fd->size - fd->pos;
    }
    if (size <= 0) {
        return 0;
    }
    lang_memcpy(buf, fd->buf + fd->pos, (uint32_t)size);
    fd->pos += size;
    return size;
}

static int host_write_fd(int fd_index, const void *buf, int size) {
    struct lang_host_fd *fd;
    int accmode;

    if ((!buf && size > 0) || size < 0 || fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used) {
        return -1;
    }
    accmode = fd->flags & LANG_HOST_O_ACCMODE;
    if (accmode == LANG_HOST_O_RDONLY) {
        return -1;
    }
    if (fd->flags & LANG_HOST_O_APPEND) {
        fd->pos = fd->size;
    }
    if (size == 0) {
        return 0;
    }
    if (lang_host_reserve(fd, fd->pos + size) != 0) {
        return -1;
    }
    lang_memcpy(fd->buf + fd->pos, buf, (uint32_t)size);
    fd->pos += size;
    if (fd->pos > fd->size) {
        fd->size = fd->pos;
    }
    fd->dirty = 1;
    return size;
}

static int host_close_fd(int fd_index) {
    struct lang_host_fd *fd;

    if (fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used) {
        return -1;
    }
    if (lang_host_flush_fd(fd) != 0) {
        return -1;
    }
    lang_reset_host_fd(fd);
    return 0;
}

static int host_seek_fd(int fd_index, int offset, int whence) {
    struct lang_host_fd *fd;
    int base;
    int next;

    if (fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used) {
        return -1;
    }

    if (whence == LANG_HOST_SEEK_SET) {
        base = 0;
    } else if (whence == LANG_HOST_SEEK_CUR) {
        base = fd->pos;
    } else if (whence == LANG_HOST_SEEK_END) {
        base = fd->size;
    } else {
        return -1;
    }

    next = base + offset;
    if (next < 0) {
        return -1;
    }
    fd->pos = next;
    return fd->pos;
}

static int host_stat_path(const char *path, struct vibe_app_stat *stat_out) {
    int node;

    if (!path || !stat_out) {
        return -1;
    }
    node = fs_resolve(path);
    if (node < 0) {
        return -1;
    }
    stat_out->size = g_fs_nodes[node].size;
    stat_out->is_dir = g_fs_nodes[node].is_dir;
    return 0;
}

static int host_fstat_fd(int fd_index, struct vibe_app_stat *stat_out) {
    struct lang_host_fd *fd;

    if (!stat_out || fd_index < 0 || fd_index >= LANG_HOST_MAX_FDS) {
        return -1;
    }
    fd = &g_lang_host_fds[fd_index];
    if (!fd->used) {
        return -1;
    }
    stat_out->size = fd->size;
    stat_out->is_dir = 0;
    return 0;
}

static const char *host_getenv_value(const char *name) {
    static char cwd_buf[80];

    if (!name || name[0] == '\0') {
        return 0;
    }
    if (str_eq(name, "HOME")) {
        return "/home/user";
    }
    if (str_eq(name, "USER")) {
        return "user";
    }
    if (str_eq(name, "TMPDIR")) {
        return "/tmp";
    }
    if (str_eq(name, "PATH")) {
        return "/bin:/usr/bin:/compat/bin";
    }
    if (str_eq(name, "JAVA_HOME")) {
        return "/lang/vendor/jdk8u";
    }
    if (str_eq(name, "CLASSPATH")) {
        return ".";
    }
    if (str_eq(name, "PWD")) {
        if (host_getcwd(cwd_buf, (int)sizeof(cwd_buf)) == 0) {
            return cwd_buf;
        }
        return "/";
    }
    return 0;
}

static int host_getcwd(char *buf, int max_len) {
    int i = 0;
    char cwd[80];

    if (!buf || max_len <= 0) {
        return -1;
    }

    fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
    while (cwd[i] != '\0' && i < (max_len - 1)) {
        buf[i] = cwd[i];
        ++i;
    }
    buf[i] = '\0';
    return 0;
}

static int host_remove_dir(const char *path) {
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
    if (rc != 0) {
        return -1;
    }
    return 0;
}

static int host_keyboard_set_layout(const char *name) {
    if (!name || name[0] == '\0') {
        return -1;
    }
    return sys_keyboard_set_layout(name);
}

static int host_keyboard_get_layout(char *buf, int max_len) {
    if (!buf || max_len <= 0) {
        return -1;
    }
    return sys_keyboard_get_layout(buf, max_len);
}

static int host_keyboard_get_available_layouts(char *buf, int max_len) {
    if (!buf || max_len <= 0) {
        return -1;
    }
    return sys_keyboard_get_available_layouts(buf, max_len);
}

static int host_sync_filesystem(void) {
    fs_flush();
    return 0;
}

static const struct vibe_app_host_api g_host_api = {
    VIBE_APP_ABI_VERSION,
    console_putc,
    console_write,
    sys_poll_key,
    sys_yield,
    host_read_file,
    host_write_file,
    host_create_dir,
    sys_write_debug,
    host_getcwd,
    host_remove_dir,
    host_keyboard_set_layout,
    host_keyboard_get_layout,
    host_keyboard_get_available_layouts,
    host_open_file,
    host_read_fd,
    host_write_fd,
    host_close_fd,
    host_seek_fd,
    host_stat_path,
    host_fstat_fd,
    host_getenv_value,
    host_sync_filesystem
};

static int lang_decode_app_header(const void *raw_header,
                                  struct vibe_app_header *header_out) {
    const struct vibe_app_header *current;
    const struct vibe_app_header_legacy *legacy;

    if (raw_header == 0 || header_out == 0) {
        return -1;
    }

    current = (const struct vibe_app_header *)raw_header;
    if (current->magic != VIBE_APP_MAGIC) {
        return -1;
    }
    if (current->abi_version != VIBE_APP_ABI_VERSION) {
        sys_write_debug("lang: header abi mismatch\n");
        return -1;
    }
    if (current->header_size >= sizeof(struct vibe_app_header)) {
        lang_memcpy(header_out, current, (uint32_t)sizeof(*header_out));
        return 0;
    }

    if (current->header_size == sizeof(struct vibe_app_header_legacy)) {
        /*
         * The oldest AppFS layout predates the dedicated load-address field.
         * Those binaries were linked against an earlier low-memory arena map,
         * so on the current kernel we fail loudly instead of pretending the
         * layout is interchangeable.
         */
        legacy = (const struct vibe_app_header_legacy *)raw_header;
        if (legacy->magic == VIBE_APP_MAGIC &&
            legacy->abi_version == VIBE_APP_ABI_VERSION) {
            sys_write_debug("lang: legacy header layout requires rebuild\n");
        }
        return -1;
    }

    sys_write_debug("lang: header size unsupported\n");
    return -1;
}

static int lang_load_address_valid(uint32_t load_address) {
    return load_address == VIBE_APP_LOAD_ADDR ||
           load_address == VIBE_APP_COMPAT_LOAD_ADDR_20260325 ||
           load_address == VIBE_APP_DESKTOP_LOAD_ADDR ||
           load_address == VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325 ||
           load_address == VIBE_APP_BOOT_LOAD_ADDR ||
           load_address == VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325;
}

void lang_invalidate_directory_cache(void) {
    g_cached_directory_valid = 0;
    lang_memset(&g_cached_directory, 0, (uint32_t)sizeof(g_cached_directory));
}

static int lang_catalog_command_exists(const char *name) {
    if (name == 0 || name[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < (int)G_APP_CATALOG_SHELL_COMMANDS_COUNT; ++i) {
        if (str_eq(name, g_app_catalog_shell_commands[i])) {
            return 1;
        }
    }

    return 0;
}

static const char *lang_command_alias_target(const char *name) {
    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (str_eq(name, "vi") || str_eq(name, "vim")) {
        return "edit";
    }
    if (str_eq(name, "mg")) {
        return "nano";
    }
    return 0;
}

int lang_normalize_command_name(const char *name_or_path, char *normalized, int max_len) {
    int node;
    const char *alias_target;

    if (name_or_path == 0 || normalized == 0 || max_len <= 0 || name_or_path[0] == '\0') {
        return -1;
    }

    if (!lang_has_slash(name_or_path)) {
        alias_target = lang_command_alias_target(name_or_path);
        str_copy_limited(normalized, alias_target ? alias_target : name_or_path, max_len);
        return 0;
    }

    node = fs_resolve(name_or_path);
    if (node >= 0) {
        if (g_fs_nodes[node].is_dir) {
            return -1;
        }
        alias_target = lang_command_alias_target(lang_path_basename(name_or_path));
        str_copy_limited(normalized,
                         alias_target ? alias_target : lang_path_basename(name_or_path),
                         max_len);
        return 0;
    }

    if (fs_lookup_executable_alias(name_or_path, normalized, max_len) == 0) {
        alias_target = lang_command_alias_target(normalized);
        if (alias_target != 0) {
            str_copy_limited(normalized, alias_target, max_len);
        }
        return 0;
    }

    return -1;
}

int lang_can_run(const char *name) {
    struct vibe_appfs_directory directory;
    char normalized[64];

    if (!name || name[0] == '\0') {
        return 0;
    }

    if (lang_normalize_command_name(name, normalized, (int)sizeof(normalized)) != 0) {
        return 0;
    }

    if (lang_has_runtime_stub(normalized)) {
        return 1;
    }

    /*
     * Keep the permissive catalog-read fallback so direct launch callers can
     * still surface concrete AppFS/storage errors instead of collapsing every
     * miss into "unknown command".
     */
    if (lang_load_directory(&directory) != 0) {
        return 1;
    }

    return lang_find_entry(&directory, normalized) != 0 ? 1 : 0;
}

static int lang_has_runtime_stub(const char *name) {
    return lang_catalog_command_exists(name);
}

static int lang_load_directory(struct vibe_appfs_directory *directory) {
    static uint8_t raw_directory[VIBE_APPFS_DIRECTORY_SECTORS * 512u];
    static uint8_t verify_directory[VIBE_APPFS_DIRECTORY_SECTORS * 512u];
    uint32_t checksum;
    uint32_t computed_checksum;

    if (!directory) {
        return -1;
    }

    if (g_cached_directory_valid) {
        lang_memcpy(directory, &g_cached_directory, (uint32_t)sizeof(*directory));
        return 0;
    }

    for (int attempt = 0; attempt < LANG_DIRECTORY_RETRY_COUNT; ++attempt) {
        if (lang_storage_read_bytes(VIBE_APPFS_DIRECTORY_LBA,
                                    raw_directory,
                                    VIBE_APPFS_DIRECTORY_SECTORS) != 0) {
            sys_write_debug("lang: directory read failed\n");
            sys_sleep();
            sys_yield();
            continue;
        }
        if (lang_storage_read_bytes(VIBE_APPFS_DIRECTORY_LBA,
                                    verify_directory,
                                    VIBE_APPFS_DIRECTORY_SECTORS) != 0) {
            sys_write_debug("lang: directory verify read failed\n");
            sys_sleep();
            sys_yield();
            continue;
        }
        if (lang_memcmp(raw_directory,
                        verify_directory,
                        (uint32_t)sizeof(raw_directory)) != 0) {
            sys_write_debug("lang: directory unstable read\n");
            sys_sleep();
            sys_yield();
            continue;
        }
        lang_memcpy(directory, raw_directory, (uint32_t)sizeof(*directory));

        if (directory->magic != VIBE_APPFS_MAGIC ||
            directory->version != VIBE_APPFS_VERSION ||
            directory->entry_count > VIBE_APPFS_ENTRY_MAX) {
            if (raw_directory[510] == 0x55u && raw_directory[511] == 0xAAu) {
                sys_write_debug("lang: directory looks like boot sector\n");
            } else if (raw_directory[0] == 0u &&
                       raw_directory[1] == 0u &&
                       raw_directory[2] == 0u &&
                       raw_directory[3] == 0u) {
                sys_write_debug("lang: directory looks zeroed\n");
            }
            sys_write_debug("lang: directory header invalid\n");
            sys_sleep();
            sys_yield();
            continue;
        }

        checksum = directory->checksum;
        directory->checksum = 0u;
        computed_checksum = lang_checksum_bytes((const uint8_t *)directory,
                                                (uint32_t)sizeof(*directory));
        if (checksum != computed_checksum) {
            directory->checksum = checksum;
            sys_write_debug("lang: directory checksum mismatch\n");
            sys_sleep();
            sys_yield();
            continue;
        }
        directory->checksum = checksum;
        lang_memcpy(&g_cached_directory, directory, (uint32_t)sizeof(*directory));
        g_cached_directory_valid = 1;
        return 0;
    }
    return -1;
}

static const struct vibe_appfs_entry *lang_find_entry(const struct vibe_appfs_directory *directory,
                                                      const char *name) {
    if (!directory || !name) {
        return 0;
    }

    for (uint32_t i = 0; i < VIBE_APPFS_ENTRY_MAX; ++i) {
        if (directory->entries[i].name[0] == '\0') {
            continue;
        }
        lang_debug_vga(12, "lang: cmp");
        if (str_eq(name, directory->entries[i].name)) {
            return &directory->entries[i];
        }
    }
    return 0;
}

static int lang_read_header_copy(const struct vibe_appfs_entry *entry,
                                 struct vibe_app_header *header_out) {
    uint8_t first_sector[LANG_SECTOR_SIZE];
    uint8_t verify_sector[LANG_STORAGE_VERIFY_SECTORS * LANG_SECTOR_SIZE];

    if (!entry || !header_out || entry->sector_count == 0u) {
        return -1;
    }
    if (lang_storage_read_bytes(entry->lba_start, first_sector, 1u) != 0) {
        sys_write_debug("lang: header read failed\n");
        return -1;
    }
    if (lang_storage_read_bytes(entry->lba_start, verify_sector, 1u) != 0) {
        sys_write_debug("lang: header verify read failed\n");
        return -1;
    }
    if (lang_memcmp(first_sector, verify_sector, LANG_SECTOR_SIZE) != 0) {
        sys_write_debug("lang: header unstable read\n");
        return -1;
    }

    if (lang_decode_app_header(first_sector, header_out) != 0) {
        sys_write_debug("lang: header invalid before read\n");
        return -1;
    }
    return 0;
}

static int lang_prepare_context(const struct vibe_appfs_entry *entry,
                                const struct vibe_app_header *header_copy_in,
                                struct vibe_app_header **header_out,
                                struct vibe_app_context *ctx_out) {
    struct vibe_app_header header_copy;
    struct vibe_app_header *header;
    uint8_t *load_base;
    uintptr_t load_address;
    uintptr_t heap_base;
    uintptr_t heap_limit;

    if (!entry || !header_out || !ctx_out || header_copy_in == 0 || entry->sector_count == 0u) {
        return -1;
    }

    lang_debug_vga(17, "lang: prep");
    if (entry->sector_count > VIBE_APPFS_APP_AREA_SECTORS) {
        sys_write_debug("lang: entry exceeds app area\n");
        return -1;
    }
    if (entry->sector_count > (lang_app_arena_size(header_copy_in->load_address) / LANG_SECTOR_SIZE)) {
        sys_write_debug("lang: entry exceeds arena\n");
        return -1;
    }

    header_copy = *header_copy_in;

    load_address = (uintptr_t)header_copy.load_address;
    if (!lang_load_address_valid((uint32_t)load_address)) {
        sys_write_debug("lang: load address invalid\n");
        return -1;
    }
    load_base = (uint8_t *)load_address;

    lang_debug_vga(18, "lang: read app");
    if (lang_storage_read_bytes(entry->lba_start, load_base, entry->sector_count) != 0) {
        return -1;
    }
    sys_write_debug("lang: image read ok\n");
    lang_debug_vga(19, "lang: app read ok");

    /*
     * 0.6 (30/10) witness: re-read just the first sector (the same path the
     * pre-read header double-read uses, which succeeds) and compare it with
     * what the bulk read landed. Discriminates the "header invalid after
     * read" failure: match => decode-side problem; mismatch with landed all
     * zero => the transfer/copy never landed; mismatch with landed garbage
     * => the arena was overwritten after the read; mismatch with fresh
     * garbage => the disk read itself is unstable.
     */
    {
        uint8_t verify_chunk[LANG_SECTOR_SIZE];

        if (lang_storage_read_bytes(entry->lba_start, verify_chunk, 1u) == 0) {
            if (lang_memcmp(verify_chunk, load_base, 64u) == 0) {
                sys_write_debug("lang: chunk0 verify match\n");
            } else {
                sys_write_debug("lang: chunk0 verify MISMATCH\n");
                lang_hex_dump_dbg("fresh ", verify_chunk);
                lang_hex_dump_dbg("landed", load_base);
            }
        }
    }

    header = (struct vibe_app_header *)load_base;
    if (lang_decode_app_header(load_base, &header_copy) != 0) {
        sys_write_debug("lang: header invalid after read\n");
        return -1;
    }
    lang_memcpy(header, &header_copy, (uint32_t)sizeof(header_copy));
    lang_debug_vga(20, "lang: hdr ok");

    if (header->load_address != (uint32_t)load_address) {
        sys_write_debug("lang: header load mismatch\n");
        return -1;
    }

    if (header->image_size == 0u ||
        header->image_size > entry->image_size ||
        header->memory_size < header->image_size ||
        header->memory_size > lang_app_arena_size((uint32_t)load_address) ||
        header->entry_offset >= header->memory_size ||
        header->entry_offset >= header->image_size) {
        sys_write_debug("lang: header sizing invalid\n");
        return -1;
    }

    if (header->name[0] != '\0' && !str_eq(header->name, entry->name)) {
        sys_write_debug("lang: header name mismatch\n");
        return -1;
    }

    lang_memset(load_base + header->image_size, 0, header->memory_size - header->image_size);

    heap_base = align_up_uintptr(load_address + header->memory_size, 16u);
    heap_limit = load_address + lang_app_arena_size((uint32_t)load_address) - lang_app_stack_size((uint32_t)load_address);
    if (heap_base >= heap_limit) {
        sys_write_debug("lang: heap base beyond limit\n");
        return -1;
    }

    ctx_out->host = &g_host_api;
    ctx_out->heap_base = (void *)heap_base;
    ctx_out->heap_size = (uint32_t)(heap_limit - heap_base);
    if (ctx_out->heap_size < header->required_heap_size) {
        sys_write_debug("lang: required heap exceeds available\n");
        return -1;
    }

    *header_out = header;
    return 0;
}

static void lang_write_load_error(const char *name) {
    console_write("erro: falha ao carregar app externo");
    if (name && name[0] != '\0') {
        console_write(" ");
        console_write(name);
    }
    console_putc('\n');
}

static void lang_debug_vga(int row, const char *text) {
    (void)row;
    (void)text;
}

static void lang_write_missing_runtime(const char *name) {
    console_write("erro: runtime externo nao instalado: ");
    console_write(name);
    console_putc('\n');
}

static uintptr_t lang_app_stack_top(uint32_t load_address) {
    return align_down_uintptr((uintptr_t)load_address + lang_app_arena_size(load_address), 16u);
}

struct lang_ring3_bootstrap {
    uint32_t app_entry;
    uint32_t ctx_ptr;
    uint32_t argc;
    uint32_t argv_ptr;
};

static const uint8_t g_lang_ring3_stub[] = {
    0x8B, 0x44, 0x24, 0x04,       /* mov eax, [esp + 4] */
    0x8B, 0x08,                   /* mov ecx, [eax] */
    0xFF, 0x70, 0x0C,             /* push dword [eax + 12] */
    0xFF, 0x70, 0x08,             /* push dword [eax + 8] */
    0xFF, 0x70, 0x04,             /* push dword [eax + 4] */
    0xFF, 0xD1,                   /* call ecx */
    0x83, 0xC4, 0x0C,             /* add esp, 12 */
    0x31, 0xDB,                   /* xor ebx, ebx */
    0xB8, 0x2C, 0x00, 0x00, 0x00, /* mov eax, 44 */
    0xCD, 0x80,                   /* int 0x80 */
    0x0F, 0x0B,                   /* ud2 */
    0xEB, 0xFE                    /* jmp $ */
};

static uint32_t lang_string_length(const char *text) {
    uint32_t len = 0u;

    if (text == 0) {
        return 0u;
    }
    while (text[len] != '\0') {
        len += 1u;
    }
    return len;
}

static int lang_task_pid_alive(uint32_t pid) {
    struct task_snapshot_summary summary;
    struct task_snapshot_entry entries[TASK_SNAPSHOT_MAX];
    uint32_t count;
    uint32_t i;

    if (pid == 0u) {
        return 0;
    }
    if (sys_task_snapshot(&summary, entries, TASK_SNAPSHOT_MAX) != 0) {
        return 1;
    }

    count = summary.total_tasks;
    if (count > TASK_SNAPSHOT_MAX) {
        count = TASK_SNAPSHOT_MAX;
    }
    for (i = 0u; i < count; ++i) {
        if (entries[i].pid == pid && entries[i].state != 3u) {
            return 1;
        }
    }
    return 0;
}

static int lang_wait_for_task_exit(uint32_t pid) {
    if (pid == 0u) {
        return -1;
    }

    while (lang_task_pid_alive(pid)) {
        sys_yield();
    }
    return 0;
}

static int lang_ring3_direct_allowed(const char *normalized_name,
                                     const struct vibe_app_header *header) {
    if (normalized_name == 0 || header == 0) {
        return 0;
    }

    switch (header->load_address) {
    case VIBE_APP_DESKTOP_LOAD_ADDR:
    case VIBE_APP_COMPAT_DESKTOP_LOAD_ADDR_20260325:
    case VIBE_APP_BOOT_LOAD_ADDR:
    case VIBE_APP_COMPAT_BOOT_LOAD_ADDR_20260325:
        return 1;
    default:
        break;
    }

    return str_eq(normalized_name, "taskmgr") ||
           str_eq(normalized_name, "audiosvc") ||
           str_eq(normalized_name, "soundctl") ||
           str_eq(normalized_name, "netmgrd") ||
           str_eq(normalized_name, "netctl") ||
           str_eq(normalized_name, "userland");
}

static int lang_prepare_ring3_payload(vibe_app_entry_t app_entry,
                                      const struct vibe_app_header *header,
                                      const struct vibe_app_context *ctx_in,
                                      int argc,
                                      char **argv,
                                      uintptr_t *stub_entry_out,
                                      uintptr_t *bootstrap_out) {
    uintptr_t heap_base;
    uintptr_t heap_limit;
    uintptr_t payload_base;
    uintptr_t cursor;
    uintptr_t string_cursor;
    uint32_t strings_bytes = 0u;
    uint32_t argv_bytes;
    uint32_t total_bytes;
    struct vibe_app_context *user_ctx;
    struct lang_ring3_bootstrap *bootstrap;
    char **argv_ptrs;
    int i;

    if (app_entry == 0 || header == 0 || ctx_in == 0 ||
        stub_entry_out == 0 || bootstrap_out == 0 || argc <= 0 || argv == 0) {
        return -1;
    }

    heap_base = (uintptr_t)ctx_in->heap_base;
    heap_limit = heap_base + ctx_in->heap_size;
    argv_bytes = (uint32_t)((argc + 1) * (int)sizeof(char *));
    for (i = 0; i < argc; ++i) {
        if (argv[i] == 0) {
            return -1;
        }
        strings_bytes += lang_string_length(argv[i]) + 1u;
    }

    total_bytes =
        (uint32_t)align_up_uintptr(sizeof(struct vibe_app_context), 16u) +
        (uint32_t)align_up_uintptr(sizeof(struct lang_ring3_bootstrap), 16u) +
        (uint32_t)align_up_uintptr(argv_bytes, 16u) +
        strings_bytes +
        (uint32_t)align_up_uintptr(sizeof(g_lang_ring3_stub), 16u);
    if ((uintptr_t)total_bytes >= ctx_in->heap_size) {
        return -1;
    }

    payload_base = align_down_uintptr(heap_limit - total_bytes, 16u);
    if (payload_base < heap_base) {
        return -1;
    }

    user_ctx = (struct vibe_app_context *)payload_base;
    *user_ctx = *ctx_in;
    user_ctx->host = 0;
    user_ctx->heap_base = (void *)heap_base;
    user_ctx->heap_size = (uint32_t)(payload_base - heap_base);
    if (user_ctx->heap_size < header->required_heap_size) {
        return -1;
    }

    cursor = align_up_uintptr(payload_base + sizeof(*user_ctx), 16u);
    bootstrap = (struct lang_ring3_bootstrap *)cursor;
    cursor = align_up_uintptr(cursor + sizeof(*bootstrap), 16u);
    argv_ptrs = (char **)cursor;
    cursor = align_up_uintptr(cursor + argv_bytes, 16u);
    string_cursor = cursor;

    for (i = 0; i < argc; ++i) {
        uint32_t len = lang_string_length(argv[i]) + 1u;

        argv_ptrs[i] = (char *)string_cursor;
        lang_memcpy((void *)string_cursor, argv[i], len);
        string_cursor += len;
    }
    argv_ptrs[argc] = 0;

    cursor = align_up_uintptr(string_cursor, 16u);
    lang_memcpy((void *)cursor, g_lang_ring3_stub, (uint32_t)sizeof(g_lang_ring3_stub));

    bootstrap->app_entry = (uint32_t)(uintptr_t)app_entry;
    bootstrap->ctx_ptr = (uint32_t)(uintptr_t)user_ctx;
    bootstrap->argc = (uint32_t)argc;
    bootstrap->argv_ptr = (uint32_t)(uintptr_t)argv_ptrs;

    *stub_entry_out = cursor;
    *bootstrap_out = (uintptr_t)bootstrap;
    return 0;
}

static int lang_launch_ring3_app(const char *normalized_name,
                                 vibe_app_entry_t app_entry,
                                 const struct vibe_app_header *header,
                                 const struct vibe_app_context *ctx,
                                 int argc,
                                 char **argv) {
    uintptr_t stub_entry;
    uintptr_t bootstrap_ptr;
    int pid;

    if (!lang_ring3_direct_allowed(normalized_name, header)) {
        return -1;
    }
    if (lang_prepare_ring3_payload(app_entry,
                                   header,
                                   ctx,
                                   argc,
                                   argv,
                                   &stub_entry,
                                   &bootstrap_ptr) != 0) {
        return -1;
    }

    sys_write_debug("lang: launching ring3 child\n");
    pid = sys_task_create(stub_entry, (void *)bootstrap_ptr, 16384u, 0u);
    if (pid <= 0) {
        return -1;
    }

    if (lang_wait_for_task_exit((uint32_t)pid) != 0) {
        return -1;
    }
    return 0;
}

__attribute__((noinline, optimize("O0")))
static int lang_call_app(vibe_app_entry_t entry,
                         const struct vibe_app_context *ctx,
                         int argc,
                         char **argv,
                         uintptr_t stack_top) {
    uintptr_t saved_esp;
    int rc;

    sys_write_debug("lang: calling app entry\n");
    __asm__ volatile(
        "mov %%esp, %[saved_esp]\n\t"
        "mov %[stack_top], %%esp\n\t"
        "push %[argv]\n\t"
        "push %[argc]\n\t"
        "push %[ctx]\n\t"
        "call *%[entry]\n\t"
        "add $12, %%esp\n\t"
        "mov %[saved_esp], %%esp\n\t"
        : [saved_esp] "=&r"(saved_esp),
          "=a"(rc)
        : [stack_top] "r"(stack_top),
          [entry] "r"(entry),
          [ctx] "g"(ctx),
          [argc] "g"(argc),
          [argv] "g"(argv)
        : "ecx", "edx", "memory", "cc");
    return rc;
}

int lang_try_run(int argc, char **argv) {
    struct vibe_appfs_directory directory;
    const struct vibe_appfs_entry *entry;
    struct vibe_app_header *header;
    struct vibe_app_header header_copy;
    vibe_app_entry_t app_entry;
    uint32_t arena_load_address = 0u;
    uintptr_t entry_addr;
    int rc = -1;
    int arena_acquired = 0;
    char normalized_name[64];

    if (argc <= 0 || !argv || !argv[0]) {
        return -1;
    }
    if (lang_normalize_command_name(argv[0], normalized_name, (int)sizeof(normalized_name)) != 0) {
        return -1;
    }

    if (sys_getpid() == 0) {
        sys_write_debug("lang: pid zero before reset\n");
    } else {
        sys_write_debug("lang: pid ok before reset\n");
    }
    lang_reset_host_fds();
    if (sys_getpid() == 0) {
        sys_write_debug("lang: pid zero after reset\n");
    } else {
        sys_write_debug("lang: pid ok after reset\n");
    }
    sys_write_debug("lang: try_run begin\n");

    lang_debug_vga(14, "lang: start");
    if (lang_load_directory(&directory) != 0) {
        sys_write_debug("lang: directory load failed\n");
        if (lang_has_runtime_stub(normalized_name)) {
            lang_write_load_error("catalog");
            return 1;
        }
        return -1;
    }
    sys_write_debug("lang: directory loaded\n");
    lang_debug_vga(15, "lang: dir ok");
    lang_debug_vga(11, directory.entries[0].name);

    entry = lang_find_entry(&directory, normalized_name);
    if (!entry) {
        sys_write_debug("lang: entry missing\n");
        lang_debug_vga(13, "lang: no entry");
        if (lang_has_runtime_stub(normalized_name)) {
            lang_write_missing_runtime(normalized_name);
            return 1;
        }
        return -1;
    }
    sys_write_debug("lang: entry found\n");
    lang_debug_vga(16, "lang: entry ok");

    if (lang_read_header_copy(entry, &header_copy) != 0) {
        lang_write_load_error(entry->name);
        return 1;
    }
    sys_write_debug("lang: header ok\n");
    arena_load_address = header_copy.load_address;
    if (lang_arena_acquire(arena_load_address) != 0) {
        sys_write_debug("lang: arena acquire failed\n");
        lang_write_load_error(entry->name);
        return 1;
    }
    sys_write_debug("lang: arena ok\n");
    arena_acquired = 1;

    if (lang_prepare_context(entry, &header_copy, &header, &g_app_ctx) != 0) {
        sys_write_debug("lang: prepare failed\n");
        lang_write_load_error(entry->name);
        rc = 1;
        goto done;
    }
    sys_write_debug("lang: prepare ok\n");
    entry_addr = (uintptr_t)header->load_address + header->entry_offset;
    app_entry = (vibe_app_entry_t)entry_addr;
    sys_write_debug("lang: app entry resolved\n");
    if (lang_launch_ring3_app(normalized_name,
                              app_entry,
                              header,
                              &g_app_ctx,
                              argc,
                              argv) != 0) {
        (void)lang_call_app(app_entry,
                            &g_app_ctx,
                            argc,
                            argv,
                            lang_app_stack_top(header->load_address));
    }
    sys_write_debug("lang: app returned\n");
    lang_reset_host_fds();
    lang_memcpy((void *)(uintptr_t)header->load_address,
                (const void *)(uintptr_t)header->load_address,
                0u);
    rc = 0;

done:
    if (arena_acquired) {
        lang_arena_release(arena_load_address);
    }
    return rc;
}
