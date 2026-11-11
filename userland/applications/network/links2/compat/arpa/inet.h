#ifndef ARPA_INET_H
#define ARPA_INET_H
#include <sys/types.h>
#include <netinet/in.h>
extern unsigned int inet_addr(const char *);
extern char *inet_ntoa(struct in_addr);
extern int inet_aton(const char *, struct in_addr *);
extern const char *inet_ntop(int, const void *, char *, socklen_t);
extern int inet_pton(int, const char *, void *);
#endif
