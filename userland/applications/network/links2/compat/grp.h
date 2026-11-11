#ifndef GRP_H
#define GRP_H
#include <sys/types.h>
struct group {
	char *gr_name;
	char *gr_passwd;
	gid_t gr_gid;
	char **gr_mem;
};
extern struct group *getgrgid(gid_t);
extern struct group *getgrnam(const char *);
#endif
