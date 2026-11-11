#ifndef VIBE_SYS_RESOURCE_H
#define VIBE_SYS_RESOURCE_H

#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_RSS 5
#define RLIMIT_NPROC 6
#define RLIMIT_NOFILE 7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS 9
#define RLIMIT_NLIMITS 10

#define PRIO_PROCESS 0
#define PRIO_PGRP 1
#define PRIO_USER 2

struct rlimit {
    long rlim_cur;
    long rlim_max;
};

#endif
