#ifndef _SYS_ENDIAN_H_
#define _SYS_ENDIAN_H_

#include <stdint.h>

#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _PDP_ENDIAN 3412
#define _BYTE_ORDER _LITTLE_ENDIAN

#define LITTLE_ENDIAN _LITTLE_ENDIAN
#define BIG_ENDIAN _BIG_ENDIAN
#define BYTE_ORDER _BYTE_ORDER

static inline uint16_t __htole16(uint16_t x) { return x; }
static inline uint32_t __htole32(uint32_t x) { return x; }
static inline uint64_t __htole64(uint64_t x) { return x; }
static inline uint16_t __letoh16(uint16_t x) { return x; }
static inline uint32_t __letoh32(uint32_t x) { return x; }
static inline uint64_t __letoh64(uint64_t x) { return x; }
static inline uint16_t __htobe16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t __htobe32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t __htobe64(uint64_t x) { return __builtin_bswap64(x); }
static inline uint16_t __betoh16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t __betoh32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t __betoh64(uint64_t x) { return __builtin_bswap64(x); }

#define htole16(x) __htole16(x)
#define htole32(x) __htole32(x)
#define htole64(x) __htole64(x)
#define letoh16(x) __letoh16(x)
#define letoh32(x) __letoh32(x)
#define letoh64(x) __letoh64(x)
#define htobe16(x) __htobe16(x)
#define htobe32(x) __htobe32(x)
#define htobe64(x) __htobe64(x)
#define betoh16(x) __betoh16(x)
#define betoh32(x) __betoh32(x)
#define betoh64(x) __betoh64(x)

#define le16toh(x) __letoh16(x)
#define le32toh(x) __letoh32(x)
#define le64toh(x) __letoh64(x)
#define be16toh(x) __betoh16(x)
#define be32toh(x) __betoh32(x)
#define be64toh(x) __betoh64(x)

#define swap16(x) __builtin_bswap16(x)
#define swap32(x) __builtin_bswap32(x)
#define swap64(x) __builtin_bswap64(x)

#endif
