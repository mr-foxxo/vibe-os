#ifndef _SYS_DIRENT_H_
#define _SYS_DIRENT_H_

#include <compat_defs.h>

#define MAXNAMLEN 255

struct dirent {
    long d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[MAXNAMLEN + 1];
};

#endif
