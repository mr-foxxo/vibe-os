/* drivers.c
 * (c) 2002 Mikulas Patocka
 * This file is a part of the Links program, released under GPL
 */

#include "cfg.h"

#ifdef G

#include "links.h"

int F = 0;

struct graphics_driver *drv = NULL;

#ifdef GRDRV_X
extern struct graphics_driver x_driver;
#endif
#ifdef GRDRV_SVGALIB
extern struct graphics_driver svga_driver;
#endif
#ifdef GRDRV_FB
extern struct graphics_driver fb_driver;
#endif
#ifdef GRDRV_DIRECTFB
extern struct graphics_driver directfb_driver;
#endif
#ifdef GRDRV_PMSHELL
extern struct graphics_driver pmshell_driver;
#endif
#ifdef GRDRV_ATHEOS
extern struct graphics_driver atheos_driver;
#endif
#ifdef GRDRV_HAIKU
extern struct graphics_driver haiku_driver;
#endif
#ifdef GRDRV_GRX
extern struct graphics_driver grx_driver;
#endif
#ifdef GRDRV_SDL
extern struct graphics_driver sdl_driver;
#endif
#ifdef GRDRV_VIBEOS
extern struct graphics_driver vibeos_driver;
#endif

/*
 * On SPAD you must test first svgalib and then X (because X test is slow).
 * On other systems you must test first X and then svgalib (because svgalib
 *	would work in X too and it's undesirable).
 */

static struct graphics_driver *graphics_drivers[] = {
#ifdef GRDRV_PMSHELL
	&pmshell_driver,
#endif
#ifdef GRDRV_ATHEOS
	&atheos_driver,
#endif
#ifdef GRDRV_HAIKU
	&haiku_driver,
#endif
#ifndef SPAD
#ifdef GRDRV_X
	&x_driver,
#endif
#endif
#ifdef GRDRV_FB
	/* use FB before DirectFB because DirectFB has bugs */
	&fb_driver,
#endif
#ifdef GRDRV_DIRECTFB
	&directfb_driver,
#endif
#ifdef GRDRV_SVGALIB
	&svga_driver,
#endif
#ifdef SPAD
#ifdef GRDRV_X
	&x_driver,
#endif
#endif
#ifdef GRDRV_GRX
	&grx_driver,
#endif
#ifdef GRDRV_SDL
	&sdl_driver,
#endif
#ifdef GRDRV_VIBEOS
	&vibeos_driver,
#endif
	NULL
};

#if 0
static unsigned char *list_graphics_drivers(void)
{
	unsigned char *d = init_str();
	int l = 0;
	struct graphics_driver **gd;
	for (gd = graphics_drivers; *gd; gd++) {
		if (l) add_chr_to_str(&d, &l, ' ');
		add_to_str(&d, &l, (*gd)->name);
	}
	return d;
}
#endif

/* Driver je jednorazovy argument, kterej se preda grafickymu driveru, nikde se dal
 * neuklada.  Param se skladuje v default_driver param a uklada se do konfiguraku. Pred
 * ukoncenim grafickeho driveru se nastavi default_driver_param podle
 * drv->get_driver_param.
 */
static unsigned char *init_graphics_driver(struct graphics_driver *gd, unsigned char *param, unsigned char *display)
{
	unsigned char *r;
	unsigned char *p = param;
	struct driver_param *dp = get_driver_param(gd->name);
	if (!param || !*param) p = dp->param;
	gd->param = dp;
	drv = gd;
	r = gd->init_driver(p,display);
	if (r) drv = NULL;
	else F = 1;
	return r;
}


void add_graphics_drivers(unsigned char **s, int *l)
{
	struct graphics_driver **gd;
	for (gd = graphics_drivers; *gd; gd++) {
		if (gd != graphics_drivers) add_to_str(s, l, cast_uchar ", ");
		add_to_str(s, l, (*gd)->name);
	}
}

