#ifndef APPS_H
#define APPS_H

#include <userland/modules/include/utils.h>   /* for struct rect */
#include <stdint.h>

enum app_type {
    APP_NONE = 0,
    APP_TERMINAL,
    APP_CLOCK,
    APP_FILEMANAGER,
    APP_EDITOR,
    APP_TASKMANAGER,
    APP_CALCULATOR,
    APP_SKETCHPAD,
    APP_SNAKE,
    APP_TETRIS,
    APP_PACMAN,
    APP_SPACE_INVADERS,
    APP_PONG,
    APP_DONKEY_KONG,
    APP_BRICK_RACE,
    APP_FLAP_BIRB,
    APP_DOOM,
    APP_CRAFT,
    APP_PERSONALIZE,
};

#define MAX_WINDOWS 20
#define MAX_TERMINALS 4
#define MAX_FILEMANAGERS 2
#define MAX_CLOCKS 4
#define MAX_EDITORS 2
#define MAX_TASKMGRS 1
#define MAX_CALCULATORS 2
#define MAX_SKETCHPADS 1
#define MAX_SNAKES 1
#define MAX_TETRIS 1
#define MAX_PACMAN 1
#define MAX_SPACE_INVADERS 1
#define MAX_PONG 1
#define MAX_DONKEY_KONG 1
#define MAX_BRICK_RACE 1
#define MAX_FLAP_BIRB 1
#define MAX_DOOM 1
#define MAX_CRAFT 1

/* generic window descriptor used by desktop_main and task manager */
struct window {
    int active;
    enum app_type type;
    struct rect rect;
    struct rect restore_rect;
    int close_hover;
    int minimized;
    int maximized;
    uint32_t start_ticks;
    int instance; /* index into type-specific state array */
};

#endif // APPS_H
