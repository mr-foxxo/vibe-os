#ifndef VIBE_STDINT_H
#define VIBE_STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;

typedef int intptr_t;
typedef unsigned int uintptr_t;
typedef unsigned int size_t;
typedef int ssize_t;
typedef int ptrdiff_t;

#define UINT8_MAX 255
#define INT8_MAX 127
#define INT8_MIN (-128)
#define UINT16_MAX 65535
#define INT16_MAX 32767
#define INT16_MIN (-32768)
#define UINT32_MAX 4294967295U
#define INT32_MAX 2147483647
#define INT32_MIN (-2147483648)
#define UINT64_MAX 18446744073709551615ULL
#define INT64_MAX 9223372036854775807LL
#define INT64_MIN (-9223372036854775807LL - 1)

#endif
