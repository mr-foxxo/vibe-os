#ifndef _SYS_SHM_H
#define _SYS_SHM_H
#include <sys/ipc.h>
#include <sys/types.h>
struct shmid_ds { struct ipc_perm shm_perm; size_t shm_segsz; pid_t shm_lpid; pid_t shm_cpid; time_t shm_atime; time_t shm_dtime; time_t shm_ctime; unsigned short shm_nattch; };
extern int shmget(key_t key, size_t size, int shmflg);
extern void *shmat(int shmid, const void *shmaddr, int shmflg);
extern int shmdt(const void *shmaddr);
extern int shmctl(int shmid, int cmd, struct shmid_ds *buf);
#define SHM_RDONLY 010000
#define SHM_RND 020000
#define SHMLBA 4096
#endif
