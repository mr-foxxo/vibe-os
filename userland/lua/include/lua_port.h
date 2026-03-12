#ifndef VIBE_LUA_PORT_H
#define VIBE_LUA_PORT_H

#include <stddef.h>
#include <stdint.h>

typedef long time_t;
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);

typedef struct FILE {
    int node;
    int pos;
    int error;
    int eof;
    int owned;
} FILE;

struct lconv {
    char *decimal_point;
};

extern int errno;
extern FILE *stdin;

#define EOF (-1)
#define LC_ALL 0
#define SIGINT 2
#define SIG_DFL ((sighandler_t)0)

void vibe_lua_console_write_len(const char *s, size_t len);
void vibe_lua_console_writef(const char *fmt, const char *arg);
int vibe_lua_number_to_string(char *buf, size_t size, double n);
int vibe_lua_integer_to_string(char *buf, size_t size, long long n);
int vibe_lua_pointer_to_string(char *buf, size_t size, const void *p);
double vibe_lua_str2number(const char *s, char **endptr);

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
int abs(int value);
__attribute__((noreturn)) void abort(void);
char *getenv(const char *name);
double strtod(const char *s, char **endptr);

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strstr(const char *haystack, const char *needle);
int strcoll(const char *a, const char *b);
char *strerror(int errnum);

int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isxdigit(int c);
int toupper(int c);
int tolower(int c);

double floor(double x);
double ceil(double x);
double fabs(double x);
double fmod(double x, double y);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double pow(double x, double y);

struct lconv *localeconv(void);
char *setlocale(int category, const char *locale);
time_t time(time_t *out);
sighandler_t signal(int signum, sighandler_t handler);

FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
int getc(FILE *stream);
int fclose(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

#endif // VIBE_LUA_PORT_H
