#include "userland.h"
#include <stddef.h>
#include <stdint.h>

#define USERLAND_LOAD_ADDR 0x20000u

typedef void (*userland_entry_t)(void);

extern const uint8_t _binary_userland_bin_start[];
extern const uint8_t _binary_userland_bin_end[];

static void memory_copy(uint8_t *dst, const uint8_t *src, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

void userland_run(void) {
    const size_t userland_size =
        (size_t)(_binary_userland_bin_end - _binary_userland_bin_start);
    uint8_t *const load_addr = (uint8_t *)USERLAND_LOAD_ADDR;

    if (userland_size == 0) {
        return;
    }

    memory_copy(load_addr, _binary_userland_bin_start, userland_size);
    ((userland_entry_t)load_addr)();
}
