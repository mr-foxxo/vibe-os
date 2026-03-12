#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/terminal.h>
#include <userland/modules/include/dirty_rects.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/ui_cursor.h>

/* Global screen resolution vars - initialized at startup */
uint32_t SCREEN_WIDTH = 320;
uint32_t SCREEN_HEIGHT = 200;
uint32_t SCREEN_PITCH = 320;
struct video_mode g_screen_mode = {0};
static struct desktop_theme g_theme = {3, 7, 8, 7, 9, 0};

void ui_init(void) {
    /* Query actual screen resolution from kernel */
    if (sys_gfx_info(&g_screen_mode) == 0) {
        SCREEN_WIDTH = g_screen_mode.width;
        SCREEN_HEIGHT = g_screen_mode.height;
        SCREEN_PITCH = g_screen_mode.pitch;
    }
    
    /* Initialize subsystems */
    dirty_init();
    clip_init();
    cursor_init();
}

const struct desktop_theme *ui_theme_get(void) {
    return &g_theme;
}

void ui_theme_set_slot(enum theme_slot slot, uint8_t color) {
    switch (slot) {
    case THEME_SLOT_BACKGROUND:
        g_theme.background = color;
        break;
    case THEME_SLOT_MENU:
        g_theme.menu = color;
        break;
    case THEME_SLOT_MENU_BUTTON:
        g_theme.menu_button = color;
        break;
    case THEME_SLOT_TASKBAR:
        g_theme.taskbar = color;
        break;
    case THEME_SLOT_WINDOW:
        g_theme.window = color;
        break;
    case THEME_SLOT_TEXT:
        g_theme.text = color;
        break;
    default:
        break;
    }
}

const char *ui_theme_slot_name(enum theme_slot slot) {
    switch (slot) {
    case THEME_SLOT_BACKGROUND: return "Plano";
    case THEME_SLOT_MENU: return "Menu";
    case THEME_SLOT_MENU_BUTTON: return "Botao";
    case THEME_SLOT_TASKBAR: return "Barra";
    case THEME_SLOT_WINDOW: return "Janela";
    case THEME_SLOT_TEXT: return "Texto";
    default: return "Tema";
    }
}

static void draw_panel(const struct rect *r, uint8_t face, uint8_t light, uint8_t dark) {
    sys_rect(r->x, r->y, r->w, r->h, face);
    sys_rect(r->x, r->y, r->w, 1, light);
    sys_rect(r->x, r->y, 1, r->h, light);
    sys_rect(r->x, r->y + r->h - 1, r->w, 1, dark);
    sys_rect(r->x + r->w - 1, r->y, 1, r->h, dark);
}

static const char *app_caption(enum app_type type) {
    switch (type) {
    case APP_TERMINAL: return "Terminal";
    case APP_CLOCK: return "Relogio";
    case APP_FILEMANAGER: return "Arquivos";
    case APP_EDITOR: return "Editor";
    case APP_TASKMANAGER: return "Tasks";
    case APP_PERSONALIZE: return "Cores";
    default: return "App";
    }
}

static void draw_task_button(const struct rect *r, const char *label, int active) {
    uint8_t face = active ? g_theme.window : g_theme.menu_button;
    draw_panel(r, face, 15, 0);
    sys_text(r->x + 5, r->y + 4, g_theme.text, label);
}

void draw_window_frame(const struct rect *w, const char *title,
                       int active,
                       int min_hover,
                       int max_hover,
                       int close_hover) {
    const struct rect min = window_min_button(w);
    const struct rect max = window_max_button(w);
    const struct rect close = window_close_button(w);
    const struct rect outer = {w->x, w->y, w->w, w->h};
    const struct rect title_bar = {w->x + 2, w->y + 2, w->w - 4, 12};

    draw_panel(&outer, 7, 15, 0);
    sys_rect(title_bar.x, title_bar.y, title_bar.w, title_bar.h, active ? g_theme.window : 8);
    sys_text(w->x + 6, w->y + 4, g_theme.text, title);

    draw_panel(&min, min_hover ? 15 : g_theme.menu_button, 15, 0);
    draw_panel(&max, max_hover ? 15 : g_theme.menu_button, 15, 0);
    draw_panel(&close, close_hover ? 12 : g_theme.menu_button, 15, 0);
    sys_text(min.x + 3, min.y + 2, g_theme.text, "-");
    sys_text(max.x + 3, max.y + 2, g_theme.text, "0");
    sys_text(close.x + 3, close.y + 2, g_theme.text, "X");
}

