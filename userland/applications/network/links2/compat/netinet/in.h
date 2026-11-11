#ifndef NETINET_IN_H
#define NETINET_IN_H
#include <sys/types.h>
struct in_addr { unsigned int s_addr; };
struct sockaddr_in {
	sa_family_t sin_family;
	unsigned short sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};
struct in6_addr { unsigned char s6_addr[16]; };
struct sockaddr_in6 {
	sa_family_t sin6_family;
	unsigned short sin6_port;
	unsigned int sin6_flowinfo;
	struct in6_addr sin6_addr;
	unsigned int sin6_scope_id;
};
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define INADDR_ANY ((unsigned int)0)
#define INADDR_LOOPBACK 0x7f000001
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
extern unsigned short htons(unsigned short);
extern unsigned short ntohs(unsigned short);
extern unsigned int htonl(unsigned int);
extern unsigned int ntohl(unsigned int);
#endif
