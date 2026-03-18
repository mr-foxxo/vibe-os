#ifndef INTTYPES_H
#define INTTYPES_H

#include <stdint.h>

/* Format specifiers for printf/scanf */
#define PRId8       "d"
#define PRId16      "d"
#define PRId32      "d"
#define PRId64      "ld"

#define PRIi8       "i"
#define PRIi16      "i"
#define PRIi32      "i"
#define PRIi64      "li"

#define PRIu8       "u"
#define PRIu16      "u"
#define PRIu32      "u"
#define PRIu64      "lu"

#define PRIx8       "x"
#define PRIx16      "x"
#define PRIx32      "x"
#define PRIx64      "lx"

#define PRIX8       "X"
#define PRIX16      "X"
#define PRIX32      "X"
#define PRIX64      "lX"

#define PRIo8       "o"
#define PRIo16      "o"
#define PRIo32      "o"
#define PRIo64      "lo"

#endif /* INTTYPES_H */
