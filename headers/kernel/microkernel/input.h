#ifndef KERNEL_MICROKERNEL_INPUT_H
#define KERNEL_MICROKERNEL_INPUT_H

#include <include/userland_api.h>
#include <stdint.h>

struct mk_input_transfer_request {
    uint32_t buffer_size;
    uint32_t transfer_id;
};

struct mk_input_layout_set_request {
    uint32_t name_length;
    uint32_t transfer_id;
};

struct mk_input_result {
    int32_t value;
};

struct mk_input_mouse_reply {
    int32_t value;
    struct mouse_state state;
};

struct mk_input_event_reply {
    int32_t value;
    struct input_event event;
};

void mk_input_service_init(void);
int mk_input_service_next_event(struct input_event *event);
int mk_input_service_poll_mouse(struct mouse_state *state);
int mk_input_service_read_key(void);
int mk_input_service_read_key_wait(uint32_t timeout_ticks);
int mk_input_service_set_layout(const char *name);
int mk_input_service_get_layout(char *buffer, int size);
int mk_input_service_get_available_layouts(char *buffer, int size);

#endif
