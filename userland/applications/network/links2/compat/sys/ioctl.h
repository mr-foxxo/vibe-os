#ifndef SYS_IOCTL_H
#define SYS_IOCTL_H
extern int ioctl(int, unsigned long, ...);
#define TIOCGWINSZ 0x5413
#define TIOCGETA 0x5401
#define TIOCSETA 0x5402
#define FIONREAD 0x541B
#endif
