#include <pthread.h>
#include <compat/posix/errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <include/userland_api.h>

#define SYSCALL_THREAD_CREATE 183
#define SYSCALL_THREAD_EXIT 184
#define SYSCALL_THREAD_JOIN 185
#define SYSCALL_THREAD_GETTLS 186
#define SYSCALL_THREAD_SETTLS 187
#define SYSCALL_WAITABLE_INIT 188
#define SYSCALL_WAITABLE_WAIT 189
#define SYSCALL_WAITABLE_WAKE 200
#define SYSCALL_THREAD_DETACH 199
#define SYSCALL_GETPID 9
#define SYSCALL_YIELD 10

struct pthread_attr {
    int detached;
};

struct pthread_mutex_attr {
    int type;
};

struct pthread_cond_attr {
    int unused;
};

struct pthread_rwlockattr {
    int pshared;
};

struct pthread_barrierattr {
    int pshared;
};

static void (*g_key_destructors[PTHREAD_KEYS_MAX])(void *);
static unsigned int g_key_used[(PTHREAD_KEYS_MAX + 31) / 32];

static long syscall6(long num, long a1, long a2, long a3, long a4, long a5,
                      __attribute__((unused)) long a6) {
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

static int atomic_xchg(volatile int *ptr, int value) {
    __asm__ volatile("xchgl %0, %1"
                     : "+r"(value), "+m"(*ptr)
                     :
                     : "memory", "cc");
    return value;
}

int pthread_attr_init(pthread_attr_t *attr) {
    if (!attr) return EINVAL;
    *attr = calloc(1, sizeof(struct pthread_attr));
    return *attr ? 0 : ENOMEM;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    if (!attr || !*attr) return EINVAL;
    free(*attr);
    *attr = NULL;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state) {
    if (!attr || !*attr || !state) return EINVAL;
    *state = (*attr)->detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state) {
    if (!attr || !*attr) return EINVAL;
    (*attr)->detached = (state == PTHREAD_CREATE_DETACHED);
    return 0;
}

int pthread_attr_getstack(const pthread_attr_t *attr, void **addr, size_t *size) {
    (void)attr; (void)addr; (void)size;
    return ENOSYS;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size) {
    (void)attr; (void)size;
    return ENOSYS;
}

int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **addr) {
    (void)attr; (void)addr;
    return ENOSYS;
}

int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size) {
    (void)attr; (void)size;
    return ENOSYS;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size) {
    (void)attr; (void)size;
    return ENOSYS;
}

int pthread_attr_setstack(pthread_attr_t *attr, void *addr, size_t size) {
    (void)attr; (void)addr; (void)size;
    return ENOSYS;
}

int pthread_attr_setstackaddr(pthread_attr_t *attr, void *addr) {
    (void)attr; (void)addr;
    return ENOSYS;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t size) {
    (void)attr; (void)size;
    return ENOSYS;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*fn)(void *), void *arg) {
    long ret;

    if (!thread || !fn) return EINVAL;
    ret = syscall6(SYSCALL_THREAD_CREATE, (long)(uintptr_t)fn,
                   (long)(uintptr_t)arg, 0, 0, 0, 0);
    if (ret < 0) return EAGAIN;
    *thread = (pthread_t)ret;
    if (attr && *attr && (*attr)->detached) {
        syscall6(SYSCALL_THREAD_DETACH, ret, 0, 0, 0, 0, 0);
    }
    return 0;
}

int pthread_detach(pthread_t thread) {
    long ret = syscall6(SYSCALL_THREAD_DETACH, (long)thread, 0, 0, 0, 0, 0);
    return ret == 0 ? 0 : EINVAL;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    long ret = syscall6(SYSCALL_THREAD_JOIN, (long)thread,
                        (long)(uintptr_t)value_ptr, 0, 0, 0, 0);
    return ret == 0 ? 0 : EINVAL;
}

