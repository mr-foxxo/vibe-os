#ifndef _SYS_SEM_H
#define _SYS_SEM_H
#include <sys/ipc.h>
#include <sys/types.h>
struct semid_ds { struct ipc_perm sem_perm; unsigned short sem_nsems; time_t sem_otime; time_t sem_ctime; };
struct sembuf { unsigned short sem_num; short sem_op; short sem_flg; };
extern int semget(key_t key, int nsems, int semflg);
extern int semop(int semid, struct sembuf *sops, size_t nsops);
extern int semctl(int semid, int semnum, int cmd, ...);
#define GETNCNT 3
#define GETPID 4
#define GETVAL 5
#define GETALL 6
#define GETZCNT 7
#define SETVAL 8
#define SETALL 9
#define SEM_UNDO 010000
#endif
