#ifndef SYS_SOCKET_H
#define SYS_SOCKET_H
#include <sys/types.h>
struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};
struct iovec {
	void *iov_base;
	size_t iov_len;
};
struct msghdr {
	void *msg_name;
	socklen_t msg_namelen;
	struct iovec *msg_iov;
	int msg_iovlen;
	void *msg_control;
	socklen_t msg_controllen;
	int msg_flags;
};
extern int socket(int, int, int);
extern int connect(int, const struct sockaddr *, socklen_t);
extern int bind(int, const struct sockaddr *, socklen_t);
extern int listen(int, int);
extern int accept(int, struct sockaddr *, socklen_t *);
extern ssize_t send(int, const void *, size_t, int);
extern ssize_t recv(int, void *, size_t, int);
extern ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
extern ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
extern int setsockopt(int, int, int, const void *, socklen_t);
extern int getsockopt(int, int, int, void *, socklen_t *);
extern int shutdown(int, int);
extern int getpeername(int, struct sockaddr *, socklen_t *);
extern int getsockname(int, struct sockaddr *, socklen_t *);
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 8
#define SO_LINGER 13
#define SO_SNDTIMEO 21
#define SO_RCVTIMEO 20
#define SO_BROADCAST 6
#define SO_ERROR 4
#endif
