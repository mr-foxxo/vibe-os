/*
 * VibeOS Compatibility Layer
 * 
 * Central header for POSIX/libc compatibility
 * Used by ported GNU applications
 * 
 * This layer bridges ported apps to VibeOS syscalls and VFS
 */

#ifndef COMPAT_H
#define COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* System types */
#include <sys/types.h>

/* Standard library groups */
#include "compat/libc/stdlib.h"
#include "compat/libc/string.h"
#include "compat/libc/stdio.h"
#include "compat/libc/ctype.h"
#include "compat/libc/assert.h"

/* POSIX APIs */
#include "compat/posix/unistd.h"
#include "compat/posix/fcntl.h"
#include "compat/posix/stat.h"
#include "compat/posix/errno.h"

/* VibeOS-specific app runtime */
#include <lang/include/vibe_app_runtime.h>

#endif /* COMPAT_H */
