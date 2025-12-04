#include "ramdisk.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"

#define RAMDISK_SIZE (1024 * 512)

static uint8_t *ramdisk_data = NULL;
static int ramdisk_initialized = 0;

void ramdisk_init(void) {
    if (ramdisk_initialized) return;

    ramdisk_data = (uint8_t*)kmalloc(RAMDISK_SIZE);

    if (!ramdisk_data) {
        PRINT(YELLOW, BLACK, "Failed to allocate RAM disk\n");
        return;
    }

    for (int i = 0; i < RAMDISK_SIZE; i++) {
        ramdisk_data[i] = 0;
    }

    ramdisk_initialized = 1;

    PRINT(MAGENTA, BLACK, "RAM disk initialized (512 KB)\n");
}

int ramdisk_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!ramdisk_initialized || !buffer || sector_count == 0) return -1;

    uint32_t offset = lba * 512;
    uint32_t size = sector_count * 512;

    if (offset + size > RAMDISK_SIZE) {
        PRINT(YELLOW, BLACK, "RAM disk read out of bounds\n");
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = ramdisk_data[offset + i];
    }

    return 0;
}

int ramdisk_write_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!ramdisk_initialized || !buffer || sector_count == 0) return -1;

    uint32_t offset = lba * 512;
    uint32_t size = sector_count * 512;

    if (offset + size > RAMDISK_SIZE) {
        PRINT(YELLOW, BLACK, "RAM disk write out of bounds\n");
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        ramdisk_data[offset + i] = buffer[i];
    }

    return 0;
}

int ramdisk_is_available(void) {
    return ramdisk_initialized;
}