#ifndef VIBE_LUA_SETJMP_H
#define VIBE_LUA_SETJMP_H

typedef int jmp_buf[5];

static inline __attribute__((noreturn)) void vibe_lua_longjmp(int *env, int val) {
    (void)val;
    __builtin_longjmp(env, 1);
}

#define setjmp(env) __builtin_setjmp((int *)(env))
#define longjmp(env,val) vibe_lua_longjmp((int *)(env), (val))

#endif // VIBE_LUA_SETJMP_H
