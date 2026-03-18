/*
 * Standard definitions - VibeOS minimal implementation
 */

#ifndef _STDDEF_H_
#define _STDDEF_H_

#include <stdint.h>

typedef int ptrdiff_t;
typedef unsigned int size_t;
typedef int wchar_t;
typedef unsigned int wint_t;
typedef int mbstate_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef offsetof
#define offsetof(t, m) ((size_t)&((t *)0)->m)
#endif

#endif /* _STDDEF_H_ */
