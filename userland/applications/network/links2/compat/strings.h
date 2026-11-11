#ifndef STRINGS_H
#define STRINGS_H
#include <string.h>
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, size_t);
extern void bzero(void *, size_t);
extern void bcopy(const void *, void *, size_t);
extern int bcmp(const void *, const void *, size_t);
extern char *index(const char *, int);
extern char *rindex(const char *, int);
#endif
