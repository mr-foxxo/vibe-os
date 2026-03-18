/* QuickJS for Vibe OS - Minimal Bare-Metal Integration
 * Compiles real QuickJS-ng with stubs for embedded use
 */

/* Bare-metal libc headers instead of system headers */
#include "../../headers/lang/vibe_libc.h"
#include "../../headers/lang/vibe_stdint.h"
#include "../../headers/lang/vibe_stdlib.h"
#include "../../headers/lang/vibe_stdio.h"
#include "../../headers/lang/vibe_string.h"

/* Feature flags to disable complex QuickJS features */
#define CONFIG_BIGNUM 0
#define CONFIG_AGENT 0
#define CONFIG_ATOMICS 0
#define CONFIG_VERSION "13.0.0-vibe"

/* Include QuickJS sources */
#include "quickjs.c"
#include "libregexp.c"
#include "libunicode.c"
#include "dtoa.c"
