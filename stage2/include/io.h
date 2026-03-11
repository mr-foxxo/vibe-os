#ifndef STAGE2_IO_H
#define STAGE2_IO_H

#include <stdint.h>

/* Read byte from I/O port */
uint8_t inb(uint16_t port);

/* Write byte to I/O port */
void outb(uint16_t port, uint8_t value);

/* I/O wait (dummy port read) */
void io_wait(void);

#endif
