#ifndef DIRENT_H
#define DIRENT_H
#include <sys/types.h>
struct dirent {
	ino_t d_ino;
	char d_name[256];
};
typedef struct DIR DIR;
extern DIR *opendir(const char *);
extern struct dirent *readdir(DIR *);
extern int closedir(DIR *);
extern void rewinddir(DIR *);
#define NAME_MAX 255
#endif
