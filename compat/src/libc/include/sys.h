#ifndef _COMPAT_SYS_H
#define _COMPAT_SYS_H

static inline int compat_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

#endif /* _COMPAT_SYS_H */
