#ifndef SYS_WAIT_H
#define SYS_WAIT_H
#include <sys/types.h>
extern pid_t waitpid(pid_t, int *, int);
#define WNOHANG 1
#define WUNTRACED 2
#define WIFEXITED(s) (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) > 0 && ((s) & 0x7f) < 0x7f)
#define WTERMSIG(s) ((s) & 0x7f)
#define WIFSTOPPED(s) (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s) (((s) >> 8) & 0xff)
#endif
