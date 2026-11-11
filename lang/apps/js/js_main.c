/* JavaScript Runtime for VibeOS - HONEST "not implemented" diagnostic.
 *
 * This app is shipped in the catalog (/bin/js) but there is NO working
 * JavaScript engine wired into the build. QuickJS is vendored under
 * lang/vendor/quickjs-ng and a real consumer already exists in
 * js_main_real.c, but it is not linked because QuickJS needs a full
 * freestanding libc port first (see COMPLETION_ACTION_PLAN P7).
 *
 * The previous js_main.c was a fake REPL: it echoed the typed line back as
 * "Result: <line>", pretending to evaluate JavaScript. The contract forbids
 * that class of false green ("Unsupported = explicit diagnostic, never a fake
 * state"). So this entry point now reports the limitation explicitly and
 * machine-readably instead of pretending.
 *
 * Exit code note: the rescue shell terminates on any non-zero command rc
 * (userland/modules/shell.c), and this userland's convention for "command did
 * not run" is exit 0 + a printed diagnostic (see busybox "unknown command").
 * The no-false-green guarantee therefore comes from the explicit
 * "NOT IMPLEMENTED (ENOSYS)" line and the absence of any fake evaluation,
 * not from a shell-killing non-zero rc.
 */

#include "vibe_app.h"
#include "vibe_app_runtime.h"

int vibe_app_main(int argc, char **argv) {
    vibe_app_console_write("js: NOT IMPLEMENTED (ENOSYS) - no JavaScript engine is linked into this build.\n");
    vibe_app_console_write("js: QuickJS is vendored; a real consumer exists (js_main_real.c) but needs a freestanding libc port first.\n");
    vibe_app_console_write("js: tracked in docs/guidelines/COMPLETION_ACTION_PLAN.md (P7).\n");

    if (argc > 1) {
        vibe_app_console_write("js: requested file was NOT executed: ");
        vibe_app_console_write(argv[1]);
        vibe_app_console_write("\n");
    }

    return 0;
}
