#include <stdlib.h>

void *malloc(size_t size) {
    return malloc(size);
}

void free(void *ptr) {
    free(ptr);
}

void *realloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

void *calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}