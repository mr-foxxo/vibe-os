#ifndef STDIO_H
#define STDIO_H
#include <sys/types.h>
typedef struct { int fd; int flags; unsigned char *buf; int bufsz; } FILE;
extern FILE *stdin, *stdout, *stderr;
extern int printf(const char *, ...);
extern int fprintf(FILE *, const char *, ...);
extern int sprintf(char *, const char *, ...);
extern int snprintf(char *, size_t, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, ...);
extern int vfprintf(FILE *, const char *, ...);
extern int putchar(int);
extern int puts(const char *);
extern int fputs(const char *, FILE *);
extern int fputc(int, FILE *);
extern int fgetc(FILE *);
extern char *fgets(char *, int, FILE *);
extern FILE *fopen(const char *, const char *);
extern int fclose(FILE *);
extern size_t fread(void *, size_t, size_t, FILE *);
extern size_t fwrite(const void *, size_t, size_t, FILE *);
extern int fseek(FILE *, long, int);
extern long ftell(FILE *);
extern void rewind(FILE *);
extern int feof(FILE *);
extern int ferror(FILE *);
extern void setbuf(FILE *, char *);
extern int remove(const char *);
extern int rename(const char *, const char *);
extern FILE *tmpfile(void);
extern void perror(const char *);
extern int vprintf(const char *, ...);
extern int vfscanf(FILE *, const char *, ...);
extern int fflush(FILE *);
extern void setlinebuf(FILE *);
extern char *tmpnam(char *);
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define NULL ((void*)0)
#define EOF (-1)
#define BUFSIZ 1024
#endif