pthread_t pthread_self(void) {
    long ret = syscall6(SYSCALL_GETPID, 0, 0, 0, 0, 0, 0);
    return (pthread_t)ret;
}

int pthread_equal(pthread_t a, pthread_t b) {
    return a == b;
}

void pthread_yield(void) {
    syscall6(SYSCALL_YIELD, 0, 0, 0, 0, 0, 0);
}

void pthread_exit(void *value) {
    syscall6(SYSCALL_THREAD_EXIT, (long)(uintptr_t)value, 0, 0, 0, 0, 0);
    for (;;) __asm__ volatile("hlt");
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    if (!attr) return EINVAL;
    *attr = calloc(1, sizeof(struct pthread_mutex_attr));
    return *attr ? 0 : ENOMEM;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    if (!attr || !*attr) return EINVAL;
    free(*attr);
    *attr = NULL;
    return 0;
}

int pthread_mutexattr_gettype(pthread_mutexattr_t *attr, int *type) {
    if (!attr || !*attr || !type) return EINVAL;
    *type = (*attr)->type;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    if (!attr || !*attr) return EINVAL;
    (*attr)->type = type;
    return 0;
}

int pthread_mutexattr_getprioceiling(pthread_mutexattr_t *attr, int *ceiling) {
    (void)attr; (void)ceiling;
    return ENOSYS;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr, int ceiling) {
    (void)attr; (void)ceiling;
    return ENOSYS;
}

int pthread_mutexattr_getprotocol(pthread_mutexattr_t *attr, int *protocol) {
    (void)attr; (void)protocol;
    return ENOSYS;
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol) {
    (void)attr; (void)protocol;
    return ENOSYS;
}

int pthread_mutex_getprioceiling(pthread_mutex_t *mutex, int *ceiling) {
    (void)mutex; (void)ceiling;
    return ENOSYS;
}

int pthread_mutex_setprioceiling(pthread_mutex_t *mutex, int ceiling, int *old_ceiling) {
    (void)mutex; (void)ceiling; (void)old_ceiling;
    return ENOSYS;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    if (!mutex) return EINVAL;
    memset(mutex, 0, sizeof(*mutex));
    mutex->type = (attr && *attr) ? (*attr)->type : PTHREAD_MUTEX_NORMAL;
    syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)mutex->__wait_data,
             0, 0, 0, 0, 0);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    if (mutex->state != 0) return EBUSY;
    memset(mutex, 0, sizeof(*mutex));
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    pid_t self;
    int old;

    if (!mutex) return EINVAL;
    self = (pid_t)pthread_self();
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->owner == self) {
        mutex->recursion_count++;
        return 0;
    }
    for (;;) {
        old = atomic_xchg(&mutex->lock, 1);
        if (old == 0) {
            mutex->owner = self;
            mutex->recursion_count = 1;
            mutex->state = 1;
            return 0;
        }
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->owner == self) {
            mutex->recursion_count++;
            return 0;
        }
        syscall6(SYSCALL_WAITABLE_WAIT,
                 (long)(uintptr_t)mutex->__wait_data, 0, 0, 0, 0, 0);
    }
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    pid_t self;
    int old;

    if (!mutex) return EINVAL;
    self = (pid_t)pthread_self();
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->owner == self) {
        mutex->recursion_count++;
        return 0;
    }
    old = atomic_xchg(&mutex->lock, 1);
    if (old == 0) {
        mutex->owner = self;
        mutex->recursion_count = 1;
        mutex->state = 1;
        return 0;
    }
    return EBUSY;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *ts) {
    (void)ts;
    return pthread_mutex_lock(mutex);
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->recursion_count > 1) {
        mutex->recursion_count--;
        return 0;
    }
    mutex->owner = 0;
    mutex->recursion_count = 0;
    mutex->state = 0;
    __asm__ volatile("" ::: "memory");
    mutex->lock = 0;
    syscall6(SYSCALL_WAITABLE_WAKE, (long)(uintptr_t)mutex->__wait_data,
             1, 0, 0, 0, 0);
    return 0;
}

