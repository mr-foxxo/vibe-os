#ifndef SYS_MMAN_H
#define SYS_MMAN_H
#include <sys/types.h>
extern void *mmap(void *, size_t, int, int, int, off_t);
extern int munmap(void *, size_t);
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FAILED ((void *)-1)
#endif
