#include "ata.h"
#include "IO.h"
#include "print.h"
#include "string_helpers.h"

#define ATA_TIMEOUT 5000000  // Much longer timeout for VirtualBox
#define ATA_PRIMARY_ALTSTATUS 0x3F6

// Wait for drive to be ready with timeout
static int ata_wait_ready(void) {
    int timeout = ATA_TIMEOUT;
    while (timeout > 0) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        
        // Check if BSY is clear and RDY is set
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_RDY)) {
            return 0;  // Success
        }
        
        // Check for error
        if (status & ATA_STATUS_ERR) {
            PRINT(YELLOW, BLACK, "[ATA] Error bit set in status: 0x%x\n", status);
            return -1;
        }
        
        timeout--;
        for (volatile int i = 0; i < 100; i++);
    }
    
    PRINT(YELLOW, BLACK, "[ATA] Timeout waiting for ready\n");
    return -1;  // Timeout
}

static int ata_wait_drq(void) {
    int timeout = ATA_TIMEOUT;
    
    // First wait for BSY to clear
    while (timeout > 0) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        
        if (!(status & ATA_STATUS_BSY)) break;
        
        timeout--;
        for (volatile int i = 0; i < 100; i++);
    }
    
    if (timeout == 0) {
        PRINT(YELLOW, BLACK, "[ATA] Timeout waiting for BSY clear\n");
        return -1;
    }
    
    // Now wait for DRQ
    timeout = ATA_TIMEOUT;
    while (timeout > 0) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        
        if (status & ATA_STATUS_DRQ) return 0;  // Success
        
        if (status & ATA_STATUS_ERR) {
            PRINT(YELLOW, BLACK, "[ATA] Error bit set waiting for DRQ: 0x%x\n", status);
            return -1;
        }
        
        timeout--;
        for (volatile int i = 0; i < 100; i++);
    }
    
    PRINT(YELLOW, BLACK, "[ATA] Timeout waiting for DRQ\n");
    return -1;
}

// Soft reset the ATA controller
static void ata_soft_reset(void) {
    outb(ATA_PRIMARY_CONTROL, 0x04);  // Set SRST bit
    for (volatile int i = 0; i < 100000; i++);
    outb(ATA_PRIMARY_CONTROL, 0x00);  // Clear SRST
    for (volatile int i = 0; i < 100000; i++);
}

void ata_init(void) {
    PRINT(MAGENTA, BLACK, "[ATA] Initializing...\n");
    
    ata_soft_reset();
    
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0xFF) {
        PRINT(YELLOW, BLACK, "ATA controller not found\n");
        return;
    }
    
    PRINT(MAGENTA, BLACK, "[ATA] Controller found, status=0x%x\n", status);
    
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);
    for (volatile int i = 0; i < 100000; i++);
    
    if (ata_wait_ready() != 0) {
        PRINT(YELLOW, BLACK, "ATA disk timeout (not ready)\n");
        return;
    }
    
    PRINT(MAGENTA, BLACK, "[ATA] Drive ready\n");
    
    outb(ATA_PRIMARY_COMMAND, 0xEC);  // IDENTIFY
    for (volatile int i = 0; i < 100000; i++);
    
    status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        PRINT(YELLOW, BLACK, "ATA disk: No drive present\n");
        return;
    }
    
    if (ata_wait_drq() == 0) {
        uint16_t identify[256];
        for (int i = 0; i < 256; i++) identify[i] = inw(ATA_PRIMARY_DATA);
        PRINT(MAGENTA, BLACK, "[ATA] IDENTIFY successful\n");
    }
    
    PRINT(MAGENTA, BLACK, "ATA disk initialized\n");
}

int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!buffer || sector_count == 0) return -1;
    
    if (ata_wait_ready() != 0) return -1;
    
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    for (volatile int i = 0; i < 10000; i++);
    
    outb(ATA_PRIMARY_SECCOUNT, sector_count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);
    
    for (int i = 0; i < sector_count; i++) {
        if (ata_wait_drq() != 0) return -1;
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_ERR) return -1;
        
        uint16_t *buf16 = (uint16_t*)(buffer + i * SECTOR_SIZE);
        for (int j = 0; j < 256; j++) buf16[j] = inw(ATA_PRIMARY_DATA);
        for (volatile int k = 0; k < 400; k++);
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!buffer || sector_count == 0) {
        PRINT(YELLOW, BLACK, "[ATA] write_sectors: invalid parameters\n");
        return -1;
    }
    
    if (ata_wait_ready() != 0) {
        PRINT(YELLOW, BLACK, "[ATA] write_sectors: Drive not ready (1)\n");
        return -1;
    }
    
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    for (volatile int i = 0; i < 50000; i++);
    
    if (ata_wait_ready() != 0) {
        PRINT(YELLOW, BLACK, "[ATA] write_sectors: Drive not ready (2)\n");
        return -1;
    }
    
    outb(ATA_PRIMARY_SECCOUNT, sector_count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);
    for (volatile int i = 0; i < 10000; i++);
    
    for (int i = 0; i < sector_count; i++) {
        if (ata_wait_drq() != 0) {
            PRINT(YELLOW, BLACK, "[ATA] write_sectors: DRQ timeout on sector %d\n", i);
            return -1;
        }
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_ERR) {
            uint8_t error = inb(ATA_PRIMARY_ERROR);
            PRINT(YELLOW, BLACK, "[ATA] write_sectors: Error on sector %d, status=0x%x, error=0x%x\n", i, status, error);
            return -1;
        }
        
        uint16_t *buf16 = (uint16_t*)(buffer + i * SECTOR_SIZE);
        for (int j = 0; j < 256; j++) {
            outw(ATA_PRIMARY_DATA, buf16[j]);
            for (volatile int k = 0; k < 10; k++);
        }
        for (volatile int k = 0; k < 10000; k++);
    }
    
    outb(ATA_PRIMARY_COMMAND, 0xE7);  // Flush
    for (volatile int i = 0; i < 100000; i++);
    
    if (ata_wait_ready() != 0) {
        PRINT(YELLOW, BLACK, "[ATA] write_sectors: Flush failed\n");
        return -1;
    }
    
    return 0;
}

static inline void ata_delay_400ns(void) {
    inb(ATA_PRIMARY_ALTSTATUS);
    inb(ATA_PRIMARY_ALTSTATUS);
    inb(ATA_PRIMARY_ALTSTATUS);
    inb(ATA_PRIMARY_ALTSTATUS);
}