#include <kernel/drivers/video/video.h>

int vga_init(struct video_mode *mode) {
    mode->fb_addr = 0xA0000;
    mode->width   = 320;
    mode->height  = 200;
    mode->pitch   = 320;
    mode->bpp     = 8;
    return 0;
}
