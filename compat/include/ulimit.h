#ifndef _ULIMIT_H_
#define _ULIMIT_H_

#define UL_GETFSIZE 1
#define UL_SETFSIZE 2

long ulimit(int cmd, ...);

#endif
