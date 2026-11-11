/* vibeos.c
 * VibeOS graphics driver for Links2
 * (c) 2024 VibeOS Project
 * This file is a part of the Links program, released under GPL.
 */

#include "cfg.h"

#ifdef GRDRV_VIBEOS

#include "links.h"
#include "vibeos.h"
#include <userland/modules/include/syscalls.h>

#define VIBEOS_WIDTH  1024
#define VIBEOS_HEIGHT 768
#define VIBEOS_PITCH  VIBEOS_WIDTH
#define VIBEOS_FB_SIZE (VIBEOS_WIDTH * VIBEOS_HEIGHT)
#define VIBEOS_POLL_MS 10

static unsigned char *vibeos_fb = NULL;
static int vibeos_fb_allocated = 0;
static struct timer *vibeos_poll_timer = NULL;

static unsigned short vibeos_palette_red[256];
static unsigned short vibeos_palette_green[256];
static unsigned short vibeos_palette_blue[256];
static int vibeos_palette_dirty = 0;
static int vibeos_palette_uploaded = 0;

static int vibeos_mouse_x = 0;
static int vibeos_mouse_y = 0;
static int vibeos_mouse_buttons = 0;

static unsigned char *vibeos_driver_param = NULL;

static void vibeos_event_poll(void *data);

static int vibeos_translate_key(int vk)
{
	if (vk >= 0x20 && vk <= 0x7E)
		return vk;
	if (vk == '\n' || vk == '\r')
		return KBD_ENTER;
	if (vk == '\b' || vk == 0x7F)
		return KBD_BS;
	if (vk == '\t')
		return KBD_TAB;
	if (vk == 0x1B)
		return KBD_ESC;
	if (vk == KEY_ARROW_UP)
		return KBD_UP;
	if (vk == KEY_ARROW_DOWN)
		return KBD_DOWN;
	if (vk == KEY_ARROW_LEFT)
		return KBD_LEFT;
	if (vk == KEY_ARROW_RIGHT)
		return KBD_RIGHT;
	if (vk == KEY_DELETE)
		return KBD_DEL;
	return 0;
}

static int vibeos_translate_buttons(unsigned char vibe_buttons)
{
	int b = 0;
	if (vibe_buttons & 1) b |= 1;
	if (vibe_buttons & 2) b |= 4;
	if (vibe_buttons & 4) b |= 2;
	return b | B_MOVE;
}

static unsigned char *vibeos_init_driver(unsigned char *param, unsigned char *display)
{
	struct video_mode vm;
	int rs;

	(void)display;

	vibeos_fb = mem_alloc(VIBEOS_FB_SIZE);
	memset(vibeos_fb, 0, VIBEOS_FB_SIZE);
	vibeos_fb_allocated = 1;

	rs = sys_gfx_set_mode(VIBEOS_WIDTH, VIBEOS_HEIGHT, 8);
	if (rs) {
		mem_free(vibeos_fb);
		vibeos_fb_allocated = 0;
		return stracpy(cast_uchar "sys_gfx_set_mode failed");
	}

	sys_gfx_info(&vm);
	if (vm.width < VIBEOS_WIDTH || vm.height < VIBEOS_HEIGHT) {
		mem_free(vibeos_fb);
		vibeos_fb_allocated = 0;
		return stracpy(cast_uchar "cannot set 1024x768 mode");
	}

	vibeos_driver_param = stracpy(param ? param : cast_uchar "");

	vibeos_driver.depth = 0x341;
	vibeos_driver.get_color = get_color_fn(vibeos_driver.depth);
	if (!vibeos_driver.get_color) {
		mem_free(vibeos_fb);
		vibeos_fb_allocated = 0;
		return stracpy(cast_uchar "get_color_fn failed for depth 0x341");
	}

	init_virtual_devices(&vibeos_driver, 1);

	vibeos_poll_timer = install_timer(VIBEOS_POLL_MS, vibeos_event_poll, NULL);

	sys_clear(0);

	sys_present();

	return NULL;
}

static struct graphics_device *vibeos_init_device(void)
{
	struct graphics_device *dev;

	dev = init_virtual_device();
	if (!dev)
		return NULL;

	dev->size.x2 = VIBEOS_WIDTH;
	dev->size.y2 = VIBEOS_HEIGHT;
	dev->clip.x1 = 0;
	dev->clip.y1 = 0;
	dev->clip.x2 = VIBEOS_WIDTH;
	dev->clip.y2 = VIBEOS_HEIGHT;
	set_clip_area(dev, &dev->size);

	return dev;
}

static void vibeos_shutdown_device(struct graphics_device *dev)
{
	shutdown_virtual_device(dev);
}

