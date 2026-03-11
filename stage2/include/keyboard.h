#ifndef STAGE2_KEYBOARD_H
#define STAGE2_KEYBOARD_H

#include <stdint.h>

#include <stdint.h>

/* Initialize keyboard */
void keyboard_init(void);

/* Pop character from keyboard queue */
int keyboard_read(void);

#endif