int pthread_condattr_init(pthread_condattr_t *attr) {
    if (!attr) return EINVAL;
    *attr = calloc(1, sizeof(struct pthread_cond_attr));
    return *attr ? 0 : ENOMEM;
}

int pthread_condattr_destroy(pthread_condattr_t *attr) {
    if (!attr || !*attr) return EINVAL;
    free(*attr);
    *attr = NULL;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attr, clockid_t *id) {
    (void)attr; (void)id;
    return ENOSYS;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t id) {
    (void)attr; (void)id;
    return ENOSYS;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)attr;
    if (!cond) return EINVAL;
    memset(cond, 0, sizeof(*cond));
    syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)cond->__wait_data,
             0, 0, 0, 0, 0);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    if (!cond) return EINVAL;
    memset(cond, 0, sizeof(*cond));
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    if (!cond) return EINVAL;
    syscall6(SYSCALL_WAITABLE_WAKE, (long)(uintptr_t)cond->__wait_data,
             1, 0, 0, 0, 0);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    if (!cond) return EINVAL;
    syscall6(SYSCALL_WAITABLE_WAKE, (long)(uintptr_t)cond->__wait_data,
             0x7FFFFFFF, 0, 0, 0, 0);
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    if (!cond || !mutex) return EINVAL;
    pthread_mutex_unlock(mutex);
    syscall6(SYSCALL_WAITABLE_WAIT, (long)(uintptr_t)cond->__wait_data,
             0, 0, 0, 0, 0);
    pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *ts) {
    (void)ts;
    return pthread_cond_wait(cond, mutex);
}

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) {
    (void)attr;
    if (!rwlock) return EINVAL;
    memset(rwlock, 0, sizeof(*rwlock));
    syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)rwlock->__wait_rd,
             0, 0, 0, 0, 0);
    syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)rwlock->__wait_wr,
             0, 0, 0, 0, 0);
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
    if (!rwlock) return EINVAL;
    if (rwlock->readers != 0 || rwlock->writer != 0) return EBUSY;
    memset(rwlock, 0, sizeof(*rwlock));
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
    if (!rwlock) return EINVAL;
    for (;;) {
        if (rwlock->writer == 0 && rwlock->pending_writers == 0) {
            rwlock->readers++;
            return 0;
        }
        syscall6(SYSCALL_WAITABLE_WAIT,
                 (long)(uintptr_t)rwlock->__wait_rd, 0, 0, 0, 0, 0);
    }
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock) {
    if (!rwlock) return EINVAL;
    if (rwlock->writer == 0 && rwlock->pending_writers == 0) {
        rwlock->readers++;
        return 0;
    }
    return EBUSY;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
    if (!rwlock) return EINVAL;
    rwlock->pending_writers++;
    for (;;) {
        if (rwlock->readers == 0 && rwlock->writer == 0) {
            rwlock->writer = 1;
            rwlock->writer_owner = (pid_t)pthread_self();
            rwlock->pending_writers--;
            return 0;
        }
        syscall6(SYSCALL_WAITABLE_WAIT,
                 (long)(uintptr_t)rwlock->__wait_wr, 0, 0, 0, 0, 0);
    }
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
    if (!rwlock) return EINVAL;
    if (rwlock->readers == 0 && rwlock->writer == 0) {
        rwlock->writer = 1;
        rwlock->writer_owner = (pid_t)pthread_self();
        return 0;
    }
    return EBUSY;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
    if (!rwlock) return EINVAL;
    if (rwlock->writer) {
        rwlock->writer = 0;
        rwlock->writer_owner = 0;
        syscall6(SYSCALL_WAITABLE_WAKE,
                 (long)(uintptr_t)rwlock->__wait_wr, 1, 0, 0, 0, 0);
        syscall6(SYSCALL_WAITABLE_WAKE,
                 (long)(uintptr_t)rwlock->__wait_rd, 0x7FFFFFFF, 0, 0, 0, 0);
    } else if (rwlock->readers > 0) {
        rwlock->readers--;
        if (rwlock->readers == 0) {
            syscall6(SYSCALL_WAITABLE_WAKE,
                     (long)(uintptr_t)rwlock->__wait_wr, 1, 0, 0, 0, 0);
        }
    }
    return 0;
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock, const struct timespec *ts) {
    (void)ts;
    return pthread_rwlock_rdlock(rwlock);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock, const struct timespec *ts) {
    (void)ts;
    return pthread_rwlock_wrlock(rwlock);
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr) {
    if (!attr) return EINVAL;
    *attr = calloc(1, sizeof(struct pthread_rwlockattr));
    return *attr ? 0 : ENOMEM;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr) {
    if (!attr || !*attr) return EINVAL;
    free(*attr);
    *attr = NULL;
    return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr, int *pshared) {
    if (!attr || !*attr || !pshared) return EINVAL;
    *pshared = (*attr)->pshared;
    return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr, int pshared) {
    if (!attr || !*attr) return EINVAL;
    (*attr)->pshared = pshared;
    return 0;
}

