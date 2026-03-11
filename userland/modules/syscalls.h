#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include "userland_api.h"

int sys_poll_mouse(struct mouse_state *state);
int sys_poll_key(void);
void sys_clear(uint8_t color);
void sys_rect(int x, int y, int w, int h, uint8_t color);
void sys_text(int x, int y, uint8_t color, const char *text);
void sys_sleep(void);
uint32_t sys_ticks(void);

#endif // SYSCALLS_H