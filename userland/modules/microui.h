#ifndef MICROUI_H
#define MICROUI_H

#include <stdint.h>

#define MU_COMMANDLIST_SIZE     (256 * 1024)
#define MU_ROOTLIST_SIZE        32
#define MU_CONTAINERSTACK_SIZE  32
#define MU_CLIPSTACK_SIZE       32
#define MU_IDSTACK_SIZE         32
#define MU_LAYOUTSTACK_SIZE     16
#define MU_CONTAINERPOOL_SIZE   48
#define MU_TREENODES_SIZE       16
#define MU_MAX_WIDTHS           16
#define MU_REAL                 float
#define MU_REAL_FMT             "%.3f"
#define MU_SLIDER_FMT           "%.2f"
#define MU_MAX_FMT              127

enum mu_color {
    MU_COLOR_TEXT,
    MU_COLOR_BORDER,
    MU_COLOR_WINDOWBG,
    MU_COLOR_TITLEBG,
    MU_COLOR_TITLETEXT,
    MU_COLOR_PANELBG,
    MU_COLOR_BUTTON,
    MU_COLOR_BUTTONHOVER,
    MU_COLOR_BUTTONFOCUS,
    MU_COLOR_BASE,
    MU_COLOR_BASEHOVER,
    MU_COLOR_BASEFOCUS,
    MU_COLOR_SCROLLBASE,
    MU_COLOR_SCROLLTHUMB,
    MU_COLOR_MAX
};

enum mu_options {
    MU_OPT_CLIPCONTENT = 1,
    MU_OPT_CLOSED      = 2,
    MU_OPT_NOFRAME     = 4,
    MU_OPT_NORESIZE    = 8,
    MU_OPT_NOSCROLL    = 16,
    MU_OPT_NOCLOSE     = 32,
    MU_OPT_NOTITLE     = 64,
    MU_OPT_HOLDFOCUS   = 128,
    MU_OPT_AUTOSIZE    = 256,
    MU_OPT_POPUP       = 512,
    MU_OPT_ALIGNLEFT   = 0,
    MU_OPT_ALIGNCENTER = 1024,
    MU_OPT_ALIGNRIGHT  = 2048,
};

enum mu_event {
    MU_EVENT_MOUSEDOWN  = (1 << 0),
    MU_EVENT_MOUSEUP    = (1 << 1),
    MU_EVENT_MOUSEMOVE  = (1 << 2),
    MU_EVENT_MOUSEWHEEL = (1 << 3),
    MU_EVENT_RESERVED   = (1 << 4),
    MU_EVENT_KEYDOWN    = (1 << 5),
    MU_EVENT_KEYUP      = (1 << 6),
    MU_EVENT_TEXTINPUT  = (1 << 7),
};

enum mu_key {
    MU_KEY_SHIFT    = 1,
    MU_KEY_CTRL     = 2,
    MU_KEY_ALT      = 4,
    MU_KEY_BACKSPACE = 8,
    MU_KEY_RETURN   = 16,
};

struct mu_rect {
    int x, y, w, h;
};

struct mu_color_value {
    unsigned char r, g, b, a;
};

struct mu_context;

typedef int  (*mu_text_width_fn)(struct mu_context *ctx, const char *text, int len);
typedef int  (*mu_text_height_fn)(struct mu_context *ctx, const char *text, int len);
typedef void (*mu_draw_frame_fn)(struct mu_context *ctx, struct mu_rect rect, int color);

struct mu_style {
    int margin_top, margin_right, margin_bottom, margin_left;
    int padding, spacing, indent;
    int title_height, scrollbar_size, thumb_size;
    int gap;
    struct mu_color_value colors[MU_COLOR_MAX];
};

struct mu_command {
    int type;
    int size;
};

enum mu_command_type {
    MU_COMMAND_JUMP   = 1,
    MU_COMMAND_CLIP   = 2,
    MU_COMMAND_RECT   = 3,
    MU_COMMAND_TEXT   = 4,
    MU_COMMAND_ICON   = 5,
    MU_COMMAND_MAX
};

struct mu_clip_command {
    int type;
    int size;
    struct mu_rect rect;
};

struct mu_rect_command {
    int type;
    int size;
    struct mu_rect rect;
    int color;
};

struct mu_text_command {
    int type;
    int size;
    struct mu_rect rect;
    int color;
    char str[1];
};

