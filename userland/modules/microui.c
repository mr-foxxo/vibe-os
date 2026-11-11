#include <userland/modules/include/microui.h>
#include <userland/modules/include/utils.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui_clip.h>
#include <string.h>
#include <stdlib.h>

#define unlikely(x) __builtin_expect(!!(x), 0)
#define default_alloc(s) malloc(s)
#define default_free(p)  free(p)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(x, a, b) min(max(x, a), b)

static int default_text_width(struct mu_context *ctx, const char *text, int len) {
    (void)ctx;
    if (!text || len == 0) return 0;
    return len * 6;
}

static int default_text_height(struct mu_context *ctx, const char *text, int len) {
    (void)ctx;
    (void)text;
    (void)len;
    return 7;
}

static void default_draw_frame(struct mu_context *ctx, struct mu_rect rect, int color) {
    (void)ctx;
    (void)rect;
    (void)color;
}

static __attribute__((unused)) int idpool_alloc(struct mu_id_pool *pool, int id) {
    struct mu_id *item;
    if (pool->free_list == 0) return 0;
    item = pool->free_list;
    pool->free_list = item->next;
    item->id = id;
    item->next = 0;
    return 1;
}

static __attribute__((unused)) void idpool_free(struct mu_id_pool *pool, int id) {
    struct mu_id *item;
    for (item = pool->free_list; item; item = item->next) {
        if (item->id == id) return;
    }
    item = &pool->slots[0];
    for (int i = 0; i < 256; i++) {
        if (pool->slots[i].id == id) {
            pool->slots[i].next = pool->free_list;
            pool->free_list = &pool->slots[i];
            return;
        }
    }
}

void mu_init(struct mu_context *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->style.margin_top = 2;
    ctx->style.margin_right = 2;
    ctx->style.margin_bottom = 2;
    ctx->style.margin_left = 2;
    ctx->style.padding = 4;
    ctx->style.spacing = 4;
    ctx->style.indent = 24;
    ctx->style.title_height = 20;
    ctx->style.scrollbar_size = 12;
    ctx->style.thumb_size = 8;
    ctx->style.gap = 2;

    {
        int i;
        for (i = 0; i < MU_COLOR_MAX; i++) {
            ctx->style.colors[i].r = 0;
            ctx->style.colors[i].g = 0;
            ctx->style.colors[i].b = 0;
            ctx->style.colors[i].a = 255;
        }
    }
    ctx->style.colors[MU_COLOR_TEXT].r = 224; ctx->style.colors[MU_COLOR_TEXT].g = 224; ctx->style.colors[MU_COLOR_TEXT].b = 224;
    ctx->style.colors[MU_COLOR_BORDER].r = 100; ctx->style.colors[MU_COLOR_BORDER].g = 100; ctx->style.colors[MU_COLOR_BORDER].b = 100;
    ctx->style.colors[MU_COLOR_WINDOWBG].r = 48; ctx->style.colors[MU_COLOR_WINDOWBG].g = 48; ctx->style.colors[MU_COLOR_WINDOWBG].b = 48;
    ctx->style.colors[MU_COLOR_TITLEBG].r = 60; ctx->style.colors[MU_COLOR_TITLEBG].g = 60; ctx->style.colors[MU_COLOR_TITLEBG].b = 60;
    ctx->style.colors[MU_COLOR_TITLETEXT].r = 224; ctx->style.colors[MU_COLOR_TITLETEXT].g = 224; ctx->style.colors[MU_COLOR_TITLETEXT].b = 224;
    ctx->style.colors[MU_COLOR_PANELBG].r = 40; ctx->style.colors[MU_COLOR_PANELBG].g = 40; ctx->style.colors[MU_COLOR_PANELBG].b = 40;
    ctx->style.colors[MU_COLOR_BUTTON].r = 72; ctx->style.colors[MU_COLOR_BUTTON].g = 72; ctx->style.colors[MU_COLOR_BUTTON].b = 72;
    ctx->style.colors[MU_COLOR_BUTTONHOVER].r = 96; ctx->style.colors[MU_COLOR_BUTTONHOVER].g = 96; ctx->style.colors[MU_COLOR_BUTTONHOVER].b = 96;
    ctx->style.colors[MU_COLOR_BUTTONFOCUS].r = 120; ctx->style.colors[MU_COLOR_BUTTONFOCUS].g = 120; ctx->style.colors[MU_COLOR_BUTTONFOCUS].b = 120;
    ctx->style.colors[MU_COLOR_BASE].r = 56; ctx->style.colors[MU_COLOR_BASE].g = 56; ctx->style.colors[MU_COLOR_BASE].b = 56;
    ctx->style.colors[MU_COLOR_BASEHOVER].r = 80; ctx->style.colors[MU_COLOR_BASEHOVER].g = 80; ctx->style.colors[MU_COLOR_BASEHOVER].b = 80;
    ctx->style.colors[MU_COLOR_BASEFOCUS].r = 100; ctx->style.colors[MU_COLOR_BASEFOCUS].g = 100; ctx->style.colors[MU_COLOR_BASEFOCUS].b = 100;
    ctx->style.colors[MU_COLOR_SCROLLBASE].r = 40; ctx->style.colors[MU_COLOR_SCROLLBASE].g = 40; ctx->style.colors[MU_COLOR_SCROLLBASE].b = 40;
    ctx->style.colors[MU_COLOR_SCROLLTHUMB].r = 96; ctx->style.colors[MU_COLOR_SCROLLTHUMB].g = 96; ctx->style.colors[MU_COLOR_SCROLLTHUMB].b = 96;

    ctx->text_width = default_text_width;
    ctx->text_height = default_text_height;
    ctx->draw_frame = default_draw_frame;

    for (int i = 0; i < 256; i++) {
        ctx->id_pool.slots[i].id = -1;
    }
    ctx->id_pool.free_list = &ctx->id_pool.slots[0];
    for (int i = 0; i < 255; i++) {
        ctx->id_pool.slots[i].next = &ctx->id_pool.slots[i + 1];
    }
    ctx->id_pool.slots[255].next = 0;

    ctx->cmd_head = (struct mu_command *)ctx->cmd_list;
    ctx->cmd_tail = ctx->cmd_head;
    ctx->cmd_ptr = ctx->cmd_head;
}

