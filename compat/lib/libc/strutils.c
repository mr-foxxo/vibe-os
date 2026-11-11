#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

void *reallocarray(void *ptr, size_t nmemb, size_t size) {
    size_t total;
    if (__builtin_mul_overflow(nmemb, size, &total))
        return NULL;
    return realloc(ptr, total);
}

int mkstemp(char *template) {
    (void)template;
    return -1;
}
