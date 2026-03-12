#include <stdint.h>

#include <include/userland_api.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/shell.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>

__attribute__((section(".entry"))) void userland_entry(void) {
    /* initialize filesystem first, shell will rely on it */
    fs_init();

    /* start text-mode console and drop into shell */
    console_init();
    shell_main();

    /* if shell exits, just halt */
    for (;;) {
        sys_sleep();
    }
}
