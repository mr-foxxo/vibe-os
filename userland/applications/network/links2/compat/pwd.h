#ifndef PWD_H
#define PWD_H
#include <sys/types.h>
struct passwd {
	char *pw_name;
	char *pw_passwd;
	uid_t pw_uid;
	gid_t pw_gid;
	char *pw_gecos;
	char *pw_dir;
	char *pw_shell;
};
extern struct passwd *getpwuid(uid_t);
extern struct passwd *getpwnam(const char *);
#endif
