#ifndef _MONETARY_H_
#define _MONETARY_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdarg.h>

__BEGIN_DECLS
ssize_t strfmon(char *s, size_t max, const char *format, ...);
__END_DECLS

#endif
