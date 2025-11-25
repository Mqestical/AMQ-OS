#include "ata.h"
#include "IO.h"
#include "print.h"

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
            char err[] = "[ATA] Error bit set in status: 0x%x\n";
            printk(0xFFFF0000, 0x000000, err, status);
            return -1;
        }
        
        timeout--;
        for (volatile int i = 0; i < 100; i++);
    }
    
    char timeout_msg[] = "[ATA] Timeout waiting for ready\n";
    printk(0xFFFF0000, 0x000000, timeout_msg);
    return -1;  // Timeout
}

static int ata_wait_drq(void) {
    int timeout = ATA_TIMEOUT;
    
    // First wait for BSY to clear
    while (timeout > 0) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        
        if (!(status & ATA_STATUS_BSY)) {
            break;
        }
        
        timeout--;
        for (volatile int i = 0; i < 100; i++);
    }
    
    if (timeout == 0) {
        char msg[] = "[ATA] Timeout waiting for BSY clear\n";
        printk(0xFFFF0000, 0x000000, msg);
        return -1;
    }
    
    // Now wait for DRQ
    timeout = ATA_TIMEOUT;
    while (timeout > 0) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        
        if (status & ATA_STATUS_DRQ) {
            return 0;  // Success
        }
        
        if (status & ATA_STATUS_ERR) {
            char err[] = "[ATA] Error bit set waiting for DRQ: 0x%x\n";
            printk(0xFFFF0000, 0x000000, err, status);
            return -1;
        }
        
        timeout--;
        for (volatile int i = 0; i < 100; i++);
    }
    
    char timeout_msg[] = "[ATA] Timeout waiting for DRQ\n";
    printk(0xFFFF0000, 0x000000, timeout_msg);
    return -1;
}

// Soft reset the ATA controller
static void ata_soft_reset(void) {
    // Set SRST bit
    outb(ATA_PRIMARY_CONTROL, 0x04);
    
    // Wait
    for (volatile int i = 0; i < 100000; i++);
    
    // Clear SRST bit
    outb(ATA_PRIMARY_CONTROL, 0x00);
    
    // Wait for drive ready
    for (volatile int i = 0; i < 100000; i++);
}

void ata_init(void) {
    char msg1[] = "[ATA] Initializing...\n";
    printk(0xFF00FFFF, 0x000000, msg1);
    
    // Soft reset first
    ata_soft_reset();
    
    // Check if ATA controller exists by reading status
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    
    // If we get 0xFF, the controller doesn't exist
    if (status == 0xFF) {
        char err[] = "ATA controller not found\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    char msg2[] = "[ATA] Controller found, status=0x%x\n";
    printk(0xFF00FFFF, 0x000000, msg2, status);
    
    // After sending WRITE command
outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);

// INCREASE THIS DELAY significantly
for (volatile int i = 0; i < 100000; i++);  // Was 10000, now 100000
    
    // Wait for drive ready
    if (ata_wait_ready() != 0) {
        char err[] = "ATA disk timeout (not ready)\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    char msg3[] = "[ATA] Drive ready\n";
    printk(0xFF00FFFF, 0x000000, msg3);
    
    // Send IDENTIFY command
    outb(ATA_PRIMARY_COMMAND, 0xEC);
    
    // Wait for response
    for (volatile int i = 0; i < 100000; i++);
    
    // Check if drive responded
    status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        char err[] = "ATA disk: No drive present\n";
        printk(0xFFFF0000, 0x000000, err);
        return;
    }
    
    // Wait for DRQ
    if (ata_wait_drq() == 0) {
        // Read IDENTIFY data (256 words)
        uint16_t identify[256];
        for (int i = 0; i < 256; i++) {
            identify[i] = inw(ATA_PRIMARY_DATA);
        }
        char msg4[] = "[ATA] IDENTIFY successful\n";
        printk(0xFF00FFFF, 0x000000, msg4);
    }
    
    char msg[] = "ATA disk initialized\n";
    printk(0xFF00FF00, 0x000000, msg);
}

