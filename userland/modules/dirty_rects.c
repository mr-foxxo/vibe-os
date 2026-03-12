#include <userland/modules/include/dirty_rects.h>
#include <userland/modules/include/syscalls.h>

static struct rect g_dirty_rects[MAX_DIRTY_RECTS];
static int g_dirty_count = 0;

void dirty_init(void) {
    g_dirty_count = 0;
}

void dirty_add_rect(int x, int y, int w, int h) {
    if (g_dirty_count >= MAX_DIRTY_RECTS) {
        return;  /* Belt and suspenders: if we overflow, just skip marking */
    }
    
    struct rect *r = &g_dirty_rects[g_dirty_count++];
    r->x = x < 0 ? 0 : x;
    r->y = y < 0 ? 0 : y;
    r->w = w > 0 ? w : 0;
    r->h = h > 0 ? h : 0;
}

void dirty_clear(void) {
    g_dirty_count = 0;
}

void dirty_flush(void) {
    /* For now, dirty_flush is a no-op since we're redrawing everything anyway.
       In a more advanced implementation, this would only redraw marked areas. */
    dirty_clear();
}
