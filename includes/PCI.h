#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI configuration space offsets
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_CLASS_CODE     0x0B
#define PCI_HEADER_TYPE    0x0E
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_INTERRUPT_LINE 0x3C

static inline uint32_t pci_read_dword(uint8_t bus, uint8_t device, 
                                      uint8_t function, uint8_t offset) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | 
                       ((uint32_t)device << 11) | ((uint32_t)function << 8) |
                       (offset & 0xFC);
    
__asm__ volatile("outl %0, %1" : : "a"(address), "dN"((uint16_t)PCI_CONFIG_ADDRESS));
uint32_t value;
__asm__ volatile("inl %1, %0" : "=a"(value) : "dN"((uint16_t)PCI_CONFIG_DATA));
return value;

}

static inline uint16_t pci_read_word(uint8_t bus, uint8_t device,
                                    uint8_t function, uint8_t offset) {
    uint32_t dword = pci_read_dword(bus, device, function, offset & 0xFC);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

static inline uint8_t pci_read_byte(uint8_t bus, uint8_t device,
                                   uint8_t function, uint8_t offset) {
    uint32_t dword = pci_read_dword(bus, device, function, offset & 0xFC);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

static inline void pci_write_dword(uint8_t bus, uint8_t device,
                                   uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) | ((uint32_t)function << 8) |
                       (offset & 0xFC);
    
__asm__ volatile("outl %0, %1" : : "a"(address), "dN"((uint16_t)PCI_CONFIG_ADDRESS));
__asm__ volatile("inl %1, %0" : "=a"(value) : "dN"((uint16_t)PCI_CONFIG_DATA));

}

static inline void pci_write_word(uint8_t bus, uint8_t device,
                                  uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t dword = pci_read_dword(bus, device, function, offset & 0xFC);
    uint8_t shift = (offset & 2) * 8;
    dword = (dword & ~(0xFFFF << shift)) | ((uint32_t)value << shift);
    pci_write_dword(bus, device, function, offset & 0xFC, dword);
}

#endif // PCI_H