static void vibeos_shutdown_driver(void)
{
	if (vibeos_poll_timer) {
		kill_timer(vibeos_poll_timer);
		vibeos_poll_timer = NULL;
	}

	shutdown_virtual_devices();

	if (vibeos_fb_allocated && vibeos_fb) {
		mem_free(vibeos_fb);
		vibeos_fb = NULL;
		vibeos_fb_allocated = 0;
	}

	if (vibeos_driver_param) {
		mem_free(vibeos_driver_param);
		vibeos_driver_param = NULL;
	}
}

static void vibeos_emergency_shutdown(void)
{
	vibeos_shutdown_driver();
}

static void vibeos_after_fork(void)
{
}

static unsigned char *vibeos_get_driver_param(void)
{
	return stracpy(vibeos_driver_param ? vibeos_driver_param : cast_uchar "");
}

static void vibeos_get_margin(int *left, int *right, int *top, int *bottom)
{
	*left = *right = *top = *bottom = 0;
}

static int vibeos_set_margin(int left, int right, int top, int bottom)
{
	(void)left; (void)right; (void)top; (void)bottom;
	return -1;
}

static int vibeos_get_empty_bitmap(struct bitmap *dest)
{
	int s = dest->x * dest->y;
	if (!s) return -1;

	dest->skip = dest->x;
	dest->data = mem_alloc(dest->x * dest->y);
	if (!dest->data) return -1;

	return 0;
}

static void vibeos_register_bitmap(struct bitmap *bmp)
{
	(void)bmp;
}

static void *vibeos_prepare_strip(struct bitmap *bmp, int top, int lines)
{
	return (unsigned char *)bmp->data + top * bmp->skip;
}

static void vibeos_commit_strip(struct bitmap *bmp, int top, int lines)
{
	(void)bmp; (void)top; (void)lines;
}

static void vibeos_unregister_bitmap(struct bitmap *bmp)
{
	(void)bmp;
}

static void vibeos_draw_bitmap(struct graphics_device *dev, struct bitmap *bmp, int x, int y)
{
	int i, j;
	int src_w = bmp->x;
	int src_h = bmp->y;
	ssize_t src_skip = bmp->skip;
	unsigned char *src_data = (unsigned char *)bmp->data;
	unsigned char *dst;

	CLIP_DRAW_BITMAP

	if (src_w <= 0 || src_h <= 0) return;

	for (j = 0; j < src_h; j++) {
		int dst_y = y + j;
		if (dst_y < dev->clip.y1 || dst_y >= dev->clip.y2) continue;
		dst = vibeos_fb + dst_y * VIBEOS_PITCH + x;
		memcpy(dst, src_data + j * src_skip, src_w <= dev->clip.x2 - x ? src_w : dev->clip.x2 - x);
	}
}

static void vibeos_fill_area(struct graphics_device *dev, int x1, int y1, int x2, int y2, long color)
{
	int x, y;
	unsigned char *row;

	CLIP_FILL_AREA

	for (y = y1; y < y2; y++) {
		row = vibeos_fb + y * VIBEOS_PITCH + x1;
		memset(row, (unsigned char)color, x2 - x1);
	}
}

static void vibeos_draw_hline(struct graphics_device *dev, int left, int y, int right, long color)
{
	unsigned char *row;

	CLIP_DRAW_HLINE

	row = vibeos_fb + y * VIBEOS_PITCH + x1;
	memset(row, (unsigned char)color, x2 - x1);
}

static void vibeos_draw_vline(struct graphics_device *dev, int x, int top, int bottom, long color)
{
	int y;

	CLIP_DRAW_VLINE

	for (y = y1; y < y2; y++) {
		vibeos_fb[y * VIBEOS_PITCH + x] = (unsigned char)color;
	}
}

static int vibeos_scroll(struct graphics_device *dev, struct rect_set **set, int scx, int scy)
{
	int pitch = VIBEOS_PITCH;
	int h = dev->size.y2;
	int w = dev->size.x2;
	int y;

	if (abs(scx) >= w || abs(scy) >= h) {
		add_to_rect_set(set, &dev->clip);
		return 1;
	}

	if (scy > 0) {
		unsigned char *src = vibeos_fb + scy * pitch;
		unsigned char *dst = vibeos_fb;
		int rows = h - scy;
		memmove(dst, src, rows * pitch);
		memset(dst + rows * pitch, 0, scy * pitch);
	} else if (scy < 0) {
		unsigned char *src = vibeos_fb;
		unsigned char *dst = vibeos_fb - scy * pitch;
		int rows = h + scy;
		memmove(dst, src, rows * pitch);
		memset(vibeos_fb, 0, -scy * pitch);
	}

	if (scx > 0) {
		for (y = 0; y < h; y++)
			memmove(vibeos_fb + y * pitch, vibeos_fb + y * pitch + scx, w - scx);
	} else if (scx < 0) {
		for (y = 0; y < h; y++)
			memmove(vibeos_fb + y * pitch - scx, vibeos_fb + y * pitch, w + scx);
	}

	return 1;
}

