#include <userland/modules/include/mu_renderer.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>
#include <userland/modules/include/icon_theme.h>
#include <string.h>
#include <stdlib.h>

static int mu_renderer_text_width(struct mu_context *ctx, const char *text, int len) {
    (void)ctx;
    if (!text || len <= 0) return 0;
    return len * 6;
}

static int mu_renderer_text_height(struct mu_context *ctx, const char *text, int len) {
    (void)ctx;
    (void)text;
    (void)len;
    return 7;
}

__attribute__((unused))
static uint8_t mu_color_to_vga(struct mu_color_value c) {
    uint8_t r = c.r > 127 ? 4 : 0;
    uint8_t g = c.g > 127 ? 2 : 0;
    uint8_t b = c.b > 127 ? 1 : 0;
    uint8_t bright = (c.r > 192 || c.g > 192 || c.b > 192) ? 8 : 0;
    uint8_t index = r | g | b;
    if (index == 0 && (c.r > 64 || c.g > 64 || c.b > 64)) {
        uint8_t gray = (uint8_t)((c.r * 30 + c.g * 59 + c.b * 11) / 100);
        index = gray > 127 ? 7 : 8;
    } else {
        if (index == 0 && bright == 0) {
            if (c.r > 32 || c.g > 32 || c.b > 32) index = 8;
        }
    }
    return index | bright;
}

__attribute__((unused))
static int mu_is_transparent(struct mu_color_value c) {
    return c.a < 128;
}

static int mu_map_color(int mu_color_idx) {
    static const uint8_t mu_to_vga[MU_COLOR_MAX] = {
        15, 8, 0, 1, 15, 7, 3, 11, 10, 8, 7, 15, 8, 7
    };
    if (mu_color_idx >= 0 && mu_color_idx < MU_COLOR_MAX)
        return mu_to_vga[mu_color_idx];
    return 0;
}

static void mu_draw_frame_callback(struct mu_context *ctx, struct mu_rect rect, int color) {
    (void)ctx;
    uint8_t vga_color = (uint8_t)mu_map_color(color);
    sys_rect(rect.x, rect.y, rect.w, rect.h, vga_color);
}

void mu_renderer_init(struct mu_context *ctx) {
    ctx->text_width = mu_renderer_text_width;
    ctx->text_height = mu_renderer_text_height;
    ctx->draw_frame = mu_draw_frame_callback;

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

    struct mu_color_value *c = ctx->style.colors;
    c[MU_COLOR_TEXT].r = 224; c[MU_COLOR_TEXT].g = 224; c[MU_COLOR_TEXT].b = 224; c[MU_COLOR_TEXT].a = 255;
    c[MU_COLOR_BORDER].r = 100; c[MU_COLOR_BORDER].g = 100; c[MU_COLOR_BORDER].b = 100; c[MU_COLOR_BORDER].a = 255;
    c[MU_COLOR_WINDOWBG].r = 48; c[MU_COLOR_WINDOWBG].g = 48; c[MU_COLOR_WINDOWBG].b = 48; c[MU_COLOR_WINDOWBG].a = 255;
    c[MU_COLOR_TITLEBG].r = 0; c[MU_COLOR_TITLEBG].g = 60; c[MU_COLOR_TITLEBG].b = 120; c[MU_COLOR_TITLEBG].a = 255;
    c[MU_COLOR_TITLETEXT].r = 224; c[MU_COLOR_TITLETEXT].g = 224; c[MU_COLOR_TITLETEXT].b = 224; c[MU_COLOR_TITLETEXT].a = 255;
    c[MU_COLOR_PANELBG].r = 40; c[MU_COLOR_PANELBG].g = 40; c[MU_COLOR_PANELBG].b = 40; c[MU_COLOR_PANELBG].a = 255;
    c[MU_COLOR_BUTTON].r = 72; c[MU_COLOR_BUTTON].g = 72; c[MU_COLOR_BUTTON].b = 72; c[MU_COLOR_BUTTON].a = 255;
    c[MU_COLOR_BUTTONHOVER].r = 96; c[MU_COLOR_BUTTONHOVER].g = 96; c[MU_COLOR_BUTTONHOVER].b = 96; c[MU_COLOR_BUTTONHOVER].a = 255;
    c[MU_COLOR_BUTTONFOCUS].r = 120; c[MU_COLOR_BUTTONFOCUS].g = 120; c[MU_COLOR_BUTTONFOCUS].b = 120; c[MU_COLOR_BUTTONFOCUS].a = 255;
    c[MU_COLOR_BASE].r = 56; c[MU_COLOR_BASE].g = 56; c[MU_COLOR_BASE].b = 56; c[MU_COLOR_BASE].a = 255;
    c[MU_COLOR_BASEHOVER].r = 80; c[MU_COLOR_BASEHOVER].g = 80; c[MU_COLOR_BASEHOVER].b = 80; c[MU_COLOR_BASEHOVER].a = 255;
    c[MU_COLOR_BASEFOCUS].r = 100; c[MU_COLOR_BASEFOCUS].g = 100; c[MU_COLOR_BASEFOCUS].b = 100; c[MU_COLOR_BASEFOCUS].a = 255;
    c[MU_COLOR_SCROLLBASE].r = 40; c[MU_COLOR_SCROLLBASE].g = 40; c[MU_COLOR_SCROLLBASE].b = 40; c[MU_COLOR_SCROLLBASE].a = 255;
    c[MU_COLOR_SCROLLTHUMB].r = 96; c[MU_COLOR_SCROLLTHUMB].g = 96; c[MU_COLOR_SCROLLTHUMB].b = 96; c[MU_COLOR_SCROLLTHUMB].a = 255;
}

static void mu_renderer_exec(struct mu_context *ctx) {
    struct mu_command *cmd = ctx->cmd_head;

    while (cmd && cmd->type) {
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
                mu_draw_frame_callback(ctx, c->rect, c->color);
                break;
            }

            case MU_COMMAND_TEXT: {
                struct mu_text_command *c = (struct mu_text_command *)cmd;
                uint8_t vga = mu_map_color(c->color);
                sys_text(c->rect.x, c->rect.y, vga, c->str);
                break;
            }

            case MU_COMMAND_ICON: {
                struct mu_icon_command *c = (struct mu_icon_command *)cmd;
                (void)c;
                break;
            }
        }
        cmd = (struct mu_command *)((char *)cmd + cmd->size);
    }
}

void mu_renderer_draw(struct mu_context *ctx) {
    mu_renderer_exec(ctx);
}
