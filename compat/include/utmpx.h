#ifndef _UTMPX_H_
#define _UTMPX_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <time.h>

#define EMPTY       0
#define RUN_LVL     1
#define BOOT_TIME   2
#define OLD_TIME    3
#define NEW_TIME    4
#define INIT_PROCESS 5
#define LOGIN_PROCESS 6
#define USER_PROCESS 7
#define DEAD_PROCESS 8
#define ACCOUNTING  9

#define UTX_LINESIZE 32
#define UTX_NAMESIZE 32
#define UTX_HOSTSIZE 256

struct utmpx {
    char ut_line[UTX_LINESIZE];
    char ut_id[4];
    char ut_user[UTX_NAMESIZE];
    char ut_host[UTX_HOSTSIZE];
    struct timeval ut_tv;
    pid_t ut_pid;
    short ut_type;
};

__BEGIN_DECLS
void setutxent(void);
void endutxent(void);
struct utmpx *getutxent(void);
struct utmpx *getutxid(const struct utmpx *id);
struct utmpx *getutxline(const struct utmpx *line);
struct utmpx *pututxline(const struct utmpx *utmpx);
__END_DECLS

#endif
