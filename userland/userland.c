#include <stdint.h>

#include "userland_api.h"
#include "console.h"
#include "shell.h"
#include "fs.h"
#include "syscalls.h"

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
