#ifndef _SYS_IOCTL_H_
#define _SYS_IOCTL_H_

#define _IOW(a,b,c) (((c) << 16) | ((b) << 8) | (a))
#define _IOR(a,b,c) (((c) << 16) | ((b) << 8) | (a) | 0x80000000)
#define _IO(a,b)    (((b) << 8) | (a))
#define _IOWR(a,b,c) _IOW(a,b,c)

#endif
