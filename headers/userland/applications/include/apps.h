#ifndef APPS_H
#define APPS_H

#include <userland/modules/include/utils.h>   /* for struct rect */
#include <stdint.h>

enum app_type {
    APP_NONE = 0,
    APP_TERMINAL,
    APP_CLOCK,
    APP_FILEMANAGER,
    APP_TASKMANAGER,
};

#define MAX_WINDOWS 8
#define MAX_TERMINALS 4
#define MAX_FILEMANAGERS 2
#define MAX_CLOCKS 4
#define MAX_TASKMGRS 1

/* generic window descriptor used by desktop_main and task manager */
struct window {
    int active;
    enum app_type type;
    struct rect rect;
    int close_hover;
    uint32_t start_ticks;
    int instance; /* index into type-specific state array */
};

#endif // APPS_H