unsigned char *init_graphics(unsigned char *driver, unsigned char *param, unsigned char *display)
{
	unsigned char *s = init_str();
	int l = 0;
	struct graphics_driver **gd;
#if defined(GRDRV_PMSHELL) && defined(GRDRV_X)
	if (is_xterm()) {
		static unsigned char swapped = 0;
		if (!swapped) {
			for (gd = graphics_drivers; *gd; gd++) {
				if (*gd == &pmshell_driver) *gd = &x_driver;
				else if (*gd == &x_driver) *gd = &pmshell_driver;
			}
			swapped = 1;
		}
	}
#endif
	for (gd = graphics_drivers; *gd; gd++) {
		if (!driver || !*driver || !casestrcmp((*gd)->name, driver)) {
			unsigned char *r;
			if ((!driver || !*driver) && (*gd)->flags & GD_NOAUTO) continue;
			if (!(r = init_graphics_driver(*gd, param, display))) {
				mem_free(s);
				return NULL;
			}
			if (!l) {
				if (!driver || !*driver) add_to_str(&s, &l, cast_uchar "Could not initialize any graphics driver. Tried the following drivers:\n");
				else add_to_str(&s, &l, cast_uchar "Could not initialize graphics driver ");
			}
			add_to_str(&s, &l, (*gd)->name);
			add_to_str(&s, &l, cast_uchar ":\n");
			add_to_str(&s, &l, r);
			mem_free(r);
		}
	}
	if (!l) {
		add_to_str(&s, &l, cast_uchar "Unknown graphics driver ");
		if (driver) add_to_str(&s, &l, driver);
		add_to_str(&s, &l, cast_uchar ".\nThe following graphics drivers are supported:\n");
		add_graphics_drivers(&s, &l);
		add_to_str(&s, &l, cast_uchar "\n");
	}
	return s;
}

void shutdown_graphics(void)
{
	if (drv) {
		drv->shutdown_driver();
		drv = NULL;
		F = 0;
	}
}

void update_driver_param(void)
{
	if (drv) {
		struct driver_param *dp = drv->param;
		if (dp->param) mem_free(dp->param), dp->param = NULL;
		if (drv->get_driver_param)
			dp->param = stracpy(drv->get_driver_param());
		dp->nosave = 0;
	}
}

int g_kbd_codepage(struct graphics_driver *drv)
{
	if (drv->param->kbd_codepage >= 0)
		return drv->param->kbd_codepage;
	return get_default_charset();
}

int do_rects_intersect(struct rect *r1, struct rect *r2)
{
	return (r1->x1 > r2->x1 ? r1->x1 : r2->x1) < (r1->x2 > r2->x2 ? r2->x2 : r1->x2) && (r1->y1 > r2->y1 ? r1->y1 : r2->y1) < (r1->y2 > r2->y2 ? r2->y2 : r1->y2);
}

void intersect_rect(struct rect *v, struct rect *r1, struct rect *r2)
{
	v->x1 = r1->x1 > r2->x1 ? r1->x1 : r2->x1;
	v->x2 = r1->x2 > r2->x2 ? r2->x2 : r1->x2;
	v->y1 = r1->y1 > r2->y1 ? r1->y1 : r2->y1;
	v->y2 = r1->y2 > r2->y2 ? r2->y2 : r1->y2;
}

void unite_rect(struct rect *v, struct rect *r1, struct rect *r2)
{
	if (!is_rect_valid(r1)) {
		if (v != r2) memcpy(v, r2, sizeof(struct rect));
		return;
	}
	if (!is_rect_valid(r2)) {
		if (v != r1) memcpy(v, r1, sizeof(struct rect));
		return;
	}
	v->x1 = r1->x1 < r2->x1 ? r1->x1 : r2->x1;
	v->x2 = r1->x2 < r2->x2 ? r2->x2 : r1->x2;
	v->y1 = r1->y1 < r2->y1 ? r1->y1 : r2->y1;
	v->y2 = r1->y2 < r2->y2 ? r2->y2 : r1->y2;
}

#define R_GR	8

struct rect_set *init_rect_set(void)
{
	struct rect_set *s;
	s = mem_calloc(sizeof(struct rect_set) + sizeof(struct rect) * R_GR);
	s->rl = R_GR;
	s->m = 0;
	return s;
}

void add_to_rect_set(struct rect_set **s, struct rect *r)
{
	struct rect_set *ss = *s;
	int i;
	if (!is_rect_valid(r)) return;
	for (i = 0; i < ss->rl; i++) if (!ss->r[i].x1 && !ss->r[i].x2 && !ss->r[i].y1 && !ss->r[i].y2) {
		x:
		memcpy(&ss->r[i], r, sizeof(struct rect));
		if (i >= ss->m) ss->m = i + 1;
		return;
	}
	if ((unsigned)ss->rl > (MAXINT - sizeof(struct rect_set)) / sizeof(struct rect) - R_GR) overalloc();
	ss = mem_realloc(ss, sizeof(struct rect_set) + sizeof(struct rect) * (ss->rl + R_GR));
	memset(&(*s = ss)->r[i = (ss->rl += R_GR) - R_GR], 0, sizeof(struct rect) * R_GR);
	goto x;
}

