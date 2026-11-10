/* Host-side unit test for the fdopen() fd-backed stream path in
 * lang/sdk/app_runtime.c (VIBE_FILE_FLAG_FD).
 *
 * The runtime under test defines its own printf/fwrite/... so this file
 * cannot use host stdio; diagnostics go out through a raw Linux i386
 * write(2) syscall instead, and the process exit code carries the failure
 * count.
 *
 * Build+run:
 *   gcc -m32 -ffreestanding -fno-builtin -I. -Ilang/include \
 *       tests/host_test_fdopen.c lang/sdk/app_runtime.c -o /tmp/fdopen_test
 *   /tmp/fdopen_test
 */
#define EOF (-1)

#include <lang/include/vibe_app.h>
#include <lang/include/vibe_app_runtime.h>

#define TEST_FD_COUNT 8
static unsigned char fd_backing[TEST_FD_COUNT][512];
static int fd_used[TEST_FD_COUNT];
static int fd_size[TEST_FD_COUNT];
static int fd_pos[TEST_FD_COUNT];
static int fd_closed[TEST_FD_COUNT];

static void tprint(const char *s) {
    int len = 0;

    while (s[len] != '\0') {
        ++len;
    }
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory");
}

static void tprint_int(int v) {
    char buf[12];
    int i = 0;

    if (v == 0) {
        tprint("0");
        return;
    }
    if (v < 0) {
        tprint("-");
        v = -v;
    }
    while (v > 0 && i < 11) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        char one[2];

        one[0] = buf[--i];
        one[1] = '\0';
        tprint(one);
    }
}

static int test_open_file(const char *path, int flags) {
    (void)path;
    (void)flags;
    return -1; /* the fdopen path never needs open_file */
}

static int test_read_fd(int fd, void *buf, int size) {
    if (fd < 0 || fd >= TEST_FD_COUNT || !fd_used[fd] || size < 0) return -1;
    if (fd_pos[fd] >= fd_size[fd]) return 0;
    if (fd_pos[fd] + size > fd_size[fd]) size = fd_size[fd] - fd_pos[fd];
    memcpy(buf, fd_backing[fd] + fd_pos[fd], (unsigned int)size);
    fd_pos[fd] += size;
    return size;
}

static int test_write_fd(int fd, const void *buf, int size) {
    if (fd < 0 || fd >= TEST_FD_COUNT || !fd_used[fd] || size < 0) return -1;
    if (fd_pos[fd] + size > 512) return -1;
    memcpy(fd_backing[fd] + fd_pos[fd], buf, (unsigned int)size);
    fd_pos[fd] += size;
    if (fd_pos[fd] > fd_size[fd]) fd_size[fd] = fd_pos[fd];
    return size;
}

static int test_close_fd(int fd) {
    if (fd < 0 || fd >= TEST_FD_COUNT || !fd_used[fd]) return -1;
    fd_used[fd] = 0;
    fd_closed[fd] = 1;
    return 0;
}

static int test_seek_fd(int fd, int offset, int whence) {
    int base;

    if (fd < 0 || fd >= TEST_FD_COUNT || !fd_used[fd]) return -1;
    if (whence == 0) base = 0;
    else if (whence == 1) base = fd_pos[fd];
    else if (whence == 2) base = fd_size[fd];
    else return -1;
    if (base + offset < 0 || base + offset > 512) return -1;
    fd_pos[fd] = base + offset;
    return fd_pos[fd];
}

static const struct vibe_app_host_api test_host = {
    1u,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    test_open_file, test_read_fd, test_write_fd, test_close_fd, test_seek_fd,
    NULL, NULL, NULL, NULL
};

static struct vibe_app_context test_ctx;
static char heap_area[65536];
static int failures;

static int t_strlen(const char *s) {
    int n = 0;

    while (s[n] != '\0') ++n;
    return n;
}

static int t_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

#define CHECK(cond, name) do { \
    if (!(cond)) { tprint("FAIL "); tprint(name); tprint("\n"); ++failures; } \
    else { tprint("pass "); tprint(name); tprint("\n"); } \
} while (0)

int main(void) {
    const char *msg = "hello fdopen";
    int msg_len = t_strlen(msg);
    char got[64];
    int i;

    test_ctx.host = &test_host;
    test_ctx.heap_base = heap_area;
    test_ctx.heap_size = sizeof(heap_area);
    vibe_app_runtime_init(&test_ctx);

    /* ---- write path through fd 4 ---- */
    fd_used[4] = 1;
    fd_pos[4] = 0;
    fd_size[4] = 0;
    FILE *w = fdopen(4, "w");
    CHECK(w != NULL, "fdopen(w) returns a stream");
    CHECK(ftell(w) == 0, "new stream starts at offset 0");
    CHECK(fwrite(msg, 1, (size_t)msg_len, w) == (size_t)msg_len, "fwrite pushes all bytes to the host fd");
    CHECK(ftell(w) == (long)msg_len, "ftell tracks fd position after write");
    CHECK(fd_size[4] == msg_len, "host buffer received the bytes");
    CHECK(fclose(w) == 0, "fclose returns 0");
    CHECK(fd_closed[4] == 1, "fclose routes through host close_fd");

    /* ---- read path through fd 5 ---- */
    fd_used[5] = 1;
    fd_pos[5] = 0;
    fd_size[5] = msg_len;
    for (i = 0; i < msg_len; ++i) fd_backing[5][i] = (unsigned char)msg[i];

    FILE *r = fdopen(5, "r");
    CHECK(r != NULL, "fdopen(r) returns a stream");
    for (i = 0; i < (int)sizeof(got); ++i) got[i] = '\0';
    CHECK(fread(got, 1, (size_t)msg_len, r) == (size_t)msg_len, "fread pulls all bytes from the host fd");
    CHECK(t_strcmp(got, msg) == 0, "fread content matches");
    CHECK(fgetc(r) == EOF, "fgetc past end returns EOF");
    CHECK(feof(r) != 0, "feof is set after EOF");
    CHECK(fseek(r, 0, 0) == 0, "fseek(SEEK_SET) rewinds through host seek_fd");
    for (i = 0; i < (int)sizeof(got); ++i) got[i] = '\0';
    CHECK(fgets(got, (int)sizeof(got), r) != NULL, "fgets reads after rewind");
    CHECK(t_strcmp(got, msg) == 0, "fgets content matches");
    CHECK(fclose(r) == 0, "fclose on read stream returns 0");

    /* ---- rejections and console mapping ---- */
    CHECK(fdopen(6, "q") == NULL, "invalid mode is rejected");
    CHECK(fdopen(1, "w") == stdout, "fd 1 maps to canonical stdout");
    CHECK(fdopen(2, "w") == stderr, "fd 2 maps to canonical stderr");
    CHECK(fdopen(0, "r") == stdin, "fd 0 maps to canonical stdin");

    if (failures == 0) {
        tprint("ALL PASS\n");
        return 0;
    }
    tprint("FAILURES: ");
    tprint_int(failures);
    tprint("\n");
    return failures;
}