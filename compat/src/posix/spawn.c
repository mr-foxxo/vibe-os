#ifndef __BEGIN_DECLS
#define __BEGIN_DECLS
#endif
#ifndef __END_DECLS
#define __END_DECLS
#endif
#ifndef __restrict
#define __restrict
#endif
#ifndef __dead
#define __dead
#endif

#include <spawn.h>
#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>

extern pid_t fork(void);
extern int execve(const char *path, char *const argv[], char *const envp[]);
#include <compat/posix/fcntl.h>
#include <compat/libc/string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/wait.h>

#define MAX_FILE_ACTIONS 16

struct __posix_spawn_file_actions {
    int used;
    int actions[MAX_FILE_ACTIONS][4];
    int count;
};

struct __posix_spawnattr {
    short flags;
    pid_t pgroup;
    int sched_policy;
    struct sched_param sched_param;
    sigset_t sigdefault;
    sigset_t sigmask;
};

static int compat_spawn_do_exec(const char *path, char *const argv[], char *const envp[],
                         const posix_spawn_file_actions_t *file_actions,
                         const posix_spawnattr_t *attr) {
    int i;

    if (file_actions && *file_actions) {
        struct __posix_spawn_file_actions *fa = *file_actions;
        for (i = 0; i < fa->count; i++) {
            int op = fa->actions[i][0];
            int fd = fa->actions[i][1];
            switch (op) {
            case 1:
                dup2(fd, fa->actions[i][2]);
                break;
            case 2:
                close(fd);
                break;
            case 3:
                close(fd);
                open((const char *)(uintptr_t)fa->actions[i][2], fa->actions[i][3]);
                dup2(fd, fd);
                break;
            }
        }
    }
    (void)attr;
    execve(path, argv, envp);
    _exit(127);
    return -1;
}

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions) {
    if (!file_actions) return EINVAL;
    *file_actions = calloc(1, sizeof(struct __posix_spawn_file_actions));
    if (!*file_actions) return ENOMEM;
    (*file_actions)->used = 1;
    (*file_actions)->count = 0;
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions) {
    if (!file_actions || !*file_actions) return EINVAL;
    free(*file_actions);
    *file_actions = 0;
    return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *file_actions, int fd, int newfd) {
    struct __posix_spawn_file_actions *fa;
    if (!file_actions || !*file_actions) return EINVAL;
    fa = *file_actions;
    if (fa->count >= MAX_FILE_ACTIONS) return ENOMEM;
    fa->actions[fa->count][0] = 1;
    fa->actions[fa->count][1] = fd;
    fa->actions[fa->count][2] = newfd;
    fa->actions[fa->count][3] = 0;
    fa->count++;
    return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *file_actions, int fd) {
    struct __posix_spawn_file_actions *fa;
    if (!file_actions || !*file_actions) return EINVAL;
    fa = *file_actions;
    if (fa->count >= MAX_FILE_ACTIONS) return ENOMEM;
    fa->actions[fa->count][0] = 2;
    fa->actions[fa->count][1] = fd;
    fa->actions[fa->count][2] = 0;
    fa->actions[fa->count][3] = 0;
    fa->count++;
    return 0;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *file_actions,
    int fd, const char *path, int oflag, mode_t mode) {
    struct __posix_spawn_file_actions *fa;
    if (!file_actions || !*file_actions) return EINVAL;
    fa = *file_actions;
    if (fa->count >= MAX_FILE_ACTIONS) return ENOMEM;
    fa->actions[fa->count][0] = 3;
    fa->actions[fa->count][1] = fd;
    fa->actions[fa->count][2] = (int)(uintptr_t)path;
    fa->actions[fa->count][3] = oflag;
    fa->count++;
    (void)mode;
    return 0;
}

