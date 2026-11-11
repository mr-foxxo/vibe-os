#ifndef _COMPAT_ICONV_H
#define _COMPAT_ICONV_H
#include <sys/types.h>
typedef void *iconv_t;
extern iconv_t iconv_open(const char *tocode, const char *fromcode);
extern size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);
extern int iconv_close(iconv_t cd);
#define ICONV_TRIVIALP 0
#endif
