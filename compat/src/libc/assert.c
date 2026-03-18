#include <compat_defs.h>
#include "../include/compat/libc/assert.h"
#include "../include/compat/libc/stdio.h"
#include "../include/compat/libc/stdlib.h"

void __assert_fail(const char *assertion, const char *file, int line, const char *function) {
    printf("Assertion failed: %s (%s: %s: %d)\n", assertion, file, function, line);
    abort();
}
