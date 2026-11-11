#ifndef _SYS_WAIT_H_
#define _SYS_WAIT_H_

#include <compat_defs.h>

#define WNOHANG     1
#define WUNTRACED   2

#define _WSTATUS(x) ((x) & 0x7f)
#define _WSTOPPED   0x7f

#define WIFEXITED(x)    (_WSTATUS(x) == 0)
#define WEXITSTATUS(x)  ((x) >> 8)
#define WIFSIGNALED(x)  (_WSTATUS(x) > 0 && _WSTATUS(x) < 0x7f)
#define WTERMSIG(x)     (_WSTATUS(x))
#define WIFSTOPPED(x)   (_WSTATUS(x) == _WSTOPPED && ((x) >> 8) != 0)
#define WSTOPSIG(x)     ((x) >> 8)
#define WIFCONTINUED(x) ((x) == 0xffff)
#define WCOREDUMP(x)    ((x) & 0x80)

pid_t waitpid(pid_t pid, int *stat_loc, int options);
pid_t wait(int *stat_loc);

#endif
