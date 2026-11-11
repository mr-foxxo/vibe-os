/* Ruby Runtime for VibeOS - HONEST "not implemented" diagnostic.
 *
 * /bin/ruby ships in the catalog but there is no mruby engine linked into
 * the build (mruby needs prebuilt artifacts; see COMPLETION_ACTION_PLAN P7).
 *
 * The previous version claimed "maps to JS engine internally" (false - it
 * mapped to nothing) and "coming soon" while returning a success-looking
 * banner. The contract forbids fake states: unsupported paths must say so
 * explicitly. The rescue-shell exit-on-nonzero convention keeps the exit code
 * at 0 (see lang/apps/js/js_main.c header for the full note).
 */

#include "vibe_app.h"
#include "vibe_app_runtime.h"

int vibe_app_main(int argc, char **argv) {
    (void)argv;
    printf("ruby: NOT IMPLEMENTED (ENOSYS) - no mruby engine is linked into this build.\n");
    printf("ruby: it does NOT map to any other interpreter; scripts were not executed.\n");
    printf("ruby: tracked in docs/guidelines/COMPLETION_ACTION_PLAN.md (P7).\n");

    if (argc > 1) {
        printf("ruby: requested file was NOT executed.\n");
    }

    return 0;
}
