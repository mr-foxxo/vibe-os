#ifndef SIGNAL_H
#define SIGNAL_H
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
typedef unsigned long sigset_t;
struct sigaction {
	void (*sa_handler)(int);
	sigset_t sa_mask;
	int sa_flags;
};
extern sighandler_t signal(int, sighandler_t);
extern int kill(int, int);
extern int raise(int);
extern int sigprocmask(int, const sigset_t *, sigset_t *);
extern int sigpending(sigset_t *);
extern int sigaction(int, const struct sigaction *, struct sigaction *);
extern int sigemptyset(sigset_t *);
extern int sigfillset(sigset_t *);
extern int sigaddset(sigset_t *, int);
extern int sigdelset(sigset_t *, int);
extern int sigismember(const sigset_t *, int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGEMT 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGBUS 10
#define SIGSEGV 11
#define SIGSYS 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGUSR1 16
#define SIGUSR2 17
#define SIGCHLD 18
#define SIGPWR 19
#define SIGWINCH 20
#define SIGURG 21
#define SIGSTOP 23
#define SIGTSTP 24
#define SIGCONT 25
#define SIGTTIN 26
#define SIGTTOU 27
#define SIGIO 29
#define SIGPROF 27
#define SIGXCPU 30
#define SIGXFSZ 31
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SA_RESTART 0x10000000
#define SA_NOCLDSTOP 1
#define SA_SIGINFO 4
#define NSIG 32
#endif
