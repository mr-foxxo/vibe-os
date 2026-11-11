#ifndef _SYS_RESOURCE_H_
#define _SYS_RESOURCE_H_

#include <sys/types.h>

typedef unsigned long rlim_t;

#define RLIMIT_CORE     4
#define RLIMIT_CPU      0
#define RLIMIT_DATA     2
#define RLIMIT_FSIZE    1
#define RLIMIT_MEMLOCK  6
#define RLIMIT_NOFILE   5
#define RLIMIT_NPROC    7
#define RLIMIT_RSS      5
#define RLIMIT_STACK    3
#define RLIMIT_AS       9
#define RLIM_INFINITY   (~0UL)
#define RLIMIT_NLIMITS  16

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#endif
