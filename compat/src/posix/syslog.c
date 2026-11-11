#include <syslog.h>
#include <compat/posix/errno.h>
#include <compat/libc/string.h>
#include <stdint.h>
#include <stdarg.h>

static int g_log_option = 0;
static int g_log_facility = LOG_USER;
static const char *g_ident = 0;
static int g_log_opened = 0;
static int g_log_mask = 0xff;

static void debug_write(const char *s) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(11), "b"((long)(uintptr_t)s) : "memory");
    (void)ret;
}

void openlog(const char *ident, int option, int facility) {
    g_ident = ident;
    g_log_option = option;
    g_log_facility = facility != 0 ? facility : LOG_USER;
    g_log_opened = 1;
}

void closelog(void) {
    g_ident = 0;
    g_log_option = 0;
    g_log_facility = LOG_USER;
    g_log_opened = 0;
}

int setlogmask(int mask) {
    int old = g_log_mask;
    if (mask != 0) g_log_mask = mask;
    return old;
}

static const char *log_priority_str(int priority) {
    switch (LOG_PRI(priority)) {
    case LOG_EMERG:   return "EMERG";
    case LOG_ALERT:   return "ALERT";
    case LOG_CRIT:    return "CRIT";
    case LOG_ERR:     return "ERR";
    case LOG_WARNING: return "WARNING";
    case LOG_NOTICE:  return "NOTICE";
    case LOG_INFO:    return "INFO";
    case LOG_DEBUG:   return "DEBUG";
    default:          return "UNKNOWN";
    }
}

void vsyslog(int priority, const char *format, va_list ap) {
    char buf[256];
    size_t pos = 0;

    if ((g_log_mask & LOG_MASK(LOG_PRI(priority))) == 0) return;

    if ((g_log_option & LOG_CONS) != 0 || (g_log_option & LOG_PERROR) != 0) {
        const char *id = g_ident ? g_ident : "vibeos";
        const char *p;

        for (p = id; *p && pos < sizeof(buf) - 1; p++) buf[pos++] = *p;
        if (pos < sizeof(buf) - 2) { buf[pos++] = ':'; buf[pos++] = ' '; }
        for (p = log_priority_str(priority); *p && pos < sizeof(buf) - 1; p++) buf[pos++] = *p;
        if (pos < sizeof(buf) - 2) { buf[pos++] = ':'; buf[pos++] = ' '; }
        for (p = format; *p && pos < sizeof(buf) - 1; p++) buf[pos++] = *p;
        buf[pos] = '\0';
        debug_write(buf);
    }
    (void)ap;
}

void syslog(int priority, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}
