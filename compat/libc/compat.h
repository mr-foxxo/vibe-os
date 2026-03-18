#ifndef __COMPAT_H__
#define __COMPAT_H__

#include <stddef.h> // For size_t

// String functions
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *dest, const char *src);

#endif // __COMPAT_H__
