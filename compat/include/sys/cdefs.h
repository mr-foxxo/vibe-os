/*
 * System common definitions - VibeOS minimal implementation
 */

#ifndef _SYS_CDEFS_H_
#define _SYS_CDEFS_H_

/* Empty for now - minimal POSIX compatibility */
#define __BEGIN_DECLS
#define __END_DECLS
#define __P(args) args

#if !defined(__dead)
#define __dead __attribute__((__noreturn__))
#endif

#if !defined(__packed)
#define __packed __attribute__((__packed__))
#endif

#if !defined(__aligned)
#define __aligned(x) __attribute__((__aligned__(x)))
#endif

#if !defined(__unused)
#define __unused __attribute__((__unused__))
#endif

#if !defined(__extern_inline)
#define __extern_inline extern inline
#endif

#if !defined(__predict_true)
#define __predict_true(exp) __builtin_expect(((exp) != 0), 1)
#endif

#if !defined(__predict_false)
#define __predict_false(exp) __builtin_expect(((exp) != 0), 0)
#endif

#if !defined(__nonnull)
#define __nonnull(x) __attribute__((__nonnull__ x))
#endif

#if !defined(__deprecated)
#define __deprecated __attribute__((__deprecated__))
#endif

#if !defined(__restrict)
#define __restrict restrict
#endif

#if !defined(__printflike)
#define __printflike(x, y) __attribute__((__format__(printf, x, y)))
#endif

#if !defined(__scanflike)
#define __scanflike(x, y) __attribute__((__format__(scanf, x, y)))
#endif

#if !defined(__bounded__)
#define __bounded__(x, y, z) 
#endif

/*
 * C99 variable-length array(VLA) support, required for some headers.
 * Many old libraries still use VLAs.
 * VibeOS does not support VLAs yet in the kernel, but some apps may use them.
 */
#if !defined(__vla)
#if __STDC_VERSION__ >= 199901L
#define __vla(type, name, size) type name[size]
#else
#define __vla(type, name, size) type name[1]
#endif
#endif

#if !defined(_DIAGASSERT)
#define _DIAGASSERT(x)
#endif

#ifndef __VA_LIST_DEFINED
#define __VA_LIST_DEFINED
typedef char *__va_list;
#endif

#ifndef __WCHAR_T_DEFINED
#define __WCHAR_T_DEFINED
typedef int __wchar_t;
#endif



#endif /* _SYS_CDEFS_H_ */
