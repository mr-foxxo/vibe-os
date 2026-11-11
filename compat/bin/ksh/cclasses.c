#include "ksh_compat.h"

const char *sys_signame[NSIG] = {
    "EXIT", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP",
    "SIGABRT", "SIGEMT", "SIGFPE", "SIGKILL", "SIGBUS", "SIGSEGV",
    "SIGSYS", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGURG", "SIGSTOP",
    "SIGTSTP", "SIGCONT", "SIGCHLD", "SIGTTIN", "SIGTTOU", "SIGIO",
    "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGINFO",
    "SIGUSR1", "SIGUSR2"
};
const char *sys_siglist[NSIG] = {
    "Exit", "Hangup", "Interrupt", "Quit", "Illegal instruction",
    "Trace/BPT trap", "Abort trap", "EMT trap", "Floating point exception",
    "Killed", "Bus error", "Segmentation fault", "Bad system call",
    "Broken pipe", "Alarm clock", "Terminated", "Urgent I/O condition",
    "Suspended (signal)", "Suspended", "Continued", "Child exited",
    "Stopped (tty input)", "Stopped (tty output)", "I/O possible",
    "CPU time limit exceeded", "File size limit exceeded",
    "Virtual timer expired", "Profiling timer expired", "Window size changes",
    "Information request", "User defined signal 1", "User defined signal 2"
};

const struct cclass cclasses[] = {
    CCLASS("alpha", cclass_isalpha),
    CCLASS("upper", cclass_isupper),
    CCLASS("lower", cclass_islower),
    CCLASS("digit", cclass_isdigit),
    CCLASS("xdigit", cclass_isxdigit),
    CCLASS("alnum", cclass_isalnum),
    CCLASS("space", cclass_isspace),
    CCLASS("punct", cclass_ispunct),
    CCLASS("print", cclass_isprint),
    CCLASS("graph", cclass_isgraph),
    CCLASS("cntrl", cclass_iscntrl),
    CCLASS("blank", cclass_isblank),
    {NULL, NULL, 0}
};
