#ifndef _SYS_IOCCOM_H_
#define _SYS_IOCCOM_H_

#define _IOC(inout, group, num, len) ((int)(((inout) << 30) | ((group) << 8) | (num) | ((len) << 16)))
#define _IO(g, n)        _IOC(0, g, n, 0)
#define _IOR(g, n, t)    _IOC(1, g, n, sizeof(t))
#define _IOW(g, n, t)    _IOC(0, g, n, sizeof(t))
#define _IOWR(g, n, t)   _IOC(1, g, n, sizeof(t))

#endif
