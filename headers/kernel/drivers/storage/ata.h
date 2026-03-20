#ifndef KERNEL_DRIVERS_STORAGE_ATA_H
#define KERNEL_DRIVERS_STORAGE_ATA_H

#include <stdint.h>

#define KERNEL_BOOT_KERNEL_SECTORS 1280u
#define KERNEL_APPFS_START_LBA 1281u
#define KERNEL_APPFS_DIRECTORY_SECTORS 8u
#define KERNEL_APPFS_APP_SECTOR_COUNT 1536u
#define KERNEL_PERSIST_START_LBA (KERNEL_APPFS_START_LBA + KERNEL_APPFS_DIRECTORY_SECTORS + KERNEL_APPFS_APP_SECTOR_COUNT)
#define KERNEL_PERSIST_SECTOR_SIZE 512u
#define KERNEL_PERSIST_SECTOR_COUNT 640u
#define KERNEL_PERSIST_MAX_BYTES (KERNEL_PERSIST_SECTOR_SIZE * KERNEL_PERSIST_SECTOR_COUNT)

void kernel_storage_init(void);
int kernel_storage_ready(void);
int kernel_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count);
int kernel_storage_write_sectors(uint32_t lba, const void *src, uint32_t sector_count);
uint32_t kernel_storage_total_sectors(void);
int kernel_storage_load(void *dst, uint32_t size);
int kernel_storage_save(const void *src, uint32_t size);

#endif
