#ifndef VIBE_DOOM_PORT_H
#define VIBE_DOOM_PORT_H

#include <stdint.h>

int doom_port_should_quit(void);
void doom_port_request_quit(const char *reason, int code);
const char *doom_port_last_error(void);
int doom_port_last_code(void);
void doom_port_set_palette(const uint8_t *pal);
uint8_t doom_port_map_color(uint8_t idx);

#endif
