#include <userland/applications/include/taskmgr.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_TASKMGR_WINDOW = {30, 30, 260, 160};
static const int TASKMGR_ROW_HEIGHT = 16;

static void append_uint(char *buf, unsigned v) {
    /* append decimal representation to end of buf */
    char tmp[12];
    int pos = 0;
    if (v == 0) {
        tmp[pos++] = '0';
    } else {
        while (v > 0 && pos < (int)sizeof(tmp) - 1) {
            tmp[pos++] = '0' + (v % 10);
            v /= 10;
        }
        /* reverse */
        for (int i = 0; i < pos/2; ++i) {
            char c = tmp[i];
            tmp[i] = tmp[pos-1-i];
            tmp[pos-1-i] = c;
        }
    }
    tmp[pos] = '\0';
    /* append to buf */
    while (*buf) ++buf;
    for (int i = 0; tmp[i]; ++i) *buf++ = tmp[i];
    *buf = '\0';
}

void taskmgr_init_state(struct taskmgr_state *tm) {
    tm->window = DEFAULT_TASKMGR_WINDOW;
}

static struct rect taskmgr_row_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect r = {tm->window.x + 6, tm->window.y + 24 + (visible_index * TASKMGR_ROW_HEIGHT),
                     tm->window.w - 12, TASKMGR_ROW_HEIGHT - 2};
    return r;
}

static struct rect taskmgr_close_button_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect row = taskmgr_row_rect(tm, visible_index);
    struct rect r = {row.x + row.w - 64, row.y + 1, 58, row.h - 2};
    return r;
}

void taskmgr_draw_window(struct taskmgr_state *tm,
                          struct window *wins,
                          int win_count,
                          uint32_t ticks,
                          int active,
                          int min_hover,
                          int max_hover,
                          int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {tm->window.x + 4, tm->window.y + 18, tm->window.w - 8, tm->window.h - 22};

    draw_window_frame(&tm->window, "TASKS", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);

    int visible_index = 0;
    for (int i = 0; i < win_count; ++i) {
        struct rect row;
        struct rect close_button;
        if (!wins[i].active) continue;

        row = taskmgr_row_rect(tm, visible_index);
        close_button = taskmgr_close_button_rect(tm, visible_index);
        ui_draw_inset(&row, ui_color_canvas());
        ui_draw_button(&close_button, "Finalizar", UI_BUTTON_DANGER, 0);

        char line[64];
        int len = 0;
        line[len++] = '0' + (i % 10);
        line[len++] = ':';
        line[len++] = ' ';
        const char *name = "?";
        switch (wins[i].type) {
        case APP_TERMINAL: name = "TERM"; break;
        case APP_CLOCK: name = "CLK"; break;
        case APP_FILEMANAGER: name = "FM"; break;
        case APP_EDITOR: name = "EDIT"; break;
        case APP_TASKMANAGER: name = "TM"; break;
        case APP_CALCULATOR: name = "CALC"; break;
        case APP_SKETCHPAD: name = "DRAW"; break;
        case APP_SNAKE: name = "SNAKE"; break;
        case APP_TETRIS: name = "TETRAX"; break;
        case APP_PACMAN: name = "PACPAC"; break;
        case APP_SPACE_INVADERS: name = "ALIENS"; break;
        case APP_PONG: name = "PONG"; break;
        case APP_DONKEY_KONG: name = "MONKEY"; break;
        case APP_BRICK_RACE: name = "RACE"; break;
        case APP_FLAP_BIRB: name = "FLAP"; break;
        case APP_DOOM: name = "DOOM"; break;
        case APP_PERSONALIZE: name = "PERS"; break;
        default: name = "???"; break;
        }
        while (*name && len < (int)sizeof(line) - 1) {
            line[len++] = *name++;
        }
        line[len++] = ' ';
        /* uptime in seconds */
        unsigned uptime = (ticks - wins[i].start_ticks) / 100u;
        char numbuf[16] = "";
        append_uint(numbuf, uptime);
        for (int j = 0; numbuf[j] && len < (int)sizeof(line) - 1; ++j) {
            line[len++] = numbuf[j];
        }
        line[len] = '\0';
        sys_text(row.x + 4, row.y + 4, theme->text, line);
        ++visible_index;
    }
}

int taskmgr_hit_test_close(const struct taskmgr_state *tm,
                           const struct window *wins,
                           int win_count,
                           int x,
                           int y) {
    int visible_index = 0;

    for (int i = 0; i < win_count; ++i) {
        struct rect close_button;

        if (!wins[i].active) {
            continue;
        }

        close_button = taskmgr_close_button_rect(tm, visible_index);
        if (point_in_rect(&close_button, x, y)) {
            return i;
        }
        ++visible_index;
    }

    return -1;
}
