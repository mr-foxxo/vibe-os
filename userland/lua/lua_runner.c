#include <userland/modules/include/console.h>
#include <userland/modules/include/syscalls.h>

#include "lua_internal.h"

#define VIBE_LUA_EXIT_KEY "vibe.exit_requested"

static void *vibe_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud;
    (void)osize;

    if (nsize == 0u) {
        free(ptr);
        return 0;
    }

    if (!ptr) {
        return malloc(nsize);
    }

    return realloc(ptr, nsize);
}

static void vibe_lua_report_error(lua_State *L, int status) {
    const char *message;

    if (status == LUA_OK) {
        return;
    }

    message = lua_tostring(L, -1);
    if (!message) {
        message = "erro desconhecido";
    }
    console_write("lua: ");
    console_write(message);
    console_putc('\n');
    lua_pop(L, 1);
}

static int vibe_lua_panic(lua_State *L) {
    const char *message = lua_tostring(L, -1);

    console_write("lua panic: ");
    console_write(message ? message : "erro interno");
    console_putc('\n');
    return 0;
}

static int vibe_lua_exit(lua_State *L) {
    vibe_lua_request_exit(L);
    return 0;
}

static void vibe_lua_openlibs(lua_State *L) {
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);

    luaL_requiref(L, "vibeconsole", luaopen_vibeconsole, 1);
    lua_setglobal(L, "vibeconsole");

    luaL_requiref(L, "vibesys", luaopen_vibesys, 1);
    lua_setglobal(L, "vibesys");

    lua_pushcfunction(L, vibe_lua_exit);
    lua_setglobal(L, "exit");

    lua_pushcfunction(L, vibe_lua_exit);
    lua_setglobal(L, "quit");
}

lua_State *vibe_lua_new_state(void) {
    lua_State *L = lua_newstate(vibe_lua_alloc, 0);

    if (!L) {
        console_write("lua: memoria insuficiente para iniciar o runtime\n");
        return 0;
    }

    lua_atpanic(L, vibe_lua_panic);
    vibe_lua_reset_exit_request(L);
    vibe_lua_openlibs(L);
    return L;
}

void vibe_lua_close_state(lua_State *L) {
    if (L) {
        lua_close(L);
    }
}

void vibe_lua_request_exit(lua_State *L) {
    lua_pushboolean(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, VIBE_LUA_EXIT_KEY);
}

int vibe_lua_take_exit_request(lua_State *L) {
    int should_exit;

    lua_getfield(L, LUA_REGISTRYINDEX, VIBE_LUA_EXIT_KEY);
    should_exit = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_pushboolean(L, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, VIBE_LUA_EXIT_KEY);
    return should_exit;
}

void vibe_lua_reset_exit_request(lua_State *L) {
    lua_pushboolean(L, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, VIBE_LUA_EXIT_KEY);
}

int vibe_lua_run_file(lua_State *L, const char *path) {
    int status;

    vibe_lua_reset_exit_request(L);
    status = luaL_loadfilex(L, path, 0);
    if (status != LUA_OK) {
        vibe_lua_report_error(L, status);
        return status;
    }

    status = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        vibe_lua_report_error(L, status);
        return status;
    }

    lua_settop(L, 0);
    return LUA_OK;
}
