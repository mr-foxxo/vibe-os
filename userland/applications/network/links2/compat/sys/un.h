#ifndef SYS_UN_H
#define SYS_UN_H
#include <sys/types.h>
struct sockaddr_un {
	sa_family_t sun_family;
	char sun_path[108];
};
#endif
