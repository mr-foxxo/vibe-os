/*
 * Standard Library - VibeOS Compatibility
 */

#ifndef COMPAT_STDLIB_H
#define COMPAT_STDLIB_H

#include <stddef.h>

/* Memory */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

/* Program control */
__attribute__((noreturn)) void exit(int status);
__attribute__((noreturn)) void abort(void);

/* Conversion */
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);

double atof(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);

/* Utilities */
int abs(int j);
long labs(long j);
long long llabs(long long j);

/* Sorting */
void qsort(void *base, size_t nmemb, size_t size, 
           int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base,
              size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

/* Random (basic) */
int rand(void);
void srand(unsigned int seed);

/* Environment */
char *getenv(const char *name);

#endif /* COMPAT_STDLIB_H */
