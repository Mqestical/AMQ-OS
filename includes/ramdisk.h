#ifndef RAMDISK_H
#define RAMDISK_H

#include <stdint.h>

// Initialize RAM disk
void ramdisk_init(void);

// Read sectors from RAM disk
int ramdisk_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer);

// Write sectors to RAM disk
int ramdisk_write_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer);

// Check if RAM disk is available
int ramdisk_is_available(void);

#endif // RAMDISK_H