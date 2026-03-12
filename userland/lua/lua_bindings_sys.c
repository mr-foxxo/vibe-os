#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>

#include "lua_internal.h"

static int vibesys_ticks(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)sys_ticks());
    return 1;
}

static int vibesys_yield(lua_State *L) {
    (void)L;
    sys_yield();
    return 0;
}

static int vibesys_cwd(lua_State *L) {
    char path[80];

    fs_build_path(g_fs_cwd, path, (int)sizeof(path));
    lua_pushstring(L, path);
    return 1;
}

int luaopen_vibesys(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"ticks", vibesys_ticks},
        {"yield", vibesys_yield},
        {"cwd", vibesys_cwd},
        {0, 0}
    };

    luaL_newlib(L, funcs);
    return 1;
}
