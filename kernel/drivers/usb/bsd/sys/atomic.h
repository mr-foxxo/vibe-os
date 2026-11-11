#ifndef _SYS_ATOMIC_H_
#define _SYS_ATOMIC_H_

#include <stdint.h>

static inline uint32_t atomic_swap_uint(volatile uint32_t *p, uint32_t v) {
    __asm__ volatile("xchgl %0, %1" : "+r"(v), "+m"(*p) : : "memory");
    return v;
}

static inline uint32_t atomic_cas_uint(volatile uint32_t *p, uint32_t o, uint32_t n) {
    uint32_t prev;
    __asm__ volatile("lock; cmpxchgl %2, %1"
        : "=a"(prev), "+m"(*p) : "r"(n), "0"(o) : "memory");
    return prev;
}

static inline void atomic_inc_uint(volatile uint32_t *p) {
    __asm__ volatile("lock; incl %0" : "+m"(*p) : : "memory");
}

static inline void atomic_dec_uint(volatile uint32_t *p) {
    __asm__ volatile("lock; decl %0" : "+m"(*p) : : "memory");
}

static inline void atomic_setbits_uint(volatile uint32_t *p, uint32_t v) {
    __asm__ volatile("lock; orl %1, %0" : "+m"(*p) : "r"(v) : "memory");
}

static inline void atomic_clearbits_uint(volatile uint32_t *p, uint32_t v) {
    __asm__ volatile("lock; andl %1, %0" : "+m"(*p) : "r"(~v) : "memory");
}

static inline int atomic_add_int(volatile uint32_t *p, int v) {
    __asm__ volatile("lock; xaddl %0, %1"
        : "+r"(v), "+m"(*p) : : "memory");
    return v;
}

static inline int atomic_dec_int_nv(volatile uint32_t *p) {
    return __sync_sub_and_fetch(p, 1);
}

static inline int atomic_inc_int_nv(volatile uint32_t *p) {
    return __sync_add_and_fetch(p, 1);
}

#define membar_enter() __asm__ volatile("" ::: "memory")
#define membar_exit() __asm__ volatile("" ::: "memory")
#define membar_producer() __asm__ volatile("" ::: "memory")
#define membar_consumer() __asm__ volatile("" ::: "memory")
#define membar_sync() __asm__ volatile("mfence" ::: "memory")

#endif