static __attribute__((unused)) void mu_push_command(struct mu_context *ctx, struct mu_command *cmd, int size) {
    (void)ctx;
    (void)cmd;
    cmd->type = MU_COMMAND_JUMP;
    cmd->size = sizeof(struct mu_command);
    ctx->cmd_ptr = (struct mu_command *)((char *)ctx->cmd_ptr + size);
}

static __attribute__((unused)) struct mu_command *mu_next_command(struct mu_context *ctx, struct mu_command *cmd) {
    (void)ctx;
    return (struct mu_command *)((char *)cmd + cmd->size);
}

void mu_begin(struct mu_context *ctx) {
    ctx->cmd_head = (struct mu_command *)ctx->cmd_list;
    ctx->cmd_tail = ctx->cmd_head;
    ctx->cmd_ptr = ctx->cmd_head;
    ctx->cmd_head->type = 0;
    ctx->cmd_head->size = sizeof(struct mu_command);

    ctx->id = 1;
    ctx->last_id = 0;
    ctx->hover_root = ctx->next_hover_root;
    ctx->next_hover_root = -1;
    ctx->mouse_pressed = 0;
    ctx->key_pressed = 0;
    ctx->scroll_delta = 0;
    ctx->container_stack_len = 0;
    ctx->clip_stack_len = 0;
    ctx->layout_stack_len = 0;
    ctx->id_stack_len = 0;
    ctx->container_pool_len = 0;
    ctx->root_count = 0;
    ctx->text_input_len = 0;
}

void mu_end(struct mu_context *ctx) {
    (void)ctx;
}

static struct mu_command *mu_jump_command(struct mu_context *ctx, int size) {
    struct mu_command *cmd;

    if ((char *)ctx->cmd_ptr + size >= ctx->cmd_list + MU_COMMANDLIST_SIZE) {
        return 0;
    }

    cmd = ctx->cmd_ptr;
    cmd->type = MU_COMMAND_JUMP;
    cmd->size = size;
    ctx->cmd_ptr = (struct mu_command *)((char *)ctx->cmd_ptr + size);
    return cmd;
}

static int mu_get_next_id(struct mu_context *ctx, int id) {
    int result = ctx->id;
    ctx->id += id + 1;
    return result;
}

void mu_push_id(struct mu_context *ctx, int id) {
    ctx->id_stack[ctx->id_stack_len].id = ctx->id;
    ctx->id_stack[ctx->id_stack_len].next = 0;
    ctx->id_stack_len++;
    ctx->id = mu_get_next_id(ctx, id);
}

