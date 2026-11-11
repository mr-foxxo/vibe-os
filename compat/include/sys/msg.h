#ifndef _SYS_MSG_H
#define _SYS_MSG_H
#include <sys/ipc.h>
#include <sys/types.h>
typedef unsigned long msgqnum_t;
typedef unsigned long msglen_t;
struct msqid_ds { struct ipc_perm msg_perm; msgqnum_t msg_qnum; msglen_t msg_qbytes; pid_t msg_lspid; pid_t msg_lrpid; time_t msg_stime; time_t msg_rtime; time_t msg_ctime; };
struct msgbuf { long mtype; char mtext[1]; };
extern int msgget(key_t key, int msgflg);
extern int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
extern ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
extern int msgctl(int msqid, int cmd, struct msqid_ds *buf);
#define MSG_NOERROR 010000
#endif
