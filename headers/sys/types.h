#ifndef SYS_TYPES_H
#define SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* POSIX types - only define if not already defined */
#ifndef _PID_T
#define _PID_T
typedef int pid_t;
#endif

#ifndef _UID_T
#define _UID_T
typedef unsigned int uid_t;
#endif

#ifndef _GID_T
#define _GID_T
typedef unsigned int gid_t;
#endif

#ifndef _OFF_T
#define _OFF_T
typedef long off_t;
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
typedef long ssize_t;
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#ifndef _MODE_T
#define _MODE_T
typedef int mode_t;
#endif

#ifndef _DEV_T
#define _DEV_T
typedef int dev_t;
#endif

#ifndef _INO_T
#define _INO_T
typedef int ino_t;
#endif

#ifndef _NLINK_T
#define _NLINK_T
typedef unsigned int nlink_t;
#endif

#ifndef _BLKSIZE_T
#define _BLKSIZE_T
typedef long blksize_t;
#endif

#ifndef _BLKCNT_T
#define _BLKCNT_T
typedef long blkcnt_t;
#endif

#ifndef _TIME_T
#define _TIME_T
typedef long time_t;
#endif

#ifndef _SUSECONDS_T
#define _SUSECONDS_T
typedef long suseconds_t;
#endif

#endif /* SYS_TYPES_H */
