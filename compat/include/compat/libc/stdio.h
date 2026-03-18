/*
 * Standard I/O - VibeOS Compatibility
 * 
 * printf, fprintf, FILE operations
 * Implemented via VibeOS app_runtime syscalls
 */

#ifndef COMPAT_STDIO_H
#define COMPAT_STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define FILENAME_MAX 256
#define FOPEN_MAX 16

/* FILE structure - minimal */
typedef struct FILE {
    int fd;
    int flags;
    char *buf;
    size_t bufsize;
    size_t bufpos;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* Standard output functions */
int putchar(int c);
int puts(const char *s);

int printf(const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);

int vprintf(const char *fmt, va_list ap);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

/* Standard input */
int getchar(void);
char *gets(char *s);
char *fgets(char *s, int size, FILE *stream);

/* File operations */
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
int fflush(FILE *f);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);

int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);

int fgetc(FILE *stream);
int fputc(int c, FILE *stream);

int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

/* Lower-level file descriptor ops - essential for ported apps */
int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);

#endif /* COMPAT_STDIO_H */
