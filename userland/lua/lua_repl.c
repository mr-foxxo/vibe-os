#include <userland/modules/include/console.h>

#include "lua_internal.h"

#define VIBE_LUA_REPL_LINE_MAX 256
#define VIBE_LUA_REPL_BUFFER_MAX 1024

static void repl_backspace(void) {
    console_putc('\b');
    console_putc(' ');
    console_putc('\b');
}

static int repl_readline(char *buf, int maxlen, const char *prompt) {
    int len = 0;

    console_write(prompt);
    for (;;) {
        int c = console_getchar();

        if (c == '\r') {
            c = '\n';
        }

        if (c == 4 && len == 0) {
            console_putc('\n');
            buf[0] = '\0';
            return -1;
        }

        if (c == '\n') {
            buf[len] = '\0';
            console_putc('\n');
            return len;
        }

        if ((c == '\b' || c == 127) && len > 0) {
            --len;
            repl_backspace();
            continue;
        }

        if (c >= 32 && c < 127 && len < (maxlen - 1)) {
            buf[len++] = (char)c;
            console_putc((char)c);
        }
    }
}

static int repl_is_incomplete(lua_State *L, int status) {
    const char *message;

    if (status != LUA_ERRSYNTAX) {
        return 0;
    }

    message = lua_tostring(L, -1);
    return message != 0 && strstr(message, "<eof>") != 0;
}

static int repl_load_expression(lua_State *L, const char *buffer) {
    char chunk[VIBE_LUA_REPL_BUFFER_MAX + 8];
    int src = 0;
    const char *body = buffer;

    if (buffer[0] == '=') {
        body = buffer + 1;
    }

    memcpy(chunk, "return ", 7u);
    src = 7;
    while (*body != '\0' && src < (int)sizeof(chunk) - 1) {
        chunk[src++] = *body++;
    }
    chunk[src] = '\0';

    return luaL_loadbufferx(L, chunk, (size_t)src, "=repl", "t");
}

static void repl_print_results(lua_State *L, int base_top) {
    int top = lua_gettop(L);

    if (top <= base_top) {
        return;
    }

    for (int i = base_top + 1; i <= top; ++i) {
        size_t len = 0;
        const char *text;

        text = luaL_tolstring(L, i, &len);
        if (i > base_top + 1) {
            console_putc('\t');
        }
        if (text) {
            vibe_lua_console_write_len(text, len);
        }
        lua_pop(L, 1);
    }
    console_putc('\n');
}

static int repl_compile(lua_State *L, const char *buffer, int *print_results) {
    int status;

    *print_results = 0;
    status = repl_load_expression(L, buffer);
    if (status == LUA_OK) {
        *print_results = 1;
        return status;
    }
    lua_pop(L, 1);

    status = luaL_loadbufferx(L, buffer, strlen(buffer), "=repl", "t");
    return status;
}

static void repl_report_error(lua_State *L) {
    const char *message = lua_tostring(L, -1);

    console_write("lua: ");
    console_write(message ? message : "erro desconhecido");
    console_putc('\n');
    lua_pop(L, 1);
}

int vibe_lua_run_repl(lua_State *L) {
    char line[VIBE_LUA_REPL_LINE_MAX];
    char buffer[VIBE_LUA_REPL_BUFFER_MAX];
    int buffered = 0;

    buffer[0] = '\0';
    vibe_lua_reset_exit_request(L);

    for (;;) {
        int len = repl_readline(line,
                                (int)sizeof(line),
                                buffered == 0 ? "> " : ">> ");
        int print_results = 0;
        int base_top = lua_gettop(L);
        int status;

        if (len < 0) {
            break;
        }
        if (len == 0 && buffered == 0) {
            continue;
        }

        if (buffered != 0) {
            if (buffered + 1 >= (int)sizeof(buffer)) {
                console_write("lua: chunk muito grande\n");
                buffer[0] = '\0';
                buffered = 0;
                continue;
            }
            buffer[buffered++] = '\n';
            buffer[buffered] = '\0';
        }

        if (buffered + len >= (int)sizeof(buffer)) {
            console_write("lua: linha muito grande\n");
            buffer[0] = '\0';
            buffered = 0;
            continue;
        }

        memcpy(buffer + buffered, line, (size_t)len + 1u);
        buffered += len;

        status = repl_compile(L, buffer, &print_results);
        if (status != LUA_OK) {
            if (repl_is_incomplete(L, status)) {
                lua_pop(L, 1);
                continue;
            }
            repl_report_error(L);
            buffer[0] = '\0';
            buffered = 0;
            lua_settop(L, base_top);
            continue;
        }

        status = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (status != LUA_OK) {
            repl_report_error(L);
            lua_settop(L, base_top);
        } else {
            if (print_results) {
                repl_print_results(L, base_top);
            }
            if (vibe_lua_take_exit_request(L)) {
                lua_settop(L, base_top);
                break;
            }
            lua_settop(L, base_top);
        }

        buffer[0] = '\0';
        buffered = 0;
    }

    lua_settop(L, 0);
    return LUA_OK;
}
