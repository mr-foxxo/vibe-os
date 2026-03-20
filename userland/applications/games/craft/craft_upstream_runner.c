#include <stdlib.h>
#include <string.h>
#include <userland/modules/include/utils.h>

float floorf(float x);
float roundf(float x);
float powf(float x, float y);
float cosf(float x);
float sinf(float x);
float atan2f(float y, float x);
float sqrtf(float x);
void *calloc(size_t count, size_t size);
char *strncat(char *dest, const char *src, size_t n);
void srand(unsigned int seed);
int rand(void);
int atoi(const char *nptr);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#define main craft_upstream_main
#include "upstream/src/main.c"
#undef main

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

int craft_upstream_run(void) {
    char *argv[] = {"craft", 0};
    return craft_upstream_main(1, argv);
}
