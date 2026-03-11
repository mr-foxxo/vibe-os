#ifndef TASKMGR_H
#define TASKMGR_H

#include "apps.h"  /* for struct window */
#include "utils.h"
#include <stdint.h>

struct taskmgr_state {
    struct rect window;
};

void taskmgr_init_state(struct taskmgr_state *tm);
void taskmgr_draw_window(struct taskmgr_state *tm,
                          struct window *wins,
                          int win_count,
                          uint32_t ticks);

#endif // TASKMGR_H
