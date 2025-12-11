#ifndef PCI_H
#define PCI_H

#include <stdint.h>

// PCI Configuration Space
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

// PCI Configuration Registers
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS_CODE      0x0B
#define PCI_CACHE_LINE      0x0C
#define PCI_LATENCY_TIMER   0x0D
#define PCI_HEADER_TYPE     0x0E
#define PCI_BIST            0x0F
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_CARDBUS_CIS     0x28
#define PCI_SUBSYSTEM_VENDOR 0x2C
#define PCI_SUBSYSTEM_ID    0x2E
#define PCI_EXPANSION_ROM   0x30
#define PCI_CAPABILITIES    0x34
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D
#define PCI_MIN_GRANT       0x3E
#define PCI_MAX_LATENCY     0x3F

// PCI Command Register Bits
#define PCI_COMMAND_IO              0x0001
#define PCI_COMMAND_MEMORY          0x0002
#define PCI_COMMAND_MASTER          0x0004
#define PCI_COMMAND_SPECIAL         0x0008
#define PCI_COMMAND_INVALIDATE      0x0010
#define PCI_COMMAND_VGA_PALETTE     0x0020
#define PCI_COMMAND_PARITY          0x0040
#define PCI_COMMAND_WAIT            0x0080
#define PCI_COMMAND_SERR            0x0100
#define PCI_COMMAND_FAST_BACK       0x0200
#define PCI_COMMAND_INTX_DISABLE    0x0400

// PCI Device Classes
#define PCI_CLASS_UNCLASSIFIED      0x00
#define PCI_CLASS_STORAGE           0x01
#define PCI_CLASS_NETWORK           0x02
#define PCI_CLASS_DISPLAY           0x03
#define PCI_CLASS_MULTIMEDIA        0x04
#define PCI_CLASS_MEMORY            0x05
#define PCI_CLASS_BRIDGE            0x06
#define PCI_CLASS_COMMUNICATION     0x07
#define PCI_CLASS_SYSTEM            0x08
#define PCI_CLASS_INPUT             0x09
#define PCI_CLASS_DOCKING           0x0A
#define PCI_CLASS_PROCESSOR         0x0B
#define PCI_CLASS_SERIAL            0x0C
#define PCI_CLASS_WIRELESS          0x0D
#define PCI_CLASS_INTELLIGENT       0x0E
#define PCI_CLASS_SATELLITE         0x0F
#define PCI_CLASS_ENCRYPTION        0x10
#define PCI_CLASS_SIGNAL            0x11
#define PCI_CLASS_UNDEFINED         0xFF

// PCI Device Structure
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint32_t bar[6];
} pci_device_t;

// Function Prototypes
uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);

void pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
void pci_write_config_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint16_t value);
void pci_write_config_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint8_t value);

int pci_scan_device(uint8_t bus, uint8_t device, uint8_t func, pci_device_t *dev);
void pci_scan_all(void);

void pci_enable_bus_mastering(uint8_t bus, uint8_t device, uint8_t func);
void pci_enable_io_space(uint8_t bus, uint8_t device, uint8_t func);
void pci_enable_memory_space(uint8_t bus, uint8_t device, uint8_t func);

const char* pci_get_class_name(uint8_t class_code);
const char* pci_get_vendor_name(uint16_t vendor_id);

#endif // PCI_H