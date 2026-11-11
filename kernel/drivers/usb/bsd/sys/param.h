#ifndef _SYS_PARAM_H_
#define _SYS_PARAM_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/time.h>
#include <sys/errno.h>

#define DIAGNOSTIC

#define __packed __attribute__((packed))
#define __dead __attribute__((noreturn))
#define __BEGIN_DECLS
#define __END_DECLS
#define __unused __attribute__((unused))
#define __const const

#define NAME "OpenBSD"
#define OSNAME "OpenBSD"
#define MACHINE "i386"
#define MACHINE_ARCH "i386"
#define MID_MACHINE 0

#define NBBY 8
#define PAGE_SIZE 4096
#define PAGE_MASK 4095
#define PAGE_SHIFT 12

#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define rounddown(x, y) ((x) - ((x) % (y)))
#define nitems(x) (sizeof((x)) / sizeof((x)[0]))
#define powerof2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define MAXPATHLEN 1024
#define MAXHOSTNAMELEN 256
#define MAXLOGNAME 32
#define NGROUPS 16
#define NOGROUP 65535

#define hz 100

#define DEV_BSHIFT 9
#define DEV_BSIZE (1 << DEV_BSHIFT)
#define BLKDEV_IOSIZE 2048

#define PRIBIO 0
#define PWAIT 0
#define PZERO 0
#define PCATCH 0x10
#define INFSLP 0x7fffffff

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

#define IPL_BIO 1
#define IPL_SOFTNET 3
#define PRIMASK 0
#define splraise(x) 0

#endif
