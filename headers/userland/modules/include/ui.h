#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <include/userland_api.h>
#include <userland/applications/include/apps.h>
#include <userland/modules/include/utils.h> // for struct rect

/* Screen resolution globals - initialized at startup */
extern uint32_t SCREEN_WIDTH;
extern uint32_t SCREEN_HEIGHT;
extern uint32_t SCREEN_PITCH;
extern struct video_mode g_screen_mode;

enum theme_slot {
    THEME_SLOT_BACKGROUND = 0,
    THEME_SLOT_MENU,
    THEME_SLOT_MENU_BUTTON,
    THEME_SLOT_MENU_BUTTON_INACTIVE,
    THEME_SLOT_TASKBAR,
    THEME_SLOT_WINDOW,
    THEME_SLOT_WINDOW_BG,
    THEME_SLOT_TEXT,
    THEME_SLOT_COUNT
};

struct desktop_theme {
    uint8_t background;
    uint8_t menu;
    uint8_t menu_button;
    uint8_t menu_button_inactive;
    uint8_t taskbar;
    uint8_t window;
    uint8_t window_bg;
    uint8_t text;
};

enum start_menu_item {
    START_MENU_ITEM_0 = 0,
    START_MENU_ITEM_1,
    START_MENU_ITEM_2,
    START_MENU_ITEM_3,
    START_MENU_ITEM_4,
    START_MENU_ITEM_5,
    START_MENU_ITEM_6,
    START_MENU_ITEM_7,
    START_MENU_ITEM_8,
    START_MENU_ITEM_9,
    START_MENU_ITEM_COUNT
};

enum start_menu_tab {
    START_MENU_TAB_APPS = 0,
    START_MENU_TAB_GAMES
};

enum ui_button_style {
    UI_BUTTON_NORMAL = 0,
    UI_BUTTON_PRIMARY,
    UI_BUTTON_DANGER,
    UI_BUTTON_ACTIVE
};

void ui_init(void);
void ui_refresh_metrics(void);
int ui_set_resolution(uint32_t width, uint32_t height);
const struct desktop_theme *ui_theme_get(void);
void ui_theme_set_slot(enum theme_slot slot, uint8_t color);
const char *ui_theme_slot_name(enum theme_slot slot);

/* Theme management */
void ui_theme_save_named(const char *name);
void ui_theme_load_named(const char *name);
void ui_theme_export(const char *export_path);
void ui_theme_import(const char *import_path);
void ui_theme_create_classic(void);
void ui_theme_create_luna(void);
void ui_theme_create_luna_dark(void);

void ui_wallpaper_clear(void);
int ui_wallpaper_set_from_node(int node);
int ui_wallpaper_source_node(void);
struct rect ui_taskbar_start_button_rect(void);
struct rect ui_start_menu_rect(void);
struct rect ui_start_menu_item_rect(int index);
uint8_t ui_color_canvas(void);
uint8_t ui_color_panel(void);
uint8_t ui_color_muted(void);
void ui_draw_surface(const struct rect *r, uint8_t fill);
void ui_draw_inset(const struct rect *r, uint8_t fill);
void ui_draw_button(const struct rect *r, const char *label,
                    enum ui_button_style style, int highlighted);
void ui_draw_status(const struct rect *r, const char *text);

void draw_window_frame(const struct rect *w, const char *title,
                       int active,
                       int min_hover,
                       int max_hover,
                       int close_hover);

/* draw background, taskbar/menu area and cursor; individual windows
   are drawn by the caller (desktop_main) after this helper has filled
   the background.  The menu flags indicate which item is hovered; more
   app entries can be added here as needed. */
void draw_desktop(const struct mouse_state *mouse,
                  int menu_open,
                  int start_hover,
                  const int *menu_item_hover,
                  const struct window *wins,
                  int win_count,
                  int focused);

/* entry point for the graphical desktop environment invoked by startx */
void desktop_request_open_editor(const char *path);
void desktop_main(void);

#endif // UI_H
