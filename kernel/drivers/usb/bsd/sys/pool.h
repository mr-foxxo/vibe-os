#ifndef _SYS_POOL_H_
#define _SYS_POOL_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PR_NOWAIT 0x01
#define PR_ZERO 0x02

struct pool {
    const char *pr_name;
    size_t pr_size;
    int pr_align;
    int pr_nitems;
};

void pool_init(struct pool *pp, size_t sz, unsigned int align, int ipl, int flags,
    const char *name, void *p);
void *pool_get(struct pool *pp, int flags);
void pool_put(struct pool *pp, void *v);

#endif
