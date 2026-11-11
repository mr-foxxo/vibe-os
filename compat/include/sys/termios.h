#ifndef COMPAT_SYS_TERMIOS_H
#define COMPAT_SYS_TERMIOS_H

#include <compat/posix/errno.h>
#include <compat_defs.h>

#define VEOF    0
#define VEOL    1
#define VEOL2   2
#define VERASE  3
#define VWERASE 4
#define VKILL   5
#define VREPRINT 6
#define VINTR   8
#define VQUIT   9
#define VSUSP   10
#define VDSUSP  11
#define VSTART  12
#define VSTOP   13
#define VLNEXT  14
#define VDISCARD 15
#define VMIN    16
#define VTIME   17
#define VSTATUS 18
#define NCCS    20

#define _POSIX_VDISABLE 0377

#define IGNBRK  0x00000001U
#define BRKINT  0x00000002U
#define IGNPAR  0x00000004U
#define PARMRK  0x00000008U
#define INPCK   0x00000010U
#define ISTRIP  0x00000020U
#define INLCR   0x00000040U
#define IGNCR   0x00000080U
#define ICRNL   0x00000100U
#define IXON    0x00000200U
#define IXOFF   0x00000400U
#define IXANY   0x00000800U

#define OPOST   0x00000001U
#define ONLCR   0x00000002U
#define TABDLY  0x00000004U
#define TAB0    0x00000000U
#define TAB3    0x00000004U
#define OXTABS  TAB3

#define CIGNORE 0x00000001U
#define CSIZE   0x00000300U
#define CS5     0x00000000U
#define CS6     0x00000100U
#define CS7     0x00000200U
#define CS8     0x00000300U
#define CSTOPB  0x00000400U
#define CREAD   0x00000800U
#define PARENB  0x00001000U
#define PARODD  0x00002000U
#define HUPCL   0x00004000U
#define CLOCAL  0x00008000U
#define CRTSCTS 0x00010000U

#define ECHOKE  0x00000001U
#define ECHOE   0x00000002U
#define ECHOK   0x00000004U
#define ECHO    0x00000008U
#define ECHONL  0x00000010U
#define ECHOPRT 0x00000020U
#define ECHOCTL 0x00000040U
#define ISIG    0x00000080U
#define ICANON  0x00000100U
#define ALTWERASE 0x00000200U
#define IEXTEN  0x00000400U
#define EXTPROC 0x00000800U
#define TOSTOP  0x00400000U
#define NOFLSH  0x80000000U

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
    int c_ispeed;
    int c_ospeed;
};

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define TCSASOFT  0x10

#define TCIFLUSH  1
#define TCOFLUSH  2
#define TCIOFLUSH 3
#define TCOOFF    1
#define TCOON     2
#define TCIOFF    3
#define TCION     4

#define B0 0
#define B50 50
#define B75 75
#define B110 110
#define B134 134
#define B150 150
#define B200 200
#define B300 300
#define B600 600
#define B1200 1200
#define B1800 1800
#define B2400 2400
#define B4800 4800
#define B9600 9600
#define B19200 19200
#define B38400 38400
#define B57600 57600
#define B115200 115200
#define EXTA B19200
#define EXTB B38400

speed_t cfgetispeed(const struct termios *t);
speed_t cfgetospeed(const struct termios *t);
int cfsetispeed(struct termios *t, speed_t speed);
int cfsetospeed(struct termios *t, speed_t speed);
int cfsetspeed(struct termios *t, speed_t speed);
void cfmakeraw(struct termios *t);

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int action, const struct termios *t);
int tcdrain(int fd);
int tcflow(int fd, int action);
int tcflush(int fd, int queue_selector);
int tcsendbreak(int fd, int duration);

#endif