void mu_pop_id(struct mu_context *ctx) {
    if (ctx->id_stack_len > 0) {
        ctx->id_stack_len--;
        ctx->id = ctx->id_stack[ctx->id_stack_len].id;
    }
}

void mu_push_clip_rect(struct mu_context *ctx, struct mu_rect rect) {
    struct mu_clip_command *cmd;

    if (ctx->clip_stack_len >= MU_CLIPSTACK_SIZE) return;
    ctx->clip_stack[ctx->clip_stack_len++] = ctx->clip;

    if (rect.x < ctx->clip.x) { rect.w -= ctx->clip.x - rect.x; rect.x = ctx->clip.x; }
    if (rect.y < ctx->clip.y) { rect.h -= ctx->clip.y - rect.y; rect.y = ctx->clip.y; }
    if (rect.x + rect.w > ctx->clip.x + ctx->clip.w) { rect.w = ctx->clip.x + ctx->clip.w - rect.x; }
    if (rect.y + rect.h > ctx->clip.y + ctx->clip.h) { rect.h = ctx->clip.y + ctx->clip.h - rect.y; }
    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;

    ctx->clip = rect;

    cmd = (struct mu_clip_command *)mu_jump_command(ctx, sizeof(struct mu_clip_command));
    if (cmd) {
        cmd->type = MU_COMMAND_CLIP;
        cmd->size = sizeof(struct mu_clip_command);
        cmd->rect = rect;
    }
}

void mu_pop_clip_rect(struct mu_context *ctx) {
    if (ctx->clip_stack_len > 0) {
        ctx->clip = ctx->clip_stack[--ctx->clip_stack_len];
    }
    {
        struct mu_clip_command *cmd = (struct mu_clip_command *)mu_jump_command(ctx, sizeof(struct mu_clip_command));
        if (cmd) {
            cmd->type = MU_COMMAND_CLIP;
            cmd->size = sizeof(struct mu_clip_command);
            cmd->rect = ctx->clip;
        }
    }
}

static int mu_mouse_over(struct mu_context *ctx, struct mu_rect r) {
    return ctx->mouse_x >= r.x && ctx->mouse_x < r.x + r.w &&
           ctx->mouse_y >= r.y && ctx->mouse_y < r.y + r.h;
}

static __attribute__((unused)) int mu_mouse_down(struct mu_context *ctx, int id) {
    (void)ctx;
    return ctx->mouse_down && id == ctx->id;
}

static int mu_mouse_hot(struct mu_context *ctx, int id) {
    return id == ctx->last_id;
}

static struct mu_pool_item *mu_find_pool_item(struct mu_context *ctx, int id) {
    for (int i = 0; i < ctx->container_pool_len; i++) {
        if (ctx->container_pool[i].id == id)
            return &ctx->container_pool[i];
    }
    return 0;
}

static struct mu_pool_item *mu_add_pool_item(struct mu_context *ctx, int id, struct mu_rect rect, int open) {
    struct mu_pool_item *item;
    if (ctx->container_pool_len >= MU_CONTAINERPOOL_SIZE) return 0;
    item = &ctx->container_pool[ctx->container_pool_len++];
    item->id = id;
    item->last_update = ctx->id;
    item->rect = rect;
    item->open = open;
    return item;
}

void mu_set_focus(struct mu_context *ctx, int id) {
    (void)ctx;
    (void)id;
}

int mu_get_focus(struct mu_context *ctx) {
    (void)ctx;
    return 0;
}

static void mu_draw_rect(struct mu_context *ctx, struct mu_rect rect, int color) {
    struct mu_rect_command *cmd = (struct mu_rect_command *)mu_jump_command(ctx, sizeof(struct mu_rect_command));
    if (cmd) {
        cmd->type = MU_COMMAND_RECT;
        cmd->size = sizeof(struct mu_rect_command);
        cmd->rect = rect;
        cmd->color = color;
    }
}

static void mu_draw_text(struct mu_context *ctx, struct mu_rect rect, const char *text, int len, int color) {
    int sz = (int)sizeof(struct mu_text_command) + len + 1;
    struct mu_text_command *cmd = (struct mu_text_command *)mu_jump_command(ctx, sz);
    if (cmd) {
        cmd->type = MU_COMMAND_TEXT;
        cmd->size = sz;
        cmd->rect = rect;
        cmd->color = color;
        memcpy(cmd->str, text, len);
        cmd->str[len] = '\0';
    }
}

