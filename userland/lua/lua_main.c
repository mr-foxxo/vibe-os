#include <userland/modules/include/console.h>

#include "lua_internal.h"

int vibe_lua_main(int argc, char **argv) {
    lua_State *L = vibe_lua_new_state();

    if (!L) {
        return 0;
    }

    if (argc <= 1) {
        console_write(LUA_VERSION);
        console_putc('\n');
        (void)vibe_lua_run_repl(L);
    } else {
        if (argc > 2) {
            console_write("lua: argumentos extras ignorados\n");
        }
        (void)vibe_lua_run_file(L, argv[1]);
    }

    vibe_lua_close_state(L);
    return 0;
}
