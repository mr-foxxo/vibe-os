/* Python Runtime for VibeOS - HONEST "not implemented" diagnostic.
 *
 * /bin/python ships in the catalog but there is no MicroPython engine linked
 * into the build (MicroPython needs prebuilt artifacts; see
 * COMPLETION_ACTION_PLAN P7).
 *
 * The previous version claimed "maps to JS engine internally" (false - it
 * mapped to nothing) and "coming soon" behind a success-looking banner. The
 * contract forbids fake states: unsupported paths must say so explicitly.
 * Exit code stays 0 per the rescue-shell exit-on-nonzero convention (see
 * lang/apps/js/js_main.c header for the full note).
 */

#include "vibe_app.h"
#include "vibe_app_runtime.h"

int vibe_app_main(int argc, char **argv) {
    (void)argv;
    printf("python: NOT IMPLEMENTED (ENOSYS) - no MicroPython engine is linked into this build.\n");
    printf("python: it does NOT map to any other interpreter; scripts were not executed.\n");
    printf("python: tracked in docs/guidelines/COMPLETION_ACTION_PLAN.md (P7).\n");

    if (argc > 1) {
        printf("python: requested file was NOT executed.\n");
    }

    return 0;
}
