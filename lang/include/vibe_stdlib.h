#ifndef VIBE_STDLIB_H
#define VIBE_STDLIB_H

typedef unsigned int size_t;

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

int abs(int j);
long labs(long j);
long long llabs(long long j);

int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);

void exit(int status);
void abort(void);
int atexit(void (*func)(void));

unsigned int sleep(unsigned int seconds);
int usleep(unsigned int microseconds);

/* Random number generator stubs */
int rand(void);
void srand(unsigned int seed);

/* Type conversions */
double atof(const char *nptr);
double strtod(const char *nptr, char **endptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);

#endif
