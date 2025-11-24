#include "ramdisk.h"
#include "memory.h"
#include "print.h"

#define RAMDISK_SIZE (1024 * 512)  // 512 KB

static uint8_t *ramdisk_data = NULL;
static int ramdisk_initialized = 0;

void ramdisk_init(void) {
    if (ramdisk_initialized) return;
    
    // Allocate memory for RAM disk
    ramdisk_data = (uint8_t*)kmalloc(RAMDISK_SIZE);
    
    if (!ramdisk_data) {
        char err[] = "Failed to allocate RAM disk\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    // Zero out the RAM disk
    for (int i = 0; i < RAMDISK_SIZE; i++) {
        ramdisk_data[i] = 0;
    }
    
    ramdisk_initialized = 1;
    
    char msg[] = "RAM disk initialized (512 KB)\n";
    printk(0xFF00FF00, 0x000000, msg);
}

int ramdisk_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!ramdisk_initialized || !buffer || sector_count == 0) return -1;
    
    uint32_t offset = lba * 512;
    uint32_t size = sector_count * 512;
    
    // Bounds check
    if (offset + size > RAMDISK_SIZE) {
        char err[] = "RAM disk read out of bounds\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    // Copy data
    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = ramdisk_data[offset + i];
    }
    
    return 0;
}

int ramdisk_write_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!ramdisk_initialized || !buffer || sector_count == 0) return -1;
    
    uint32_t offset = lba * 512;
    uint32_t size = sector_count * 512;
    
    // Bounds check
    if (offset + size > RAMDISK_SIZE) {
        char err[] = "RAM disk write out of bounds\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    // Copy data
    for (uint32_t i = 0; i < size; i++) {
        ramdisk_data[offset + i] = buffer[i];
    }
    
    return 0;
}

int ramdisk_is_available(void) {
    return ramdisk_initialized;
}