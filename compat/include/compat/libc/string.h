/*
 * String Functions - VibeOS Compatibility
 */

#ifndef COMPAT_STRING_H
#define COMPAT_STRING_H

#include <stddef.h>

/* Memory operations */
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

/* String operations */
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strcasestr(const char *haystack, const char *needle);

char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);

size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

/* String duplication */
char *strdup(const char *s);
char *strndup(const char *s, size_t n);

#endif /* COMPAT_STRING_H */
