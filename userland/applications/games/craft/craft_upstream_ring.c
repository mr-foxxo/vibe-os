#include "craft_upstream_compat.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "upstream/src/ring.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
