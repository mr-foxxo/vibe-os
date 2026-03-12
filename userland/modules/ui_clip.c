#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/ui.h>

#define CLIP_STACK_DEPTH 16

static struct rect g_clip_stack[CLIP_STACK_DEPTH];
static int g_clip_depth = 0;

void clip_init(void) {
    g_clip_depth = 0;
    /* Initialize with full screen */
    g_clip_stack[0].x = 0;
    g_clip_stack[0].y = 0;
    g_clip_stack[0].w = (int)SCREEN_WIDTH;
    g_clip_stack[0].h = (int)SCREEN_HEIGHT;
    g_clip_depth = 1;
}

void clip_push(int x, int y, int w, int h) {
    if (g_clip_depth >= CLIP_STACK_DEPTH) return;
    
    struct rect *parent = &g_clip_stack[g_clip_depth - 1];
    struct rect *new_clip = &g_clip_stack[g_clip_depth];
    
    /* Calculate intersection with parent clip region */
    int x0 = x > parent->x ? x : parent->x;
    int y0 = y > parent->y ? y : parent->y;
    int x1_parent = parent->x + parent->w;
    int y1_parent = parent->y + parent->h;
    int x1_new = x + w;
    int y1_new = y + h;
    int x1 = x1_parent < x1_new ? x1_parent : x1_new;
    int y1 = y1_parent < y1_new ? y1_parent : y1_new;
    
    new_clip->x = x0;
    new_clip->y = y0;
    new_clip->w = x1 > x0 ? x1 - x0 : 0;
    new_clip->h = y1 > y0 ? y1 - y0 : 0;
    
    g_clip_depth++;
}

void clip_pop(void) {
    if (g_clip_depth > 1) {
        g_clip_depth--;
    }
}

void clip_get_current(struct rect *out) {
    if (out && g_clip_depth > 0) {
        *out = g_clip_stack[g_clip_depth - 1];
    }
}

int clip_intersects(int x, int y, int w, int h) {
    if (g_clip_depth == 0) return 0;
    
    struct rect *clip = &g_clip_stack[g_clip_depth - 1];
    
    int x1 = x + w;
    int y1 = y + h;
    int cx1 = clip->x + clip->w;
    int cy1 = clip->y + clip->h;
    
    return !(x1 <= clip->x || x >= cx1 || y1 <= clip->y || y >= cy1);
}

int clip_rect(int *out_x, int *out_y, int *out_w, int *out_h, int x, int y, int w, int h) {
    if (g_clip_depth == 0 || !clip_intersects(x, y, w, h)) {
        *out_w = 0;
        *out_h = 0;
        return 0;
    }
    
    struct rect *clip = &g_clip_stack[g_clip_depth - 1];
    
    int x0 = x > clip->x ? x : clip->x;
    int y0 = y > clip->y ? y : clip->y;
    int x1_new = x + w;
    int y1_new = y + h;
    int x1_clip = clip->x + clip->w;
    int y1_clip = clip->y + clip->h;
    int x1 = x1_clip < x1_new ? x1_clip : x1_new;
    int y1 = y1_clip < y1_new ? y1_clip : y1_new;
    
    *out_x = x0;
    *out_y = y0;
    *out_w = x1 - x0;
    *out_h = y1 - y0;
    
    return (*out_w > 0 && *out_h > 0) ? 1 : 0;
}
