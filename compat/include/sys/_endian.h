#ifndef _SYS__ENDIAN_H_
#define _SYS__ENDIAN_H_

#define _LITTLE_ENDIAN 1234
#define _BIG_ENDIAN 4321
#define _PDP_ENDIAN 3412

#define __swap16(x) ((unsigned short)((((unsigned short)(x) & 0xffU) << 8) | \
                     (((unsigned short)(x) & 0xff00U) >> 8)))
#define __swap32(x) ((unsigned int)((((unsigned int)(x) & 0xff) << 24) | \
                     (((unsigned int)(x) & 0xff00) << 8) | \
                     (((unsigned int)(x) & 0xff0000) >> 8) | \
                     (((unsigned int)(x) & 0xff000000) >> 24)))

#define __htobe16 __swap16
#define __htobe32 __swap32
#define __htole16(x) ((unsigned short)(x))
#define __htole32(x) ((unsigned int)(x))

#endif
