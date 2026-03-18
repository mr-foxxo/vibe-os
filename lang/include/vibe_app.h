#ifndef VIBE_LANG_VIBE_APP_H
#define VIBE_LANG_VIBE_APP_H

#include <stddef.h>
#include <stdint.h>

#define VIBE_APP_MAGIC 0x50504156u
#define VIBE_APP_ABI_VERSION 1u
#define VIBE_APP_NAME_MAX 16u

#define VIBE_APP_LOAD_ADDR 0x00300000u
#define VIBE_APP_ARENA_SIZE 0x00200000u
#define VIBE_APP_STACK_SIZE 0x00010000u
#define VIBE_APP_STACK_TOP (VIBE_APP_LOAD_ADDR + VIBE_APP_ARENA_SIZE)

struct vibe_app_header {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t memory_size;
    uint32_t entry_offset;
    uint32_t required_heap_size;
    char name[VIBE_APP_NAME_MAX];
};

struct vibe_app_host_api {
    uint32_t abi_version;
    void (*console_putc)(char c);
    void (*console_write)(const char *text);
    int (*poll_key)(void);
    void (*yield)(void);
    int (*read_file)(const char *path, const char **data_out, int *size_out);
    void (*write_debug)(const char *msg);
    int (*getcwd)(char *buf, int max_len);
    int (*remove_dir)(const char *path);
    int (*keyboard_set_layout)(const char *name);
    int (*keyboard_get_layout)(char *buf, int max_len);
    int (*keyboard_get_available_layouts)(char *buf, int max_len);
};

struct vibe_app_context {
    const struct vibe_app_host_api *host;
    void *heap_base;
    uint32_t heap_size;
};

typedef int (*vibe_app_entry_t)(const struct vibe_app_context *ctx, int argc, char **argv);

#endif