static __attribute__((unused)) void mu_draw_icon(struct mu_context *ctx, struct mu_rect rect, int id) {
    struct mu_icon_command *cmd = (struct mu_icon_command *)mu_jump_command(ctx, sizeof(struct mu_icon_command));
    if (cmd) {
        cmd->type = MU_COMMAND_ICON;
        cmd->size = sizeof(struct mu_icon_command);
        cmd->rect = rect;
        cmd->id = id;
    }
}

static void mu_draw_frame(struct mu_context *ctx, struct mu_rect rect, int color) {
    ctx->draw_frame(ctx, rect, color);
    mu_draw_rect(ctx, rect, color);
}

static __attribute__((unused)) struct mu_rect mu_expand_rect(struct mu_rect rect, int n) {
    struct mu_rect r = {rect.x - n, rect.y - n, rect.w + n * 2, rect.h + n * 2};
    return r;
}

static __attribute__((unused)) struct mu_rect mu_intersect_rects(struct mu_rect r1, struct mu_rect r2) {
    int x1 = max(r1.x, r2.x);
    int y1 = max(r1.y, r2.y);
    int x2 = min(r1.x + r1.w, r2.x + r2.w);
    int y2 = min(r1.y + r1.h, r2.y + r2.h);
    struct mu_rect r = {x1, y1, max(0, x2 - x1), max(0, y2 - y1)};
    return r;
}

/* layout */
static void mu_layout_row_begin(struct mu_context *ctx, int items, const int *widths, int height) {
    struct mu_real_layout *layout;
    if (ctx->layout_stack_len >= MU_LAYOUTSTACK_SIZE) return;
    layout = &ctx->layout_stack[ctx->layout_stack_len++];
    layout->body[0] = (MU_REAL)ctx->clip.x;
    layout->body[1] = (MU_REAL)ctx->clip.y;
    layout->body[2] = (MU_REAL)ctx->clip.w;
    layout->body[3] = (MU_REAL)ctx->clip.h;
    layout->next[0] = layout->body[0];
    layout->next[1] = layout->body[1];
    layout->next[2] = 0;
    layout->next[3] = 0;
    layout->position[0] = layout->body[0];
    layout->position[1] = layout->body[1];
    layout->position[2] = 0;
    layout->position[3] = 0;
    layout->up = 0;
    layout->max_x = 0;
    layout->max_y = 0;
    layout->width = items > 0 && widths ? widths[0] : 0;
    layout->height = height;
    layout->expand = 0;
    layout->item_index = 0;
    layout->next_row = 0;
}

static int mu_layout_row_end(struct mu_context *ctx) {
    if (ctx->layout_stack_len <= 0) return 0;
    ctx->layout_stack_len--;
    return 1;
}

void mu_layout_row(struct mu_context *ctx, int items, const int *widths, int height) {
    if (ctx->layout_stack_len > 0) mu_layout_row_end(ctx);
    mu_layout_row_begin(ctx, items, widths, height);
}

void mu_layout_width(struct mu_context *ctx, int width) {
    if (ctx->layout_stack_len > 0)
        ctx->layout_stack[ctx->layout_stack_len - 1].width = width;
}

void mu_layout_height(struct mu_context *ctx, int height) {
    if (ctx->layout_stack_len > 0)
        ctx->layout_stack[ctx->layout_stack_len - 1].height = height;
}

void mu_layout_begin_column(struct mu_context *ctx) {
    mu_layout_row_begin(ctx, 0, 0, 0);
}

void mu_layout_end_column(struct mu_context *ctx) {
    mu_layout_row_end(ctx);
}

void mu_layout_set_next(struct mu_context *ctx, struct mu_rect r, int relative) {
    struct mu_real_layout *layout;
    if (ctx->layout_stack_len <= 0) return;
    layout = &ctx->layout_stack[ctx->layout_stack_len - 1];
    layout->next[0] = (MU_REAL)(relative ? layout->body[0] + r.x : r.x);
    layout->next[1] = (MU_REAL)(relative ? layout->body[1] + r.y : r.y);
    layout->next[2] = (MU_REAL)r.w;
    layout->next[3] = (MU_REAL)r.h;
}

struct mu_rect mu_layout_next(struct mu_context *ctx) {
    struct mu_real_layout *layout;
    struct mu_rect r;
    int item_w, item_h;

    if (ctx->layout_stack_len <= 0) {
        r.x = ctx->clip.x; r.y = ctx->clip.y; r.w = 0; r.h = 0;
        return r;
    }

