#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "utils.h"

struct filemanager_state {
    struct rect window;
    int cwd;
};

void filemanager_init_state(struct filemanager_state *fm);
void filemanager_draw_window(struct filemanager_state *fm, int close_hover);

#endif // FILEMANAGER_H
