#include <iconv.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

struct _iconv_cd {
    int dummy;
};

iconv_t iconv_open(const char *tocode, const char *fromcode) {
    (void)tocode;
    (void)fromcode;
    return (iconv_t)1;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft) {
    (void)cd;
    if (!inbuf || !*inbuf || !outbuf || !*outbuf) return (size_t)-1;
    size_t n = *inbytesleft < *outbytesleft ? *inbytesleft : *outbytesleft;
    memcpy(*outbuf, *inbuf, n);
    *inbuf += n; *inbytesleft -= n;
    *outbuf += n; *outbytesleft -= n;
    return n;
}

int iconv_close(iconv_t cd) {
    (void)cd;
    return 0;
}