int posix_spawnattr_init(posix_spawnattr_t *attr) {
    if (!attr) return EINVAL;
    *attr = calloc(1, sizeof(struct __posix_spawnattr));
    if (!*attr) return ENOMEM;
    (*attr)->flags = 0;
    (*attr)->pgroup = 0;
    return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *attr) {
    if (!attr || !*attr) return EINVAL;
    free(*attr);
    *attr = 0;
    return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t *attr, short *flags) {
    if (!attr || !*attr || !flags) return EINVAL;
    *flags = (*attr)->flags;
    return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags) {
    if (!attr || !*attr) return EINVAL;
    (*attr)->flags = flags;
    return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *attr, pid_t *pgroup) {
    if (!attr || !*attr || !pgroup) return EINVAL;
    *pgroup = (*attr)->pgroup;
    return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup) {
    if (!attr || !*attr) return EINVAL;
    (*attr)->pgroup = pgroup;
    return 0;
}

int posix_spawnattr_getschedparam(const posix_spawnattr_t *attr, struct sched_param *param) {
    if (!attr || !*attr || !param) return EINVAL;
    *param = (*attr)->sched_param;
    return 0;
}

int posix_spawnattr_setschedparam(posix_spawnattr_t *attr, const struct sched_param *param) {
    if (!attr || !*attr || !param) return EINVAL;
    (*attr)->sched_param = *param;
    return 0;
}

int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *attr, int *policy) {
    if (!attr || !*attr || !policy) return EINVAL;
    *policy = (*attr)->sched_policy;
    return 0;
}

int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy) {
    if (!attr || !*attr) return EINVAL;
    (*attr)->sched_policy = policy;
    return 0;
}

int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attr, sigset_t *sigdefault) {
    if (!attr || !*attr || !sigdefault) return EINVAL;
    *sigdefault = (*attr)->sigdefault;
    return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t *attr, const sigset_t *sigdefault) {
    if (!attr || !*attr || !sigdefault) return EINVAL;
    (*attr)->sigdefault = *sigdefault;
    return 0;
}

int posix_spawnattr_getsigmask(const posix_spawnattr_t *attr, sigset_t *sigmask) {
    if (!attr || !*attr || !sigmask) return EINVAL;
    *sigmask = (*attr)->sigmask;
    return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t *attr, const sigset_t *sigmask) {
    if (!attr || !*attr || !sigmask) return EINVAL;
    (*attr)->sigmask = *sigmask;
    return 0;
}

int posix_spawn(pid_t *pid, const char *path,
    const posix_spawn_file_actions_t *file_actions,
    const posix_spawnattr_t *attr,
    char *const argv[], char *const envp[]) {
    pid_t child;

    if (!pid || !path) {
        errno = EINVAL;
        return -1;
    }

    child = fork();
    if (child < 0) {
        return errno;
    }
    if (child == 0) {
        compat_spawn_do_exec(path, argv, envp, file_actions, attr);
    }
    *pid = child;
    return 0;
}

int posix_spawnp(pid_t *pid, const char *file,
    const posix_spawn_file_actions_t *file_actions,
    const posix_spawnattr_t *attr,
    char *const argv[], char *const envp[]) {
    char *path;
    char *p;
    char *saveptr;
    pid_t child;

    if (!pid || !file) {
        errno = EINVAL;
        return -1;
    }

    if (strchr(file, '/') != 0) {
        return posix_spawn(pid, file, file_actions, attr, argv, envp);
    }

    path = getenv("PATH");
    if (!path) {
        path = "/bin:/usr/bin";
    }
    p = strtok_r(path, ":", &saveptr);
    while (p) {
        char full[256];
        size_t plen = strlen(p);
        size_t flen = strlen(file);
        if (plen + 1 + flen + 1 > sizeof(full)) {
            p = strtok_r(0, ":", &saveptr);
            continue;
        }
        memcpy(full, p, plen);
        full[plen] = '/';
        memcpy(full + plen + 1, file, flen + 1);
        child = fork();
        if (child < 0) {
            return errno;
        }
        if (child == 0) {
            compat_spawn_do_exec(full, argv, envp, file_actions, attr);
        }
        *pid = child;
        return 0;
    }
    return ENOENT;
}
