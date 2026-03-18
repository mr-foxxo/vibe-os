#ifndef KERNEL_KEYMAP_H
#define KERNEL_KEYMAP_H

#define KEYMAP_SIZE 128

typedef struct {
    const char* name;
    const char* map;
    const char* shift_map;
} keymap_t;

#endif // KERNEL_KEYMAP_H
