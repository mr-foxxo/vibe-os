#include "taskmgr.h"
#include "ui.h"
#include "syscalls.h"

static const struct rect DEFAULT_TASKMGR_WINDOW = {30, 30, 260, 160};

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

void taskmgr_draw_window(struct taskmgr_state *tm,
                          struct window *wins,
                          int win_count,
                          uint32_t ticks) {
    draw_window_frame(&tm->window, "TASKS", 0);
    sys_rect(tm->window.x + 4, tm->window.y + 18,
             tm->window.w - 8, tm->window.h - 22, 0);

    int y = tm->window.y + 24;
    for (int i = 0; i < win_count; ++i) {
        if (!wins[i].active) continue;
        char line[64];
        int len = 0;
        /* index */
        line[len++] = '0' + (i % 10);
        line[len++] = ':';
        line[len++] = ' ';
        const char *name = "?";
        switch (wins[i].type) {
        case APP_TERMINAL: name = "TERM"; break;
        case APP_CLOCK: name = "CLK"; break;
        case APP_FILEMANAGER: name = "FM"; break;
        case APP_TASKMANAGER: name = "TM"; break;
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
        sys_text(tm->window.x + 6, y, 15, line);
        y += 8;
    }
}