    layout = &ctx->layout_stack[ctx->layout_stack_len - 1];

    if (layout->next_row) {
        layout->next[0] = layout->body[0] + layout->position[2];
        layout->next[1] = layout->body[1] + layout->position[3];
        layout->next[2] = 0;
        layout->next[3] = 0;
        layout->next_row = 0;
    }

    if (layout->width == 0) {
        item_w = (int)(layout->body[2] - layout->position[2] - layout->position[0] + layout->body[0]);
        item_h = layout->height > 0 ? layout->height : ctx->style.gap;
    } else {
        item_w = layout->width;
        item_h = layout->height > 0 ? layout->height : ctx->style.gap;
    }

    if (layout->next[2] > 0 || layout->next[3] > 0) {
        item_w = (int)layout->next[2];
        item_h = (int)layout->next[3];
        layout->next[2] = 0;
        layout->next[3] = 0;
    }

    r.x = (int)(layout->next[0] + (layout->position[0] - layout->body[0]));
    r.y = (int)(layout->next[1] + (layout->position[1] - layout->body[1]));
    r.w = item_w;
    r.h = item_h;

    layout->position[0] += item_w + ctx->style.spacing;

    if (item_w > 0) {
        if (r.x + r.w > layout->max_x) layout->max_x = r.x + r.w;
    }
    if (item_h > 0) {
        if (r.y + r.h > layout->max_y) layout->max_y = r.y + r.h;
    }

    layout->item_index++;
    return r;
}

static void mu_update_control(struct mu_context *ctx, int id, struct mu_rect rect, int opt) {
    int mouseover = mu_mouse_over(ctx, rect);

    if (ctx->mouse_pressed && mouseover) {
        ctx->last_id = id;
    }
    if (!ctx->mouse_down && ctx->last_id == id) {
        ctx->last_id = 0;
    }

    (void)opt;
}

/* container management */
static struct mu_container *mu_get_container(struct mu_context *ctx, int id, int opt) {
    struct mu_pool_item *pool_item;
    struct mu_container *c;
    int is_open;

    pool_item = mu_find_pool_item(ctx, id);
    if (pool_item) {
        is_open = pool_item->open;
    } else {
        is_open = !(opt & MU_OPT_CLOSED);
    }

    if (ctx->root_count >= MU_ROOTLIST_SIZE) return 0;
    c = (struct mu_container *)default_alloc(sizeof(struct mu_container));
    if (!c) return 0;
    memset(c, 0, sizeof(*c));
    c->open = is_open;
    c->id = id;
    c->head = ctx->root_count;
    ctx->root_list[ctx->root_count++] = c;

    if (!pool_item) {
        mu_add_pool_item(ctx, id, (struct mu_rect){0, 0, 0, 0}, is_open);
    }

    return c;
}

void mu_begin_window(struct mu_context *ctx, const char *title, struct mu_rect rect, int opt) {
    int id = mu_get_next_id(ctx, 1);
    struct mu_container *cnt;

    if (title) {
        int len = str_len(title);
        for (int i = 0; i < len; i++) id += title[i];
    }

    cnt = mu_get_container(ctx, id, opt);
    if (!cnt) return;

    cnt->title = title;
    cnt->title_len = title ? str_len(title) : 0;
    cnt->body = rect;

    if (!(opt & MU_OPT_AUTOSIZE)) {
        if (cnt->body.w < 0) cnt->body.w = ctx->style.margin_left + ctx->style.margin_right;
        if (cnt->body.h < 0) cnt->body.h = ctx->style.margin_top + ctx->style.margin_bottom + ctx->style.title_height;
    }

    cnt->scroll.x = 0;
    cnt->scroll.y = 0;

    if (ctx->container_stack_len >= MU_CONTAINERSTACK_SIZE) return;
    ctx->container_stack[ctx->container_stack_len++] = cnt;

    if (!(opt & MU_OPT_NOCLOSE)) {
        struct mu_rect close_btn = {rect.x + rect.w - 16, rect.y + 2, 14, 14};
        int close_id = id + 1;
        mu_update_control(ctx, close_id, close_btn, 0);
        if (ctx->mouse_pressed && mu_mouse_over(ctx, close_btn)) {
            cnt->open = 0;
        }
    }

    if (!(opt & MU_OPT_NOTITLE)) {
        if (ctx->mouse_pressed && mu_mouse_over(ctx, (struct mu_rect){rect.x, rect.y, rect.w, ctx->style.title_height})) {
            int drag = 1;
            (void)drag;
        }
    }

    ctx->clip = cnt->body;
    mu_push_clip_rect(ctx, cnt->body);

    mu_layout_row_begin(ctx, 0, 0, 0);
}

