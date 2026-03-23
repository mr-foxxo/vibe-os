#ifndef USERLAND_API_H
#define USERLAND_API_H

#include <stdint.h>

enum syscall_id {
    SYSCALL_GFX_CLEAR = 1,
    SYSCALL_GFX_RECT = 2,
    SYSCALL_GFX_TEXT = 3,
    SYSCALL_INPUT_MOUSE = 4,
    SYSCALL_INPUT_KEY = 5,
    SYSCALL_SLEEP = 6,
    SYSCALL_TIME_TICKS = 7,
    SYSCALL_GFX_INFO = 8,
    SYSCALL_GETPID = 9,
    SYSCALL_YIELD = 10,
    SYSCALL_WRITE_DEBUG = 11,
    SYSCALL_GFX_FLIP = 14,
    SYSCALL_GFX_LEAVE = 15,
    SYSCALL_TEXT_MOVE_CURSOR = 16,
    SYSCALL_GFX_SET_MODE = 17,
    SYSCALL_STORAGE_LOAD = 18,
    SYSCALL_STORAGE_SAVE = 19,
    SYSCALL_STORAGE_READ_SECTORS = 20,
    SYSCALL_STORAGE_WRITE_SECTORS = 26,
    SYSCALL_STORAGE_TOTAL_SECTORS = 27,
    SYSCALL_GFX_SET_PALETTE = 28,
    SYSCALL_GFX_GET_PALETTE = 29,
    SYSCALL_GFX_BLIT8 = 30,

    SYSCALL_KEYBOARD_SET_LAYOUT = 21,
    SYSCALL_KEYBOARD_GET_LAYOUT = 22,
    SYSCALL_KEYBOARD_GET_AVAILABLE_LAYOUTS = 23,
    SYSCALL_GFX_CAPS = 24,
    SYSCALL_SHUTDOWN = 25,

    /* Filesystem syscalls */
    SYSCALL_OPEN = 31,
    SYSCALL_READ = 32,
    SYSCALL_WRITE = 33,
    SYSCALL_CLOSE = 34,
    SYSCALL_LSEEK = 35,
    SYSCALL_STAT = 36,
    SYSCALL_FSTAT = 37
};

enum input_keycode {
    KEY_ARROW_UP = 0x100,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_DELETE
};

struct mouse_state {
    int x;
    int y;
    int dx;
    int dy;
    uint8_t buttons;
};

struct video_mode {
    uint32_t fb_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

#define VIDEO_MODE_LIST_MAX 5u

enum video_capability_flags {
    VIDEO_CAPS_TEXT_ONLY = 1u << 0,
    VIDEO_CAPS_BOOT_LFB = 1u << 1,
    VIDEO_CAPS_BGA = 1u << 2,
    VIDEO_CAPS_CAN_SET_MODE = 1u << 3
};

enum video_resolution_bits {
    VIDEO_RES_640X480 = 1u << 0,
    VIDEO_RES_800X600 = 1u << 1,
    VIDEO_RES_1024X768 = 1u << 2,
    VIDEO_RES_1360X720 = 1u << 3,
    VIDEO_RES_1920X1080 = 1u << 4
};

struct video_capabilities {
    uint32_t flags;
    uint32_t supported_modes;
    uint32_t active_width;
    uint32_t active_height;
    uint32_t active_bpp;
    uint32_t mode_count;
    uint16_t mode_width[VIDEO_MODE_LIST_MAX];
    uint16_t mode_height[VIDEO_MODE_LIST_MAX];
};

typedef void (*userland_entry_t)(void);

#endif
