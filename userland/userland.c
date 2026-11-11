#include <stdint.h>
#include <kernel/bootinfo.h>
#include <userland/modules/include/busybox.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/shell.h>

#ifdef VIBE_USERLAND_APP
static void userland_app_boot_debug(const char *text) {
    __asm__ volatile("int $0x80"
                     :
                     : "a"(11), "b"((int)(uintptr_t)text), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
}

void kernel_debug_puts(const char *msg) {
    (void)msg;
}

int vibe_app_main(int argc, char **argv) {
    struct userland_launch_info info;
    int boot_to_desktop = 0;

    (void)argc;
    (void)argv;
    userland_app_boot_debug("userland.app: shell start\n");
    if (sys_launch_info(&info) == 0) {
        boot_to_desktop =
            (info.boot_flags & BOOTINFO_FLAG_BOOT_TO_DESKTOP) != 0u &&
            (info.boot_flags & (BOOTINFO_FLAG_BOOT_SAFE_MODE | BOOTINFO_FLAG_BOOT_RESCUE_SHELL)) == 0u;
    }
    if (boot_to_desktop) {
        if (sys_launch_app("startx") > 0) {
            userland_app_boot_debug("userland.app: autostart startx\n");
            userland_app_boot_debug("userland.app: desktop handoff complete\n");
            return 0;
        }
        userland_app_boot_debug("userland.app: autostart startx failed, attempting builtin fallback\n");
        if (sys_launch_builtin_user(USERLAND_BUILTIN_STARTX) > 0) {
            userland_app_boot_debug("userland.app: autostart startx builtin fallback\n");
            return 0;
        }
        userland_app_boot_debug("userland.app: autostart startx builtin fallback failed\n");
    }
    userland_app_boot_debug("userland.app: shell_start begin\n");
    shell_start();
    return 0;
}
#else
__attribute__((section(".entry"))) void userland_entry(void) {
    shell_start();
    for (;;)
        __asm__ volatile("hlt");
}
#endif
