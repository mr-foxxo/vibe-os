#ifndef VIBE_LANG_VIBE_APPFS_H
#define VIBE_LANG_VIBE_APPFS_H

#include <stdint.h>

#include <lang/include/vibe_app.h>

#define VIBE_APPFS_MAGIC 0x53465056u
#define VIBE_APPFS_VERSION 1u
#define VIBE_APPFS_ENTRY_MAX 48u
#define VIBE_APPFS_DIRECTORY_LBA 1281u
#define VIBE_APPFS_DIRECTORY_SECTORS 8u
#define VIBE_APPFS_APP_AREA_SECTORS 1536u

struct vibe_appfs_entry {
    char name[VIBE_APP_NAME_MAX];
    uint32_t lba_start;
    uint32_t sector_count;
    uint32_t image_size;
    uint32_t flags;
};

struct vibe_appfs_directory {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t checksum;
    struct vibe_appfs_entry entries[VIBE_APPFS_ENTRY_MAX];
};

#endif