static void draw_start_menu(int taskbar_y,
                            int terminal_item_hover,
                            int clock_item_hover,
                            int filemgr_item_hover,
                            int editor_item_hover,
                            int taskmgr_item_hover,
                            int logout_item_hover) {
    const struct rect menu_rect = {2, taskbar_y - 134, 158, 132};
    const struct rect blue_strip = {menu_rect.x + 2, menu_rect.y + 2, 22, menu_rect.h - 4};
    const struct rect terminal_item = {menu_rect.x + 28, menu_rect.y + 8, 126, 16};
    const struct rect clock_item = {menu_rect.x + 28, menu_rect.y + 26, 126, 16};
    const struct rect filemgr_item = {menu_rect.x + 28, menu_rect.y + 44, 126, 16};
    const struct rect editor_item = {menu_rect.x + 28, menu_rect.y + 62, 126, 16};
    const struct rect taskmgr_item = {menu_rect.x + 28, menu_rect.y + 80, 126, 16};
    const struct rect logout_item = {menu_rect.x + 28, menu_rect.y + 106, 126, 16};

    draw_panel(&menu_rect, g_theme.menu, 15, 0);
    sys_rect(blue_strip.x, blue_strip.y, blue_strip.w, blue_strip.h, 9);
    sys_text(blue_strip.x + 4, blue_strip.y + 6, 15, "V");
    sys_text(blue_strip.x + 4, blue_strip.y + 18, 15, "I");
    sys_text(blue_strip.x + 4, blue_strip.y + 30, 15,  "B");
    sys_text(blue_strip.x + 4, blue_strip.y + 42, 15, "E");

    draw_task_button(&terminal_item, "Terminal", terminal_item_hover);
    draw_task_button(&clock_item, "Relogio", clock_item_hover);
    draw_task_button(&filemgr_item, "Arquivos", filemgr_item_hover);
    draw_task_button(&editor_item, "Editor", editor_item_hover);
    draw_task_button(&taskmgr_item, "Tasks", taskmgr_item_hover);
    sys_rect(menu_rect.x + 28, menu_rect.y + 100, 126, 1, 8);
    draw_task_button(&logout_item, "Encerrar sessao", logout_item_hover);
}

static void draw_taskbar(const struct window *wins, int win_count, int focused, int start_hover) {
    const int taskbar_y = (int)SCREEN_HEIGHT - 22;
    struct rect start_button = {4, taskbar_y + 3, 56, 16};
    int x = 66;

    sys_rect(0, taskbar_y, (int)SCREEN_WIDTH, 22, g_theme.taskbar);
    sys_rect(0, taskbar_y, (int)SCREEN_WIDTH, 1, 15);
    sys_rect(0, taskbar_y + 21, (int)SCREEN_WIDTH, 1, 0);
    draw_task_button(&start_button, "Iniciar", start_hover);

    for (int i = 0; i < win_count; ++i) {
        struct rect button;

        if (!wins[i].active) {
            continue;
        }

        button.x = x;
        button.y = taskbar_y + 3;
        button.w = 68;
        button.h = 16;
        if (button.x + button.w > (int)SCREEN_WIDTH - 4) {
            break;
        }
        draw_task_button(&button, app_caption(wins[i].type), i == focused && !wins[i].minimized);
        x += button.w + 4;
    }
}

void draw_desktop(const struct mouse_state *mouse,
                  int menu_open,
                  int start_hover,
                  int terminal_item_hover,
                  int clock_item_hover,
                  int filemgr_item_hover,
                  int editor_item_hover,
                  int taskmgr_item_hover,
                  int logout_item_hover,
                  const struct window *wins,
                  int win_count,
                  int focused) {
    const int desktop_h = (int)SCREEN_HEIGHT - 22;

    sys_clear(g_theme.background);
    sys_rect(0, 0, (int)SCREEN_WIDTH, desktop_h, g_theme.background);
    sys_rect(20, 18, 140, 18, g_theme.window);
    sys_text(28, 24, g_theme.text, "VIBE DESKTOP");
    sys_rect((int)SCREEN_WIDTH - 108, 24, 72, 72, 11);
    sys_text((int)SCREEN_WIDTH - 94, 52, g_theme.text, "Meu PC");

    draw_taskbar(wins, win_count, focused, start_hover);

    if (menu_open) {
        draw_start_menu((int)SCREEN_HEIGHT - 22,
                        terminal_item_hover,
                        clock_item_hover,
                        filemgr_item_hover,
                        editor_item_hover,
                        taskmgr_item_hover,
                        logout_item_hover);
    }
    (void)mouse;
}
