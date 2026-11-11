#ifndef POLL_H
#define POLL_H
struct pollfd {
	int fd;
	short events;
	short revents;
};
typedef unsigned int nfds_t;
extern int poll(struct pollfd *, nfds_t, int);
#define POLLIN 1
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32
#endif
