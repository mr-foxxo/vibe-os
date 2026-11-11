/*
 * Stack-protector support for the userland main binary (startx/init modules)
 * — P4 classe #5 probe. The shared CFLAGS now enable -fstack-protector for
 * every object, and this link group (USERLAND_SRCS: userland.c + modules/*)
 * does not include lang/sdk/app_runtime.c (which carries the per-app
 * guard), so the guard word and fail path live here. The fail path uses the
 * existing sys_write_debug channel so a corrupted local frame names itself
 * instead of vanishing into a silent bogus-ret, and then parks the task.
 */
extern void sys_write_debug(const char *msg);
extern void sys_yield(void);

unsigned long __stack_chk_guard = 0x56494245UL; /* "VIBE" */

__attribute__((noreturn)) void __stack_chk_fail(void) {
    sys_write_debug("userland: stack check fail (local buffer overflow)\n");
    for (;;) {
        sys_yield();
    }
}
