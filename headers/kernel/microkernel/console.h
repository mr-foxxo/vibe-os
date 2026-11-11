#ifndef KERNEL_MICROKERNEL_CONSOLE_H
#define KERNEL_MICROKERNEL_CONSOLE_H

#include <stdint.h>

struct mk_console_text_request {
    uint32_t length;
    uint32_t transfer_id;
};

struct mk_console_cursor_request {
    int32_t delta;
};

struct mk_console_putc_request {
    uint32_t character;
};

struct mk_console_result {
    int32_t value;
};

struct mk_console_vt_switch_request {
    uint32_t vt;
};

void mk_console_service_init(void);
int mk_console_service_write_debug(const char *message);
int mk_console_service_text_write(const char *message);
int mk_console_service_text_clear(void);
int mk_console_service_text_move_cursor(int delta);
int mk_console_service_text_putc(char c);

#endif
