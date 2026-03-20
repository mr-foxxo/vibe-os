#ifndef VIBEOS_CRAFT_UPSTREAM_COMPAT_H
#define VIBEOS_CRAFT_UPSTREAM_COMPAT_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
int atoi(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);

int rand(void);
void srand(unsigned int seed);

double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double floor(double x);
double ceil(double x);
double round(double x);
double fabs(double x);
double fmod(double x, double y);
double pow(double x, double y);

float sqrtf(float x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float acosf(float x);
float atan2f(float y, float x);
float floorf(float x);
float ceilf(float x);
float roundf(float x);
float fabsf(float x);
float fmodf(float x, float y);
float powf(float x, float y);

#endif
