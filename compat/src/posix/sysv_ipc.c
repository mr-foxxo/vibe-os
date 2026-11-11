#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <compat/posix/errno.h>

key_t ftok(const char *path, int id) {
    struct stat st;
    if (stat(path, &st) != 0) return (key_t)-1;
    return (key_t)((st.st_ino & 0xFFFF) | ((st.st_dev & 0xFF) << 16) | ((id & 0xFF) << 24));
}

static int g_msg_id = 0;
int msgget(key_t key, int msgflg) {
    (void)key;
    (void)msgflg;
    return ++g_msg_id;
}

int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg) {
    (void)msqid;
    (void)msgp;
    (void)msgsz;
    (void)msgflg;
    return 0;
}

ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg) {
    (void)msqid;
    (void)msgp;
    (void)msgsz;
    (void)msgtyp;
    (void)msgflg;
    errno = ENOMSG;
    return -1;
}

int msgctl(int msqid, int cmd, struct msqid_ds *buf) {
    (void)msqid;
    (void)cmd;
    (void)buf;
    return 0;
}

static int g_sem_id = 0;
int semget(key_t key, int nsems, int semflg) {
    (void)key;
    (void)nsems;
    (void)semflg;
    return ++g_sem_id;
}

int semop(int semid, struct sembuf *sops, size_t nsops) {
    (void)semid;
    (void)sops;
    (void)nsops;
    return 0;
}

int semctl(int semid, int semnum, int cmd, ...) {
    (void)semid;
    (void)semnum;
    (void)cmd;
    return 0;
}

static int g_shm_id = 0;
int shmget(key_t key, size_t size, int shmflg) {
    (void)key;
    (void)size;
    (void)shmflg;
    return ++g_shm_id;
}

void *shmat(int shmid, const void *shmaddr, int shmflg) {
    (void)shmid;
    (void)shmaddr;
    (void)shmflg;
    errno = ENOSYS;
    return (void *)-1;
}

int shmdt(const void *shmaddr) {
    (void)shmaddr;
    return 0;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    (void)shmid;
    (void)cmd;
    (void)buf;
    return 0;
}
