#ifndef STDLIB_H
#define STDLIB_H
#include <sys/types.h>
extern void *malloc(size_t);
extern void *calloc(size_t, size_t);
extern void *realloc(void *, size_t);
extern void free(void *);
extern int atoi(const char *);
extern long atol(const char *);
extern double atof(const char *);
extern long strtol(const char *, char **, int);
extern unsigned long strtoul(const char *, char **, int);
extern long long strtoll(const char *, char **, int);
extern unsigned long long strtoull(const char *, char **, int);
extern int rand(void);
extern void srand(unsigned);
extern void abort(void);
extern void exit(int);
extern int abs(int);
extern char *getenv(const char *);
extern int putenv(char *);
extern int setenv(const char *, const char *, int);
extern int unsetenv(const char *);
extern void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
extern void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
extern int mkstemp(char *);
extern char *mktemp(char *);
extern int system(const char *);
extern double strtod(const char *, char **);
extern long double strtold(const char *, char **);
extern int mbtowc(void *, const char *, size_t);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
#endif
