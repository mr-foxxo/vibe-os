/* config-vibeos.h
 * VibeOS configuration for Links2.
 * This is a minimal configuration header for building Links2
 * on VibeOS with the compat layer.
 *
 * Generated for VibeOS i686-elf target.
 */

#ifndef CONFIG_VIBEOS_H
#define CONFIG_VIBEOS_H

#define VERSION "2.29"
#define HOST_OS "vibeos"
#define HOST "i686-elf-vibeos"

/* C compiler */
#define HAVE_LONG_LONG 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDARG_H 1

/* C library (via compat) */
#define HAVE_STDLIB_H_X 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDINT_H 1

/* Memory functions */
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMSET 1
#define HAVE_MEMCMP 1
#define HAVE_MEMCHR 1

/* String functions */
#define HAVE_STRCPY 1
#define HAVE_STRNCPY 1
#define HAVE_STRLEN 1
#define HAVE_STRNLEN 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_STRSTR 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOD 1
#define HAVE_SNPRINTF 1
#define HAVE_SNPRINTF 1

/* I/O */
#define HAVE_FCNTL_H 1
#define HAVE_UTIME_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_FILE_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_TIME_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_UN_H 1
#define HAVE_DIRENT_H 1

/* Network */
#define HAVE_NETINET_IN_H 1
#define HAVE_ARPA_INET_H 1
#define HAVE_NETDB_H 1

/* Signals */
#define HAVE_SIGNAL_H 1

/* Locale */
#define HAVE_LOCALE_H 1

/* Math (optional, may not be available on freestanding) */
#define HAVE_MATH_H 1

/* Setjmp */
#define HAVE_SETJMP_H 1

/* Termios - VibeOS uses its own input system */
#define HAVE_TERMIOS_H 1

/* Time */
#define HAVE_GETTIMEOFDAY 1

/* Poll */
#undef HAVE_POLL

/* Graphics driver - VibeOS */
#define G 1
#define GRDRV_VIBEOS 1
#define GRDRV_VIRTUAL_DEVICES 1

/* No event library */
#undef HAVE_LIBEVENT_H
#undef USE_LIBEVENT

/* No OpenMP */
#undef HAVE_OPENMP

/* No SSL */
#undef HAVE_SSL
#undef HAVE_OPENSSL
#undef HAVE_NSS

/* No image libraries (links will still work without them) */
#undef HAVE_PNG_H
#undef HAVE_LIBPNG_PNG_H
#undef HAVE_JPEG
#undef HAVE_TIFF
#undef HAVE_WEBP
#undef HAVE_AVIF
#undef HAVE_SVG
#undef HAVE_FREETYPE

/* Endianness - i386 is little endian */
#define C_LITTLE_ENDIAN 1

/* Sizes */
#define SIZEOF_UNSIGNED_SHORT 2
#define SIZEOF_UNSIGNED_INT 4
#define SIZEOF_UNSIGNED_LONG 4

/* No gettext */
#undef ENABLE_NLS

/* No IPv6 for now */
#undef SUPPORT_IPV6

/* Use select, not poll */
#define HAVE_SYS_SELECT_H 1

/* Debug */
#undef DEBUG
#undef LEAK_DEBUG
#undef OOPS

/* Timer uses milliseconds */
#define HAVE_FTIME 1

/* Force graphics mode */
#define NO_GRPH 0

#endif /* CONFIG_VIBEOS_H */
