#ifndef VIBE_LUA_INTERNAL_H
#define VIBE_LUA_INTERNAL_H

#include <userland/lua/include/lua_main.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

lua_State *vibe_lua_new_state(void);
void vibe_lua_close_state(lua_State *L);
int vibe_lua_run_repl(lua_State *L);
int vibe_lua_run_file(lua_State *L, const char *path);
void vibe_lua_request_exit(lua_State *L);
int vibe_lua_take_exit_request(lua_State *L);
void vibe_lua_reset_exit_request(lua_State *L);
int luaopen_vibesys(lua_State *L);
int luaopen_vibeconsole(lua_State *L);

#endif // VIBE_LUA_INTERNAL_H
