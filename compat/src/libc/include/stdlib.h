#ifndef _COMPAT_STDLIB_H
#define _COMPAT_STDLIB_H

#include <sys/types.h>

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);
void exit(int status);
void abort(void);

#endif /* _COMPAT_STDLIB_H */
