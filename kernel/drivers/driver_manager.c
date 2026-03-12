#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>  /* not strictly needed but keep consistency */

#include <kernel/driver_manager.h>
#include <kernel/kernel.h>

#define MAX_DRIVERS 32

struct driver_entry {
    const char *name;
    const char *type;
    void (*init)(void);
};

static struct driver_entry g_drivers[MAX_DRIVERS];
static int g_driver_count = 0;

int register_driver(const char *name, const char *type, void (*init)(void)) {
    if (g_driver_count >= MAX_DRIVERS || !name || !type || !init) {
        return -1;
    }
    g_drivers[g_driver_count].name = name;
    g_drivers[g_driver_count].type = type;
    g_drivers[g_driver_count].init = init;
    g_driver_count++;
    return 0;
}

void driver_manager_init(void) {
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].init) {
            g_drivers[i].init();
        }
    }
}