void mu_end_window(struct mu_context *ctx) {
    mu_layout_row_end(ctx);
    mu_pop_clip_rect(ctx);
    if (ctx->container_stack_len > 0) ctx->container_stack_len--;
}

void mu_begin_panel(struct mu_context *ctx, const char *name) {
    int id = mu_get_next_id(ctx, 1);
    struct mu_container *cnt;

    if (name) {
        int len = str_len(name);
        for (int i = 0; i < len; i++) id += name[i];
    }

    cnt = mu_get_container(ctx, id, 0);
    if (!cnt) return;

    cnt->title = name;
    cnt->title_len = name ? str_len(name) : 0;

    if (ctx->container_stack_len >= MU_CONTAINERSTACK_SIZE) return;
    cnt->body = ctx->clip;
    ctx->container_stack[ctx->container_stack_len++] = cnt;
    mu_push_clip_rect(ctx, cnt->body);
    mu_layout_row_begin(ctx, 0, 0, 0);
}

void mu_end_panel(struct mu_context *ctx) {
    mu_layout_row_end(ctx);
    mu_pop_clip_rect(ctx);
    if (ctx->container_stack_len > 0) ctx->container_stack_len--;
}

void mu_begin_treenode(struct mu_context *ctx, int id, const char *label, int open) {
    (void)ctx;
    (void)id;
    (void)label;
    (void)open;
}

int mu_end_treenode(struct mu_context *ctx) {
    (void)ctx;
    return 0;
}

/* widgets */
int mu_button(struct mu_context *ctx, const char *label) {
    int id = mu_get_next_id(ctx, 1);
    struct mu_rect r = mu_layout_next(ctx);
    int res = 0;
    int color;

    if (!label) return 0;

    mu_update_control(ctx, id, r, 0);

    if (ctx->mouse_pressed && mu_mouse_hot(ctx, id)) {
        res = 1;
    }

    if (mu_mouse_hot(ctx, id)) {
        color = ctx->mouse_down ? MU_COLOR_BUTTONFOCUS : MU_COLOR_BUTTONHOVER;
    } else {
        color = MU_COLOR_BUTTON;
    }
    mu_draw_frame(ctx, r, color);

    if (label[0]) {
        struct mu_rect text_r;
        int tw = ctx->text_width(ctx, label, str_len(label));
        text_r.x = r.x + (r.w - tw) / 2;
        text_r.y = r.y + (r.h - ctx->text_height(ctx, label, str_len(label))) / 2;
        text_r.w = tw;
        text_r.h = ctx->text_height(ctx, label, str_len(label));
        mu_draw_text(ctx, text_r, label, str_len(label), MU_COLOR_TEXT);
    }

    return res;
}

int mu_checkbox(struct mu_context *ctx, const char *label, int *value) {
    int id = mu_get_next_id(ctx, 1);
    struct mu_rect r = mu_layout_next(ctx);
    int res = 0;

    if (!label || !value) return 0;

    mu_update_control(ctx, id, r, 0);

    if (ctx->mouse_pressed && mu_mouse_hot(ctx, id)) {
        *value = !*value;
        res = 1;
    }

    {
        int box_size = r.h;
        struct mu_rect box = {r.x, r.y, box_size, box_size};
        int color = mu_mouse_hot(ctx, id) ? (ctx->mouse_down ? MU_COLOR_BASEFOCUS : MU_COLOR_BASEHOVER) : MU_COLOR_BASE;
        mu_draw_frame(ctx, box, color);
        if (*value) {
            struct mu_rect check = {box.x + 3, box.y + 3, box.w - 6, box.h - 6};
            mu_draw_rect(ctx, check, MU_COLOR_TEXT);
        }
    }

    if (label[0]) {
        struct mu_rect text_r = {r.x + r.h + ctx->style.spacing, r.y, ctx->text_width(ctx, label, str_len(label)), ctx->text_height(ctx, label, str_len(label))};
        mu_draw_text(ctx, text_r, label, str_len(label), MU_COLOR_TEXT);
    }

    return res;
}

