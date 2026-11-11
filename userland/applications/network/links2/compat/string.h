#ifndef STRING_H
#define STRING_H
#include <sys/types.h>
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern void *memmove(void *, const void *, size_t);
extern int memcmp(const void *, const void *, size_t);
extern void *memchr(const void *, int, size_t);
extern void *memrchr(const void *, int, size_t);
extern char *strcpy(char *, const char *);
extern char *strncpy(char *, const char *, size_t);
extern char *strcat(char *, const char *);
extern char *strncat(char *, const char *, size_t);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);
extern char *strchr(const char *, int);
extern char *strrchr(const char *, int);
extern char *strstr(const char *, const char *);
extern size_t strlen(const char *);
extern size_t strnlen(const char *, size_t);
extern char *strdup(const char *);
extern char *strndup(const char *, size_t);
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, size_t);
extern char *strtok(char *, const char *);
extern char *strtok_r(char *, const char *, char **);
extern char *strerror(int);
extern size_t strlcpy(char *, const char *, size_t);
extern size_t strlcat(char *, const char *, size_t);
extern size_t strcspn(const char *, const char *);
extern size_t strspn(const char *, const char *);
extern void *memmem(const void *, size_t, const void *, size_t);
#endif
