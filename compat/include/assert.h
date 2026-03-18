#ifndef _COMPAT_ASSERT_H
#define _COMPAT_ASSERT_H

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) \
    ((expression) ? (void)0 : \
    __assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);

#endif /* _COMPAT_ASSERT_H */