struct mu_icon_command {
    int type;
    int size;
    struct mu_rect rect;
    int id;
};

struct mu_pool_item {
    int id;
    int last_update;
    struct mu_rect rect;
    int open;
};

struct mu_container {
    struct mu_rect body;
    struct mu_rect scroll;
    int open;
    int id;
    int zindex;
    int head;
    const char *title;
    int title_len;
};

struct mu_real_layout {
    MU_REAL body[4], next[4], position[4];
    int up, max_x, max_y;
    int width, height;
    int expand;
    int item_index;
    int next_row;
};

struct mu_id {
    int id;
    struct mu_id *next;
};

struct mu_id_pool {
    struct mu_id *free_list;
    struct mu_id slots[256];
};

struct mu_context {
    struct mu_rect clip;
    struct mu_style style;
    int id;
    int last_id;
    int hover_root;
    int next_hover_root;
    int scroll_delta;
    int mouse_x, mouse_y;
    int mouse_down;
    int mouse_pressed;
    int key_down;
    int key_pressed;
    int text_input[16];
    int text_input_len;

    mu_text_width_fn text_width;
    mu_text_height_fn text_height;
    mu_draw_frame_fn draw_frame;

    char _begin_pool;

    struct mu_command *cmd_head;
    struct mu_command *cmd_tail;
    struct mu_command *cmd_ptr;
    char cmd_list[MU_COMMANDLIST_SIZE];

    struct mu_container *root_list[MU_ROOTLIST_SIZE];
    int root_count;

    struct mu_container *container_stack[MU_CONTAINERSTACK_SIZE];
    int container_stack_len;

    struct mu_rect clip_stack[MU_CLIPSTACK_SIZE];
    int clip_stack_len;

    struct mu_real_layout layout_stack[MU_LAYOUTSTACK_SIZE];
    int layout_stack_len;

    struct mu_id id_stack[MU_IDSTACK_SIZE];
    int id_stack_len;

    struct mu_pool_item container_pool[MU_CONTAINERPOOL_SIZE];
    int container_pool_len;

    struct mu_id_pool id_pool;
};

void mu_init(struct mu_context *ctx);
void mu_begin(struct mu_context *ctx);
void mu_end(struct mu_context *ctx);
void mu_set_focus(struct mu_context *ctx, int id);
int mu_get_focus(struct mu_context *ctx);
void mu_push_id(struct mu_context *ctx, int id);
void mu_pop_id(struct mu_context *ctx);
void mu_push_clip_rect(struct mu_context *ctx, struct mu_rect rect);
void mu_pop_clip_rect(struct mu_context *ctx);

void mu_begin_window(struct mu_context *ctx, const char *title, struct mu_rect rect, int opt);
void mu_end_window(struct mu_context *ctx);

void mu_begin_panel(struct mu_context *ctx, const char *name);
void mu_end_panel(struct mu_context *ctx);

void mu_begin_treenode(struct mu_context *ctx, int id, const char *label, int open);
int mu_end_treenode(struct mu_context *ctx);

void mu_layout_row(struct mu_context *ctx, int items, const int *widths, int height);
void mu_layout_width(struct mu_context *ctx, int width);
void mu_layout_height(struct mu_context *ctx, int height);
void mu_layout_begin_column(struct mu_context *ctx);
void mu_layout_end_column(struct mu_context *ctx);
void mu_layout_set_next(struct mu_context *ctx, struct mu_rect r, int relative);

struct mu_rect mu_layout_next(struct mu_context *ctx);

int mu_button(struct mu_context *ctx, const char *label);
int mu_checkbox(struct mu_context *ctx, const char *label, int *value);
int mu_textbox(struct mu_context *ctx, char *buf, int bufsz, int flags);
int mu_slider(struct mu_context *ctx, MU_REAL *value, MU_REAL low, MU_REAL high, MU_REAL step, const char *fmt, int flags);
int mu_number(struct mu_context *ctx, int *value, int step);
void mu_header(struct mu_context *ctx, const char *label);
int mu_treenode(struct mu_context *ctx, const char *label);
void mu_text(struct mu_context *ctx, const char *text);
void mu_label(struct mu_context *ctx, const char *label);

int mu_draw_cmd_list(struct mu_context *ctx, uint8_t *screen, int sw, int sh, int spitch);

#endif
