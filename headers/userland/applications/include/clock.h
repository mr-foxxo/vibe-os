#ifndef CLOCK_H
#define CLOCK_H

#include <userland/modules/include/utils.h>
#include <stdint.h>

struct clock_state {
    struct rect window;
    uint32_t tick_count;
    uint32_t last_second;
};

void clock_init_state(struct clock_state *c);
int clock_step(struct clock_state *c);
void clock_draw_window(struct clock_state *c, int active,
                       int min_hover, int max_hover, int close_hover);

#endif // CLOCK_H
