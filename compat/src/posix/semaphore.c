#include <semaphore.h>
#include <compat/posix/errno.h>
#include <compat/libc/string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <stdlib.h>

#define MAX_NAMED_SEMS 16
#define SEM_NAME_MAX 32
#define SYSCALL_WAITABLE_INIT 188
#define SYSCALL_WAITABLE_WAIT 189
#define SYSCALL_WAITABLE_WAKE 200

struct sem_internal {
    char name[SEM_NAME_MAX];
    int value;
    int initialized;
    int named;
    unsigned long wait_data[10];
};

static struct sem_internal g_sems[MAX_NAMED_SEMS];

static long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "memory");
    return ret;
}

static unsigned long long get_ms(void) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(7) : "memory");
    return (unsigned long long)(unsigned int)ret;
}

static void sleep_ms(unsigned int ms) {
    unsigned long long deadline = get_ms() + (unsigned long long)ms;
    while (get_ms() < deadline) {
        __asm__ volatile("int $0x80" : : "a"(10) : "memory");
    }
}

static struct sem_internal *sem_find_by_name(const char *name) {
    unsigned int i;
    for (i = 0; i < MAX_NAMED_SEMS; i++) {
        if (g_sems[i].initialized && g_sems[i].named && strcmp(g_sems[i].name, name) == 0) {
            return &g_sems[i];
        }
    }
    return 0;
}

static struct sem_internal *sem_alloc_slot(void) {
    unsigned int i;
    for (i = 0; i < MAX_NAMED_SEMS; i++) {
        if (!g_sems[i].initialized) {
            return &g_sems[i];
        }
    }
    return 0;
}

int sem_init(sem_t *sem, int pshared, unsigned int value) {
    struct sem_internal *s;
    (void)pshared;
    if (!sem) {
        errno = EINVAL;
        return -1;
    }
    s = sem_alloc_slot();
    if (!s) {
        errno = ENOSPC;
        return -1;
    }
    s->value = (int)value;
    s->initialized = 1;
    s->named = 0;
    s->name[0] = '\0';
    syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)s->wait_data, 0, 0, 0, 0, 0);
    *sem = (sem_t)(uintptr_t)s;
    return 0;
}

int sem_destroy(sem_t *sem) {
    struct sem_internal *s;
    if (!sem || !*sem) {
        errno = EINVAL;
        return -1;
    }
    s = (struct sem_internal *)(uintptr_t)*sem;
    if (s->named) {
        errno = EBUSY;
        return -1;
    }
    memset(s, 0, sizeof(*s));
    *sem = 0;
    return 0;
}

sem_t *sem_open(const char *name, int oflag, ...) {
    struct sem_internal *s;
    unsigned int value = 0;
    va_list ap;

    if (!name || name[0] != '/') {
        errno = EINVAL;
        return SEM_FAILED;
    }
    s = sem_find_by_name(name);
    if (!s && (oflag & 0x0200) != 0) {
        va_start(ap, oflag);
        value = va_arg(ap, unsigned int);
        va_end(ap);
        s = sem_alloc_slot();
        if (!s) {
            errno = ENOSPC;
            return SEM_FAILED;
        }
        strncpy(s->name, name, SEM_NAME_MAX - 1);
        s->name[SEM_NAME_MAX - 1] = '\0';
        s->value = (int)value;
        s->initialized = 1;
        s->named = 1;
        syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)s->wait_data, 0, 0, 0, 0, 0);
    }
    if (!s) {
        errno = ENOENT;
        return SEM_FAILED;
    }
    return (sem_t *)(uintptr_t)s;
}

int sem_close(sem_t *sem) {
    (void)sem;
    return 0;
}

int sem_unlink(const char *name) {
    struct sem_internal *s;
    if (!name) {
        errno = EINVAL;
        return -1;
    }
    s = sem_find_by_name(name);
    if (!s) {
        errno = ENOENT;
        return -1;
    }
    memset(s, 0, sizeof(*s));
    return 0;
}

int sem_wait(sem_t *sem) {
    struct sem_internal *s;
    if (!sem || !*sem) {
        errno = EINVAL;
        return -1;
    }
    s = (struct sem_internal *)(uintptr_t)*sem;
    for (;;) {
        if (s->value > 0) {
            s->value--;
            return 0;
        }
        syscall6(SYSCALL_WAITABLE_WAIT, (long)(uintptr_t)s->wait_data, 0, 0, 0, 0, 0);
    }
}

int sem_trywait(sem_t *sem) {
    struct sem_internal *s;
    if (!sem || !*sem) {
        errno = EINVAL;
        return -1;
    }
    s = (struct sem_internal *)(uintptr_t)*sem;
    if (s->value <= 0) {
        errno = EAGAIN;
        return -1;
    }
    s->value--;
    return 0;
}

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout) {
    struct sem_internal *s;
    unsigned long long deadline_ms;
    unsigned long long now;
    if (!sem || !*sem || !abs_timeout) {
        errno = EINVAL;
        return -1;
    }
    s = (struct sem_internal *)(uintptr_t)*sem;
    deadline_ms = (unsigned long long)abs_timeout->tv_sec * 1000ull +
                  (unsigned long long)(abs_timeout->tv_nsec / 1000000l);
    for (;;) {
        if (s->value > 0) {
            s->value--;
            return 0;
        }
        now = get_ms();
        if (now >= deadline_ms) {
            errno = ETIMEDOUT;
            return -1;
        }
        sleep_ms(1u);
    }
}

int sem_post(sem_t *sem) {
    struct sem_internal *s;
    if (!sem || !*sem) {
        errno = EINVAL;
        return -1;
    }
    s = (struct sem_internal *)(uintptr_t)*sem;
    s->value++;
    syscall6(SYSCALL_WAITABLE_WAKE, (long)(uintptr_t)s->wait_data, 1, 0, 0, 0, 0);
    return 0;
}

int sem_getvalue(sem_t *sem, int *sval) {
    struct sem_internal *s;
    if (!sem || !*sem || !sval) {
        errno = EINVAL;
        return -1;
    }
    s = (struct sem_internal *)(uintptr_t)*sem;
    *sval = s->value;
    return 0;
}
