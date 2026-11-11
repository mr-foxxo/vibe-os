#ifndef _SYS_CONF_H_
#define _SYS_CONF_H_

#include <sys/types.h>

typedef int dev_t;
typedef int mode_t;

#define minor(x) ((x) & 0xFF)
#define major(x) (((x) >> 8) & 0xFF)
#define makedev(x,y) ((((x) & 0xFF) << 8) | ((y) & 0xFF))

#define FREAD 0x01
#define FWRITE 0x02

#endif
