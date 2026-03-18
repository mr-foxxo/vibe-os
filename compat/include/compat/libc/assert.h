/*
 * Assert Macro - VibeOS Compatibility
 */

#ifndef COMPAT_ASSERT_H
#define COMPAT_ASSERT_H

#ifndef NDEBUG
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#else
#define assert(expr) ((void)0)
#endif

__attribute__((noreturn)) void __assert_fail(
    const char *assertion,
    const char *file,
    int line,
    const char *func);

#endif /* COMPAT_ASSERT_H */
