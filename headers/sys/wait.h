#ifndef VIBE_SYS_WAIT_H
#define VIBE_SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s) ((s) & 0x7f)
#define WIFEXITED(s) (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WIFSTOPPED(s) (((s) & 0xff) == 0x7f)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);

#endif