int mu_textbox(struct mu_context *ctx, char *buf, int bufsz, int flags) {
    int id = mu_get_next_id(ctx, 1);
    struct mu_rect r = mu_layout_next(ctx);
    int res = 0;

    (void)flags;
    if (!buf || bufsz <= 0) return 0;

    mu_update_control(ctx, id, r, 0);

    {
        int color = mu_mouse_hot(ctx, id) ? (ctx->mouse_down ? MU_COLOR_BASEFOCUS : MU_COLOR_BASEHOVER) : MU_COLOR_BASE;
        mu_draw_frame(ctx, r, color);
    }

    {
        int len = str_len(buf);
        struct mu_rect text_r = {r.x + ctx->style.padding, r.y + (r.h - ctx->text_height(ctx, buf, len)) / 2, ctx->text_width(ctx, buf, len), ctx->text_height(ctx, buf, len)};
        mu_draw_text(ctx, text_r, buf, len, MU_COLOR_TEXT);
    }

    return res;
}

int mu_slider(struct mu_context *ctx, MU_REAL *value, MU_REAL low, MU_REAL high, MU_REAL step, const char *fmt, int flags) {
    int id = mu_get_next_id(ctx, 1);
    struct mu_rect r = mu_layout_next(ctx);
    int res = 0;
    MU_REAL v, w, thumb_pos;
    int thumb_w = ctx->style.thumb_size;
    (void)fmt;
    (void)flags;

    if (!value) return 0;

    mu_update_control(ctx, id, r, 0);

    v = *value;
    if (high - low > 0) {
        w = (MU_REAL)(r.w - thumb_w);
        if (ctx->mouse_down && mu_mouse_hot(ctx, id)) {
            v = low + ((MU_REAL)(ctx->mouse_x - r.x - thumb_w / 2) / w) * (high - low);
            v = v < low ? low : (v > high ? high : v);
            if (step > 0) v = (MU_REAL)((int)((v - low) / step + (MU_REAL)0.5f) * step + low);
            *value = v;
            res = 1;
        }
    }

    {
        int color = mu_mouse_hot(ctx, id) ? (ctx->mouse_down ? MU_COLOR_BASEFOCUS : MU_COLOR_BASEHOVER) : MU_COLOR_BASE;
        mu_draw_frame(ctx, r, color);
    }

    if (high - low > 0) {
        w = (MU_REAL)(r.w - thumb_w);
        thumb_pos = (v - low) / (high - low) * w;
    } else {
        thumb_pos = 0;
    }
    {
        struct mu_rect thumb = {r.x + (int)thumb_pos, r.y, thumb_w, r.h};
        mu_draw_rect(ctx, thumb, MU_COLOR_SCROLLTHUMB);
    }

    return res;
}

int mu_number(struct mu_context *ctx, int *value, int step) {
    char buf[32];
    int res;

    if (value == 0) return 0;
    buf[0] = '\0';
    str_append(buf, "0", sizeof(buf));
    res = mu_textbox(ctx, buf, sizeof(buf), 0);
    if (res && buf[0] != '\0') {
        int v = 0;
        int sign = 1;
        char *p = buf;
        if (*p == '-') { sign = -1; p++; }
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        *value = v * sign;
    }
    (void)step;
    return res;
}

void mu_header(struct mu_context *ctx, const char *label) {
    int id = mu_get_next_id(ctx, 1);
    struct mu_rect r = mu_layout_next(ctx);

    mu_update_control(ctx, id, r, 0);
    {
        int color = mu_mouse_hot(ctx, id) ? (ctx->mouse_down ? MU_COLOR_BUTTONFOCUS : MU_COLOR_BUTTONHOVER) : MU_COLOR_BUTTON;
        mu_draw_frame(ctx, r, color);
    }
    if (label && label[0]) {
        struct mu_rect text_r = {r.x + ctx->style.padding, r.y + (r.h - ctx->text_height(ctx, label, str_len(label))) / 2, ctx->text_width(ctx, label, str_len(label)), ctx->text_height(ctx, label, str_len(label))};
        mu_draw_text(ctx, text_r, label, str_len(label), MU_COLOR_TEXT);
    }
}

