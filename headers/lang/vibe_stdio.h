#ifndef VIBE_STDIO_H
#define VIBE_STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* Standard I/O Constants */
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define FILENAME_MAX 256
#define FOPEN_MAX 16

typedef unsigned int size_t;

/* FILE structure - minimal implementation */
typedef struct FILE {
    int fd;              /* file descriptor */
    int flags;           /* open mode flags */
    int error;           /* error indicator */
    long pos;            /* current position */
    unsigned char *buf;  /* buffer pointer */
    int bufsize;         /* buffer size */
    int bufpos;          /* current position in buffer */
} FILE;

/* Standard file handles */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* Standard I/O functions */
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);

int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

/* File operations */
FILE *fopen(const char *filename, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);

/* File I/O */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputs(const char *s, FILE *stream);
int puts(const char *s);

/* Error and status */
void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);

/* Positioning */
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);

int getchar(void);
int putchar(int c);
int getc(FILE *stream);
int putc(int c, FILE *stream);

#endif
