#ifndef _COMPAT_DEFS_H_
#define _COMPAT_DEFS_H_

#include <stdint.h>

typedef unsigned int size_t;
typedef long ssize_t;
typedef long int off_t;
typedef int pid_t;
typedef int mode_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef int ino_t;
typedef int dev_t;
typedef long long quad_t;
typedef unsigned long long u_quad_t;
typedef long int time_t;
typedef long int suseconds_t;
typedef long int clock_t;
typedef int clockid_t;
typedef char *__va_list;
typedef int __wchar_t;

typedef unsigned int __size_t;
typedef long int __time_t;
typedef int __clockid_t;
typedef long int __clock_t;
typedef int __pid_t;
typedef int __timer_t;
typedef int id_t;
typedef unsigned char u_char;
typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;

#endif /* _COMPAT_DEFS_H_ */