int mu_treenode(struct mu_context *ctx, const char *label) {
    int id = mu_get_next_id(ctx, 1);
    struct mu_pool_item *item;
    struct mu_rect r = mu_layout_next(ctx);

    mu_update_control(ctx, id, r, 0);

    item = mu_find_pool_item(ctx, id);
    if (!item) {
        mu_add_pool_item(ctx, id, r, 1);
        item = mu_find_pool_item(ctx, id);
    }

    if (ctx->mouse_pressed && mu_mouse_hot(ctx, id)) {
        if (item) item->open = !item->open;
    }

    {
        int color = mu_mouse_hot(ctx, id) ? (ctx->mouse_down ? MU_COLOR_BUTTONFOCUS : MU_COLOR_BUTTONHOVER) : MU_COLOR_BUTTON;
        mu_draw_frame(ctx, r, color);
    }

    if (label && label[0]) {
        struct mu_rect arrow = {r.x + 2, r.y + 2, r.h - 4, r.h - 4};
        int arrow_color = MU_COLOR_TEXT;
        mu_draw_rect(ctx, arrow, arrow_color);
        struct mu_rect text_r = {r.x + ctx->style.indent, r.y, ctx->text_width(ctx, label, str_len(label)), ctx->text_height(ctx, label, str_len(label))};
        mu_draw_text(ctx, text_r, label, str_len(label), MU_COLOR_TEXT);
    }

    return item ? item->open : 0;
}

void mu_text(struct mu_context *ctx, const char *text) {
    if (!text) return;
    struct mu_rect r = mu_layout_next(ctx);
    int len = str_len(text);
    mu_draw_text(ctx, (struct mu_rect){r.x, r.y, ctx->text_width(ctx, text, len), ctx->text_height(ctx, text, len)}, text, len, MU_COLOR_TEXT);
}

void mu_label(struct mu_context *ctx, const char *label) {
    if (!label) return;
    int len = str_len(label);
    struct mu_rect r = mu_layout_next(ctx);
    mu_draw_text(ctx, (struct mu_rect){r.x, r.y, ctx->text_width(ctx, label, len), ctx->text_height(ctx, label, len)}, label, len, MU_COLOR_TEXT);
}

int mu_draw_cmd_list(struct mu_context *ctx, uint8_t *screen, int sw, int sh, int spitch) {
    (void)screen;
    (void)sw;
    (void)sh;
    (void)spitch;
    struct mu_command *cmd = ctx->cmd_head;
    while (cmd && cmd->type) {
        int next_size;
        switch (cmd->type) {
            case MU_COMMAND_JUMP:
                break;
            case MU_COMMAND_CLIP: {
                struct mu_clip_command *c = (struct mu_clip_command *)cmd;
                clip_push(c->rect.x, c->rect.y, c->rect.w, c->rect.h);
                break;
            }
            case MU_COMMAND_RECT: {
                struct mu_rect_command *c = (struct mu_rect_command *)cmd;
                struct mu_color_value *col = &ctx->style.colors[c->color];
                uint8_t vga_color = 0;
                int best_diff = 999999;
                for (int i = 0; i < 16; i++) {
                    int dr = (int)col->r - (int)(i & 4 ? 255 : 0);
                    int dg = (int)col->g - (int)(i & 2 ? 255 : 0);
                    int db = (int)col->b - (int)(i & 1 ? 255 : 0);
                    int diff = dr*dr + dg*dg + db*db;
                    if (diff < best_diff) { best_diff = diff; vga_color = (uint8_t)i; }
                }
                sys_rect(c->rect.x, c->rect.y, c->rect.w, c->rect.h, vga_color);
                break;
            }
            case MU_COMMAND_TEXT: {
                struct mu_text_command *c = (struct mu_text_command *)cmd;
                uint8_t tc = MU_COLOR_TEXT;
                struct mu_color_value *col = &ctx->style.colors[tc];
                uint8_t vga_color = 0;
                int best_diff = 999999;
                for (int i = 0; i < 16; i++) {
                    int dr = (int)col->r - (int)(i & 4 ? 255 : 0);
                    int dg = (int)col->g - (int)(i & 2 ? 255 : 0);
                    int db = (int)col->b - (int)(i & 1 ? 255 : 0);
                    int diff = dr*dr + dg*dg + db*db;
                    if (diff < best_diff) { best_diff = diff; vga_color = (uint8_t)i; }
                }
                sys_text(c->rect.x, c->rect.y, vga_color, c->str);
                break;
            }
            case MU_COMMAND_ICON: {
                struct mu_icon_command *c = (struct mu_icon_command *)cmd;
                (void)c;
                break;
            }
            default:
                break;
        }
        next_size = cmd->size;
        cmd = (struct mu_command *)((char *)cmd + next_size);
    }
    return 0;
}