void exclude_rect_from_set(struct rect_set **s, struct rect *r)
{
	int i, a;
	struct rect *rr;
	do {
		a = 0;
		for (i = 0; i < (*s)->m; i++) if (do_rects_intersect(rr = &(*s)->r[i], r)) {
			struct rect r1, r2, r3, r4;
			r1.x1 = rr->x1;
			r1.x2 = rr->x2;
			r1.y1 = rr->y1;
			r1.y2 = r->y1;

			r2.x1 = rr->x1;
			r2.x2 = r->x1;
			r2.y1 = r->y1;
			r2.y2 = r->y2;

			r3.x1 = r->x2;
			r3.x2 = rr->x2;
			r3.y1 = r->y1;
			r3.y2 = r->y2;

			r4.x1 = rr->x1;
			r4.x2 = rr->x2;
			r4.y1 = r->y2;
			r4.y2 = rr->y2;

			intersect_rect(&r2, &r2, rr);
			intersect_rect(&r3, &r3, rr);
			rr->x1 = rr->x2 = rr->y1 = rr->y2 = 0;
#ifdef DEBUG
			if (is_rect_valid(&r1) && do_rects_intersect(&r1, r)) internal_error("bad intersection 1");
			if (is_rect_valid(&r2) && do_rects_intersect(&r2, r)) internal_error("bad intersection 2");
			if (is_rect_valid(&r3) && do_rects_intersect(&r3, r)) internal_error("bad intersection 3");
			if (is_rect_valid(&r4) && do_rects_intersect(&r4, r)) internal_error("bad intersection 4");
#endif
			add_to_rect_set(s, &r1);
			add_to_rect_set(s, &r2);
			add_to_rect_set(s, &r3);
			add_to_rect_set(s, &r4);
			a = 1;
		}
	} while (a);
}

void set_clip_area(struct graphics_device *dev, struct rect *r)
{
	dev->clip = *r;
	if (dev->clip.x1 < 0) dev->clip.x1 = 0;
	if (dev->clip.x2 > dev->size.x2) dev->clip.x2 = dev->size.x2;
	if (dev->clip.y1 < 0) dev->clip.y1 = 0;
	if (dev->clip.y2 > dev->size.y2) dev->clip.y2 = dev->size.y2;
	if (!is_rect_valid(&dev->clip)) {
		/* Empty region */
		dev->clip.x1 = dev->clip.x2 = dev->clip.y1 = dev->clip.y2 = 0;
	}
	if (drv->set_clip_area)
		drv->set_clip_area(dev);
}

/* memory address r must contain one struct rect
 * x1 is leftmost pixel that is still valid
 * x2 is leftmost pixel that isn't valid any more
 * y1, y2 analogically
 */
int restrict_clip_area(struct graphics_device *dev, struct rect *r, int x1, int y1, int x2, int y2)
{
	struct rect v, rr;
	rr.x1 = x1, rr.x2 = x2, rr.y1 = y1, rr.y2 = y2;
	if (r) memcpy(r, &dev->clip, sizeof(struct rect));
	intersect_rect(&v, &dev->clip, &rr);
	set_clip_area(dev, &v);
	return is_rect_valid(&v);
}

struct rect_set *g_scroll(struct graphics_device *dev, int scx, int scy)
{
	struct rect_set *rs = init_rect_set();

	if (!scx && !scy)
		return rs;
	if (abs(scx) >= dev->clip.x2 - dev->clip.x1 ||
	    abs(scy) >= dev->clip.y2 - dev->clip.y1) {
		add_to_rect_set(&rs, &dev->clip);
		return rs;
	}

	if (drv->scroll(dev, &rs, scx, scy)) {
		struct rect q = dev->clip;
		if (scy >= 0)
			q.y2 = q.y1 + scy;
		else
			q.y1 = q.y2 + scy;
		add_to_rect_set(&rs, &q);

		q = dev->clip;
		if (scy >= 0)
			q.y1 += scy;
		else
			q.y2 += scy;
		if (scx >= 0)
			q.x2 = q.x1 + scx;
		else
			q.x1 = q.x2 + scx;
		add_to_rect_set(&rs, &q);
	}

	return rs;
}

#ifdef GRDRV_VIRTUAL_DEVICES

struct graphics_device **virtual_devices;
int n_virtual_devices = 0;
struct graphics_device *current_virtual_device;

static struct timer *virtual_device_timer;

void init_virtual_devices(struct graphics_driver *drv, int n)
{
	if (n_virtual_devices) {
		internal_error("init_virtual_devices: already initialized");
		return;
	}
	if ((unsigned)n > MAXINT / sizeof(struct graphics_device *)) overalloc();
	virtual_devices = mem_calloc(n * sizeof(struct graphics_device *));
	n_virtual_devices = n;
	virtual_device_timer = NULL;
	current_virtual_device = NULL;
}

