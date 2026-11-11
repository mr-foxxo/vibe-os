#include <utmpx.h>
#include <compat/posix/errno.h>
#include <compat/libc/string.h>
#include <compat/posix/unistd.h>
#include <stdint.h>
#include <include/userland_api.h>

#define UTMPX_ENTRIES 8
#define SYSCALL_TIME_TICKS 7

static unsigned int ut_ticks(void) {
    unsigned int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYSCALL_TIME_TICKS) : "memory");
    return ret;
}

static unsigned int ut_clock_hz(void) {
    return 1000u;
}

static struct utmpx g_utmpx_buf[UTMPX_ENTRIES];
static int g_utmpx_count = 0;
static int g_utmpx_index = 0;
static int g_utmpx_open = 0;

void setutxent(void) {
    g_utmpx_index = 0;
    g_utmpx_open = 1;
}

void endutxent(void) {
    g_utmpx_index = 0;
    g_utmpx_count = 0;
    g_utmpx_open = 0;
}

static void utmpx_build_current(struct utmpx *u) {
    if (!u) return;
    memset(u, 0, sizeof(*u));
    u->ut_pid = getpid();
    u->ut_type = USER_PROCESS;
    strncpy(u->ut_line, "pts/0", UTX_LINESIZE - 1);
    strncpy(u->ut_user, "user", UTX_NAMESIZE - 1);
    strncpy(u->ut_host, "localhost", UTX_HOSTSIZE - 1);
    u->ut_tv.tv_sec = (long)(ut_ticks() / ut_clock_hz());
    u->ut_tv.tv_usec = 0;
}

struct utmpx *getutxent(void) {
    if (!g_utmpx_open) {
        setutxent();
    }
    if (g_utmpx_count == 0) {
        utmpx_build_current(&g_utmpx_buf[0]);
        g_utmpx_count = 1;
    }
    if (g_utmpx_index >= g_utmpx_count) {
        return 0;
    }
    return &g_utmpx_buf[g_utmpx_index++];
}

struct utmpx *getutxid(const struct utmpx *id) {
    struct utmpx *u;
    if (!id) return 0;
    if (!g_utmpx_open) setutxent();
    while ((u = getutxent()) != 0) {
        if (id->ut_type == USER_PROCESS || id->ut_type == INIT_PROCESS ||
            id->ut_type == LOGIN_PROCESS) {
            if (u->ut_type == id->ut_type) return u;
        }
        if (id->ut_type == ACCOUNTING || id->ut_type == BOOT_TIME) {
            if (u->ut_type == id->ut_type) return u;
        }
        if (id->ut_id[0] != '\0') {
            if (memcmp(u->ut_id, id->ut_id, 4) == 0) return u;
        }
    }
    return 0;
}

struct utmpx *getutxline(const struct utmpx *line) {
    struct utmpx *u;
    if (!line) return 0;
    if (!g_utmpx_open) setutxent();
    while ((u = getutxent()) != 0) {
        if (strncmp(u->ut_line, line->ut_line, UTX_LINESIZE) == 0) return u;
    }
    return 0;
}

struct utmpx *pututxline(const struct utmpx *utmpx) {
    int i;
    if (!utmpx) return 0;
    for (i = 0; i < g_utmpx_count; i++) {
        if (g_utmpx_buf[i].ut_pid == utmpx->ut_pid) {
            g_utmpx_buf[i] = *utmpx;
            return &g_utmpx_buf[i];
        }
    }
    if (g_utmpx_count < UTMPX_ENTRIES) {
        g_utmpx_buf[g_utmpx_count] = *utmpx;
        return &g_utmpx_buf[g_utmpx_count++];
    }
    return 0;
}