int pthread_barrier_init(pthread_barrier_t *barrier,
    pthread_barrierattr_t *attr, unsigned int count) {
    (void)attr;
    if (!barrier || count == 0) return EINVAL;
    memset(barrier, 0, sizeof(*barrier));
    barrier->total = count;
    syscall6(SYSCALL_WAITABLE_INIT, (long)(uintptr_t)barrier->__wait_data,
             0, 0, 0, 0, 0);
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {
    if (!barrier) return EINVAL;
    memset(barrier, 0, sizeof(*barrier));
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
    if (!barrier) return EINVAL;
    barrier->count++;
    if (barrier->count >= barrier->total) {
        barrier->count = 0;
        syscall6(SYSCALL_WAITABLE_WAKE,
                 (long)(uintptr_t)barrier->__wait_data, 0x7FFFFFFF, 0, 0, 0, 0);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    syscall6(SYSCALL_WAITABLE_WAIT,
             (long)(uintptr_t)barrier->__wait_data, 0, 0, 0, 0, 0);
    return 0;
}

int pthread_barrierattr_init(pthread_barrierattr_t *attr) {
    if (!attr) return EINVAL;
    *attr = calloc(1, sizeof(struct pthread_barrierattr));
    return *attr ? 0 : ENOMEM;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *attr) {
    if (!attr || !*attr) return EINVAL;
    free(*attr);
    *attr = NULL;
    return 0;
}

int pthread_barrierattr_getpshared(pthread_barrierattr_t *attr, int *pshared) {
    if (!attr || !*attr || !pshared) return EINVAL;
    *pshared = (*attr)->pshared;
    return 0;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared) {
    if (!attr || !*attr) return EINVAL;
    (*attr)->pshared = pshared;
    return 0;
}

int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
    (void)pshared;
    if (!lock) return EINVAL;
    lock->lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock) {
    if (!lock) return EINVAL;
    return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
    if (!lock) return EINVAL;
    while (atomic_xchg(&lock->lock, 1) != 0) {
        while (lock->lock != 0) {
            __asm__ volatile("pause");
        }
    }
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
    if (!lock) return EINVAL;
    if (atomic_xchg(&lock->lock, 1) == 0) return 0;
    return EBUSY;
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
    if (!lock) return EINVAL;
    __asm__ volatile("" ::: "memory");
    lock->lock = 0;
    return 0;
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    unsigned int i;

    if (!key) return EINVAL;
    for (i = 0; i < PTHREAD_KEYS_MAX; i++) {
        unsigned int word = i / 32;
        unsigned int bit = i % 32;

        if ((g_key_used[word] & (1u << bit)) == 0) {
            g_key_used[word] |= (1u << bit);
            g_key_destructors[i] = destructor;
            *key = (pthread_key_t)i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key < 0 || (unsigned int)key >= PTHREAD_KEYS_MAX) return EINVAL;
    g_key_used[(unsigned int)key / 32] &= ~(1u << ((unsigned int)key % 32));
    g_key_destructors[(unsigned int)key] = 0;
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key < 0 || (unsigned int)key >= PTHREAD_KEYS_MAX) return EINVAL;
    syscall6(SYSCALL_THREAD_SETTLS, (long)key, (long)(uintptr_t)value,
             0, 0, 0, 0);
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key < 0 || (unsigned int)key >= PTHREAD_KEYS_MAX) return NULL;
    return (void *)(uintptr_t)syscall6(SYSCALL_THREAD_GETTLS, (long)key,
                                       0, 0, 0, 0, 0);
}

