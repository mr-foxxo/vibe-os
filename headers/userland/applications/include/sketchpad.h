#ifndef SKETCHPAD_H
#define SKETCHPAD_H

#include <userland/modules/include/utils.h>
#include <stdint.h>

#define SKETCHPAD_CANVAS_W 56
#define SKETCHPAD_CANVAS_H 40
#define SKETCHPAD_COLOR_COUNT 10

struct sketchpad_state {
    struct rect window;
    uint8_t pixels[SKETCHPAD_CANVAS_H][SKETCHPAD_CANVAS_W];
    uint8_t current_color;
    char last_export_path[80];
};

void sketchpad_init_state(struct sketchpad_state *sketch);
void sketchpad_clear(struct sketchpad_state *sketch);
void sketchpad_draw_window(struct sketchpad_state *sketch, int active,
                           int min_hover, int max_hover, int close_hover);
struct rect sketchpad_canvas_rect(const struct sketchpad_state *sketch);
struct rect sketchpad_clear_button_rect(const struct sketchpad_state *sketch);
struct rect sketchpad_export_button_rect(const struct sketchpad_state *sketch);
struct rect sketchpad_color_rect(const struct sketchpad_state *sketch, int index);
int sketchpad_hit_color(const struct sketchpad_state *sketch, int x, int y);
int sketchpad_paint_at(struct sketchpad_state *sketch, int x, int y);
int sketchpad_export_bitmap(struct sketchpad_state *sketch);
int sketchpad_export_bitmap_named(struct sketchpad_state *sketch, const char *filename);

#endif // SKETCHPAD_H
