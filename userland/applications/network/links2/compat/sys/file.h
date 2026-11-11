#ifndef SYS_FILE_H
#define SYS_FILE_H
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
extern int flock(int, int);
#endif
