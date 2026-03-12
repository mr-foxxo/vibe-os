#ifndef EDITOR_H
#define EDITOR_H

#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>

struct editor_state {
    struct rect window;
    int file_node;
    int length;
    int cursor;
    char buffer[FS_FILE_MAX + 1];
    char status[40];
};

void editor_init_state(struct editor_state *ed);
int editor_load_node(struct editor_state *ed, int node);
int editor_save(struct editor_state *ed);
void editor_insert_char(struct editor_state *ed, char c);
void editor_backspace(struct editor_state *ed);
void editor_newline(struct editor_state *ed);
void editor_draw_window(struct editor_state *ed, int active,
                        int min_hover, int max_hover, int close_hover);
struct rect editor_save_button_rect(const struct editor_state *ed);

#endif // EDITOR_H
