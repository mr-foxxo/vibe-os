#ifndef NETDB_H
#define NETDB_H
#include <sys/types.h>
#include <netinet/in.h>
struct hostent {
	char *h_name;
	char **h_aliases;
	int h_addrtype;
	int h_length;
	char **h_addr_list;
};
struct addrinfo {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};
extern struct hostent *gethostbyname(const char *);
extern struct hostent *gethostbyaddr(const void *, socklen_t, int);
extern int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
extern void freeaddrinfo(struct addrinfo *);
extern const char *gai_strerror(int);
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define AI_PASSIVE 1
#define AI_CANONNAME 2
#define AI_NUMERICHOST 4
#endif
