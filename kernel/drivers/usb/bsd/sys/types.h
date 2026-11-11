#ifndef _SYS_TYPES_H_
#define _SYS_TYPES_H_

#include <stdint.h>
#include <stddef.h>

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
typedef int32_t int32_t;
typedef int64_t int64_t;
typedef uint32_t u_quad_t;
typedef int64_t quad_t;

typedef char *caddr_t;
typedef uint32_t daddr_t;
typedef uint32_t dev_t;
typedef uint32_t mode_t;
typedef uint32_t ino_t;
typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t off_t;
typedef int32_t ssize_t;
typedef uint32_t id_t;

typedef uint32_t register_t;

#define __BIT_TYPES_DEFINED__

#define __inline inline

typedef int clockid_t;
typedef int timer_t;

#define NBBY 8

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#endif
