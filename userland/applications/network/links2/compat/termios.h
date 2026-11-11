#ifndef TERMIOS_H
#define TERMIOS_H
#include <sys/types.h>
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;
#define NCCS 32
struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[NCCS];
};
extern int tcgetattr(int, struct termios *);
extern int tcsetattr(int, int, const struct termios *);
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define ECHO 0000010
#define ICANON 0000002
#define ISIG 0000001
#define ICRNL 0000400
#define OPOST 0000001
#define ONLCR 0000004
#define VMIN 6
#define VTIME 5
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VEOL 11
#endif