static void vibeos_flush(struct graphics_device *dev)
{
	(void)dev;

	if (!vibeos_fb) return;

	if (vibeos_palette_dirty && !vibeos_palette_uploaded) {
		unsigned char sys_pal[768];
		int i;
		for (i = 0; i < 256; i++) {
			sys_pal[i * 3 + 0] = (unsigned char)(vibeos_palette_red[i] >> 8);
			sys_pal[i * 3 + 1] = (unsigned char)(vibeos_palette_green[i] >> 8);
			sys_pal[i * 3 + 2] = (unsigned char)(vibeos_palette_blue[i] >> 8);
		}
		sys_gfx_set_palette(sys_pal);
		vibeos_palette_uploaded = 1;
		vibeos_palette_dirty = 0;
	}

	sys_gfx_blit8(vibeos_fb, VIBEOS_WIDTH, VIBEOS_HEIGHT, 0, 0, 1);
}

static int vibeos_block(struct graphics_device *dev)
{
	(void)dev;
	return 0;
}

static int vibeos_unblock(struct graphics_device *dev)
{
	(void)dev;
	return 0;
}

static void vibeos_set_palette(void)
{
	int i;
	unsigned rgb[3];

	for (i = 0; i < 256; i++) {
		q_palette(256, i, 65535, rgb);
		vibeos_palette_red[i] = rgb[0];
		vibeos_palette_green[i] = rgb[1];
		vibeos_palette_blue[i] = rgb[2];
	}

	vibeos_palette_dirty = 1;
	vibeos_palette_uploaded = 0;
}

static unsigned short *vibeos_get_real_colors(void)
{
	return NULL;
}

static void vibeos_event_poll(void *data)
{
	struct input_event ev;
	struct mouse_state ms;

	(void)data;

	while (sys_next_input_event(&ev) == 0) {
		if (ev.type == INPUT_EVENT_KEY) {
			int key = vibeos_translate_key(ev.value);
			if (key && current_virtual_device && current_virtual_device->keyboard_handler) {
				current_virtual_device->keyboard_handler(current_virtual_device, key, 0);
			}
		}
		if (ev.type == INPUT_EVENT_MOUSE) {
			vibeos_mouse_x = ev.mouse.x;
			vibeos_mouse_y = ev.mouse.y;
			vibeos_mouse_buttons = ev.mouse.buttons;
			if (current_virtual_device && current_virtual_device->mouse_handler) {
				int b = vibeos_translate_buttons(ev.mouse.buttons);
				current_virtual_device->mouse_handler(
					current_virtual_device,
					vibeos_mouse_x,
					vibeos_mouse_y,
					b);
			}
		}
	}

	if (sys_poll_mouse(&ms) == 0) {
		if ((ms.x != vibeos_mouse_x || ms.y != vibeos_mouse_y || ms.buttons != vibeos_mouse_buttons)
		    && current_virtual_device && current_virtual_device->mouse_handler) {
			vibeos_mouse_x = ms.x;
			vibeos_mouse_y = ms.y;
			vibeos_mouse_buttons = ms.buttons;
			{
				int b = vibeos_translate_buttons(ms.buttons);
				current_virtual_device->mouse_handler(
					current_virtual_device,
					ms.x, ms.y, b);
			}
		}
	}

	vibeos_poll_timer = install_timer(VIBEOS_POLL_MS, vibeos_event_poll, NULL);
}

struct graphics_driver vibeos_driver = {
	cast_uchar "vibeos",
	vibeos_init_driver,
	vibeos_init_device,
	vibeos_shutdown_device,
	vibeos_shutdown_driver,
	vibeos_emergency_shutdown,
	vibeos_after_fork,
	vibeos_get_driver_param,
	(unsigned char *(*)(void))NULL,
	vibeos_get_margin,
	vibeos_set_margin,
	vibeos_get_empty_bitmap,
	vibeos_register_bitmap,
	vibeos_prepare_strip,
	vibeos_commit_strip,
	vibeos_unregister_bitmap,
	vibeos_draw_bitmap,
	(long (*)(int))NULL,
	vibeos_fill_area,
	vibeos_draw_hline,
	vibeos_draw_vline,
	vibeos_scroll,
	(void (*)(struct graphics_device *dev))NULL,
	vibeos_flush,
	vibeos_block,
	vibeos_unblock,
	vibeos_set_palette,
	vibeos_get_real_colors,
	(void (*)(struct graphics_device *, unsigned char *))NULL,
	(int (*)(unsigned char *, int))NULL,
	(void (*)(unsigned char *))NULL,
	(unsigned char *(*)(void))NULL,
	0x341,
	VIBEOS_WIDTH,
	VIBEOS_HEIGHT,
	GD_NEED_CODEPAGE,
	NULL,
};

#endif
