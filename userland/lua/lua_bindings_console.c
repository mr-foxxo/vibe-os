#include "lua_internal.h"

static int vibeconsole_write(lua_State *L) {
    int n = lua_gettop(L);

    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char *text = luaL_tolstring(L, i, &len);

        if (text) {
            vibe_lua_console_write_len(text, len);
        }
        lua_pop(L, 1);
    }
    return 0;
}

static int vibeconsole_writeln(lua_State *L) {
    vibeconsole_write(L);
    vibe_lua_console_write_len("\n", 1);
    return 0;
}

int luaopen_vibeconsole(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"write", vibeconsole_write},
        {"writeln", vibeconsole_writeln},
        {0, 0}
    };

    luaL_newlib(L, funcs);
    return 1;
}
