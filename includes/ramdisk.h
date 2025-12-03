#ifndef RAMDISK_H
#define RAMDISK_H

#include <stdint.h>

void ramdisk_init(void);

int ramdisk_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer);

int ramdisk_write_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer);

int ramdisk_is_available(void);

#endif
