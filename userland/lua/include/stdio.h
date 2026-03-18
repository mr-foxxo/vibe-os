#ifndef VIBE_LUA_STDIO_H
#define VIBE_LUA_STDIO_H

#include <stdarg.h>
#include <lua_port.h>

#define BUFSIZ 256
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int sprintf(char *str, const char *fmt, ...);
int vsprintf(char *str, const char *fmt, va_list ap);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

int fputc(int c, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
void setbuf(FILE *stream, char *buf);
int fscanf(FILE *stream, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);

#endif // VIBE_LUA_STDIO_H