int pthread_once(pthread_once_t *once, void (*init_routine)(void)) {
    if (!once || !init_routine) return EINVAL;
    if (once->state == PTHREAD_DONE_INIT) return 0;
    pthread_mutex_lock(&once->mutex);
    if (once->state != PTHREAD_DONE_INIT) {
        init_routine();
        once->state = PTHREAD_DONE_INIT;
    }
    pthread_mutex_unlock(&once->mutex);
    return 0;
}

int pthread_kill(pthread_t thread, int sig) {
    (void)thread; (void)sig;
    return ENOSYS;
}

int pthread_cancel(pthread_t thread) {
    (void)thread;
    return ENOSYS;
}

int pthread_setcancelstate(int state, int *oldstate) {
    (void)state; (void)oldstate;
    return 0;
}

int pthread_setcanceltype(int type, int *oldtype) {
    (void)type; (void)oldtype;
    return 0;
}

void pthread_testcancel(void) {
}

int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void)) {
    (void)prepare; (void)parent; (void)child;
    return 0;
}

void pthread_cleanup_push(void (*routine)(void *), void *arg) {
    (void)routine; (void)arg;
}

void pthread_cleanup_pop(int execute) {
    (void)execute;
}

int pthread_getprio(pthread_t thread) {
    (void)thread;
    return 0;
}

int pthread_setprio(pthread_t thread, int prio) {
    (void)thread; (void)prio;
    return 0;
}

int pthread_getschedparam(pthread_t thread, int *policy,
                          struct sched_param *param) {
    (void)thread; (void)policy; (void)param;
    return ENOSYS;
}

int pthread_setschedparam(pthread_t thread, int policy,
                          const struct sched_param *param) {
    (void)thread; (void)policy; (void)param;
    return ENOSYS;
}

int pthread_getconcurrency(void) {
    return 0;
}

int pthread_setconcurrency(int level) {
    (void)level;
    return 0;
}

int pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inheritsched) {
    (void)attr; (void)inheritsched;
    return ENOSYS;
}

int pthread_attr_getschedparam(const pthread_attr_t *attr,
                               struct sched_param *param) {
    (void)attr; (void)param;
    return ENOSYS;
}

int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *policy) {
    (void)attr; (void)policy;
    return ENOSYS;
}

int pthread_attr_getscope(const pthread_attr_t *attr, int *scope) {
    (void)attr; (void)scope;
    return ENOSYS;
}

int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched) {
    (void)attr; (void)inheritsched;
    return ENOSYS;
}

int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param) {
    (void)attr; (void)param;
    return ENOSYS;
}

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy) {
    (void)attr; (void)policy;
    return ENOSYS;
}

int pthread_attr_setscope(pthread_attr_t *attr, int scope) {
    (void)attr; (void)scope;
    return ENOSYS;
}

int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id) {
    (void)thread; (void)clock_id;
    return ENOSYS;
}
