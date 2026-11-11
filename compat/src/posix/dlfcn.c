#include <compat/posix/errno.h>
#include <dlfcn.h>

void *dlopen(const char *filename, int flags) {
    (void)filename;
    (void)flags;
    errno = ENOSYS;
    return 0;
}

int dlclose(void *handle) {
    (void)handle;
    return 0;
}

void *dlsym(void *handle, const char *symbol) {
    (void)handle;
    (void)symbol;
    return 0;
}

char *dlerror(void) {
    return "Dynamic linking not supported on VibeOS";
}
