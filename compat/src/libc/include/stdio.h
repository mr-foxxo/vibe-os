#ifndef _COMPAT_STDIO_H
#define _COMPAT_STDIO_H

#include <sys/types.h>
#include <stdarg.h>

// A minimal FILE struct. For now, we'll just define it as an opaque pointer.
// We'll need to expand this if we need to support file I/O.
typedef void FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int puts(const char *s);
int putchar(int c);
int getchar(void);

// Basic I/O functions
int open(const char *pathname, int flags, ...);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int stat(const char *pathname, void *statbuf);
int fstat(int fd, void *statbuf);
int isatty(int fd);

// We'll need errno
extern int errno;


#endif /* _COMPAT_STDIO_H */
