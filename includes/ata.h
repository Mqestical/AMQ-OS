#ifndef ATA_H
#define ATA_H

#include <stdint.h>

// ATA Primary Bus I/O Ports
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBA_LO      0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HI      0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_CONTROL     0x3F6  // Control/Alternate Status port

// ATA Commands
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_FLUSH_CACHE     0xE7

// ATA Status Register Bits
#define ATA_STATUS_ERR          0x01  // Error
#define ATA_STATUS_IDX          0x02  // Index
#define ATA_STATUS_CORR         0x04  // Corrected data
#define ATA_STATUS_DRQ          0x08  // Data request
#define ATA_STATUS_DSC          0x10  // Drive seek complete
#define ATA_STATUS_DF           0x20  // Drive fault
#define ATA_STATUS_DRDY         0x40  // Drive ready
#define ATA_STATUS_BSY          0x80  // Busy

// Alias for compatibility
#define ATA_STATUS_RDY          ATA_STATUS_DRDY

// Sector size
#define SECTOR_SIZE             512

// Function declarations
void ata_init(void);
int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer);
int ata_write_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer);

#endif // ATA_H