struct graphics_device *init_virtual_device(void)
{
	int i;
	for (i = 0; i < n_virtual_devices; i++) if (!virtual_devices[i]) {
		struct graphics_device *dev;
		dev = mem_calloc(sizeof(struct graphics_device));
		dev->size.x2 = drv->x;
		dev->size.y2 = drv->y;
		current_virtual_device = virtual_devices[i] = dev;
		set_clip_area(dev, &dev->size);
		return dev;
	}
	return NULL;
}

static void virtual_device_timer_fn(void *p)
{
	virtual_device_timer = NULL;
	if (current_virtual_device && current_virtual_device->redraw_handler) {
		set_clip_area(current_virtual_device, &current_virtual_device->size);
		current_virtual_device->redraw_handler(current_virtual_device, &current_virtual_device->size);
	}
}

void switch_virtual_device(int i)
{
	if (i == VD_NEXT) {
		int j;
		int t = 0;
		for (j = 0; j < n_virtual_devices * 2; j++)
			if (virtual_devices[j % n_virtual_devices] == current_virtual_device) t = 1;
			else if (virtual_devices[j % n_virtual_devices] && t) {
				current_virtual_device = virtual_devices[j % n_virtual_devices];
				goto ok_switch;
			}
		return;
	}
	if (i < 0 || i >= n_virtual_devices || !virtual_devices[i]) return;
	current_virtual_device = virtual_devices[i];
	ok_switch:
	if (virtual_device_timer == NULL)
		virtual_device_timer = install_timer(0, virtual_device_timer_fn, NULL);
}

void shutdown_virtual_device(struct graphics_device *dev)
{
	int i;
	for (i = 0; i < n_virtual_devices; i++) if (virtual_devices[i] == dev) {
		virtual_devices[i] = NULL;
		mem_free(dev);
		if (current_virtual_device != dev) return;
		for (; i < n_virtual_devices; i++) if (virtual_devices[i]) {
			switch_virtual_device(i);
			return;
		}
		for (i = 0; i < n_virtual_devices; i++) if (virtual_devices[i]) {
			switch_virtual_device(i);
			return;
		}
		current_virtual_device = NULL;
		return;
	}
	mem_free(dev);
	/*internal_error("shutdown_virtual_device: device not initialized");*/
}

void resize_virtual_devices(int x, int y)
{
	int i;
	drv->x = x;
	drv->y = y;
	for (i = 0; i < n_virtual_devices; i++) {
		struct graphics_device *dev = virtual_devices[i];
		if (dev) {
			dev->size.x2 = x;
			dev->size.y2 = y;
			dev->resize_handler(dev);
		}
	}
}

void shutdown_virtual_devices(void)
{
	int i;
	if (!n_virtual_devices) {
		internal_error("shutdown_virtual_devices: already shut down");
		return;
	}
	for (i = 0; i < n_virtual_devices; i++) if (virtual_devices[i]) internal_error("shutdown_virtual_devices: virtual device %d is still active", i);
	mem_free(virtual_devices);
	n_virtual_devices = 0;
	if (virtual_device_timer != NULL) kill_timer(virtual_device_timer), virtual_device_timer = NULL;
}

#endif

#if defined(GRDRV_X) || defined(GRDRV_HAIKU)

/* This is executed in a helper thread, so we must not use mem_alloc */

static void addchr(unsigned char **str, size_t *l, unsigned char c)
{
	unsigned char *s;
	if (!*str) return;
	if ((*str)[*l]) *l = strlen(cast_const_char *str);
	if (*l > MAXINT - 2) overalloc();
	s = realloc(*str, *l + 2);
	if (!s) {
		free(*str);
		*str = NULL;
		return;
	}
	*str = s;
	s[(*l)++] = c;
	s[*l] = 0;
}

int x_exec(unsigned char *command, int fg)
{
	unsigned char *pattern, *final;
	size_t i, j, l;
	int retval;

	if (!fg) {
		retval = system(cast_const_char command);
		return retval;
	}

	l = 0;
	if (*drv->param->shell_term) {
		pattern = cast_uchar strdup(cast_const_char drv->param->shell_term);
	} else {
		pattern = cast_uchar strdup(cast_const_char links_xterm());
		if (*command) {
			addchr(&pattern, &l, ' ');
			addchr(&pattern, &l, '%');
		}
	}
	if (!pattern) return -1;

	final = cast_uchar strdup("");
	l = 0;
	for (i = 0; pattern[i]; i++) {
		if (pattern[i] == '%') {
			for (j = 0; j < strlen(cast_const_char command); j++)
				addchr(&final, &l, command[j]);
		} else {
			addchr(&final, &l, pattern[i]);
		}
	}
	free(pattern);
	if (!final) return -1;

	retval = system(cast_const_char final);
	free(final);
	return retval;
}

#endif

#endif