int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!buffer || sector_count == 0) return -1;
    
    // Wait for drive ready
    if (ata_wait_ready() != 0) {
        return -1;
    }
    
    // Select drive and LBA mode
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    
    // Delay after drive select
    for (volatile int i = 0; i < 10000; i++);
    
    // Send parameters
    outb(ATA_PRIMARY_SECCOUNT, sector_count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    
    // Send read command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);
    
    // Read each sector
    for (int i = 0; i < sector_count; i++) {
        // Wait for data ready
        if (ata_wait_drq() != 0) {
            return -1;
        }
        
        // Check for errors
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_ERR) {
            return -1;
        }
        
        // Read 256 words (512 bytes)
        uint16_t *buf16 = (uint16_t*)(buffer + i * SECTOR_SIZE);
        for (int j = 0; j < 256; j++) {
            buf16[j] = inw(ATA_PRIMARY_DATA);
        }
        
        // Small delay between sectors
        for (volatile int k = 0; k < 400; k++);
    }
    
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!buffer || sector_count == 0) {
        char err[] = "[ATA] write_sectors: invalid parameters\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    char msg1[] = "[ATA] write_sectors: lba=%u, count=%u\n";
  //  printk(0xFF00FFFF, 0x000000, msg1, lba, sector_count);
    
    // Wait for drive ready
    char msg2[] = "[ATA] write_sectors: Waiting for ready...\n";
   // printk(0xFF00FFFF, 0x000000, msg2);
    
    if (ata_wait_ready() != 0) {
        char err[] = "[ATA] write_sectors: Drive not ready (1)\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    char msg3[] = "[ATA] write_sectors: Drive ready\n";
   // printk(0xFF00FFFF, 0x000000, msg3);
    
    // Select drive and LBA mode
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    
    // CRITICAL: Longer delay after drive select for VirtualBox
    for (volatile int i = 0; i < 50000; i++);
    
    // Wait for ready again
    if (ata_wait_ready() != 0) {
        char err[] = "[ATA] write_sectors: Drive not ready (2)\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    // Send parameters
    outb(ATA_PRIMARY_SECCOUNT, sector_count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    
    char msg4[] = "[ATA] write_sectors: Sending WRITE command...\n";
    // printk(0xFF00FFFF, 0x000000, msg4);
    
    // Send write command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);
    
    // Delay after command
    for (volatile int i = 0; i < 10000; i++);
    
    // Write each sector
    for (int i = 0; i < sector_count; i++) {
        char msg5[] = "[ATA] write_sectors: Writing sector %d...\n";
       // printk(0xFF00FFFF, 0x000000, msg5, i);
        
        // Wait for data request
        if (ata_wait_drq() != 0) {
            char err[] = "[ATA] write_sectors: DRQ timeout on sector %d\n";
            printk(0xFFFF0000, 0x000000, err, i);
            return -1;
        }
        
        // Check for errors
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_ERR) {
            char err[] = "[ATA] write_sectors: Error status on sector %d: 0x%x\n";
            printk(0xFFFF0000, 0x000000, err, i, status);
            uint8_t error = inb(ATA_PRIMARY_ERROR);
            char err2[] = "[ATA] write_sectors: Error register: 0x%x\n";
            printk(0xFFFF0000, 0x000000, err2, error);
            return -1;
        }
        
        // Write 256 words (512 bytes)
        uint16_t *buf16 = (uint16_t*)(buffer + i * SECTOR_SIZE);
        for (int j = 0; j < 256; j++) {
            outw(ATA_PRIMARY_DATA, buf16[j]);
            
            // Small delay between writes for stability
            for (volatile int k = 0; k < 10; k++);
        }
        
        // Longer delay between sectors
        for (volatile int k = 0; k < 10000; k++);
    }
    
    char msg6[] = "[ATA] write_sectors: Flushing cache...\n";
   // printk(0xFF00FFFF, 0x000000, msg6);
    
    // Flush cache
    outb(ATA_PRIMARY_COMMAND, 0xE7);
    
    // Wait for flush to complete
    for (volatile int i = 0; i < 100000; i++);
    
    if (ata_wait_ready() != 0) {
        char err[] = "[ATA] write_sectors: Flush failed\n";
        printk(0xFFFF0000, 0x000000, err);
        return -1;
    }
    
    char msg7[] = "[ATA] write_sectors: SUCCESS\n";
    //printk(0xFF00FF00, 0x000000, msg7);
    
    return 0;
}

static inline void ata_delay_400ns(void) {
    inb(ATA_PRIMARY_ALTSTATUS);
    inb(ATA_PRIMARY_ALTSTATUS);
    inb(ATA_PRIMARY_ALTSTATUS);
    inb(ATA_PRIMARY_ALTSTATUS);
}
