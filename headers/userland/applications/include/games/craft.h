#ifndef GAME_CRAFT_H
#define GAME_CRAFT_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

#define CRAFT_MAX_EDITS 256

struct craft_edit {
    int x;
    int z;
    int top_y;
    int block;
    uint8_t active;
};

struct craft_state {
    struct rect window;
    int running;
    int last_code;
    int started;
    int failed;
    int focused;
    int surface_w;
    int surface_h;
    int mouse_x;
    int mouse_y;
    int mouse_dx;
    int mouse_dy;
    uint8_t mouse_buttons;
    char status[64];
};

void craft_init_state(struct craft_state *state);
int craft_step(struct craft_state *state, uint32_t ticks);
int craft_handle_key(struct craft_state *state, int key);
int craft_handle_click(struct craft_state *state);
void craft_update_input(struct craft_state *state, int focused,
                        int mouse_x, int mouse_y, int mouse_dx, int mouse_dy,
                        uint8_t mouse_buttons);
void craft_shutdown_state(struct craft_state *state);
void craft_draw_window(struct craft_state *state, int active,
                       int min_hover, int max_hover, int close_hover);
int craft_upstream_start(int width, int height);
int craft_upstream_frame(void);
void craft_upstream_stop(void);
void craft_upstream_resize(int width, int height);
void craft_upstream_queue_key(int key);
void craft_upstream_set_mouse(int x, int y, int dx, int dy,
                              uint8_t buttons, int focused, int inside);
void craft_upstream_blit(int x, int y);
void craft_upstream_request_close(void);

#endif
