#include "E1000.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"
#include "PCI.h"
#include "net.h"

static e1000_device_t e1000_dev;

// PCI Vendor/Device IDs for Intel 8254x
#define E1000_VENDOR_ID  0x8086
#define E1000_DEV_82540EM 0x100E  // VirtualBox default
#define E1000_DEV_82545EM 0x100F
#define E1000_DEV_82543GC 0x1004

uint32_t e1000_read_reg(uint16_t reg) {
    return *(volatile uint32_t*)(e1000_dev.mmio_base + reg);
}

void e1000_write_reg(uint16_t reg, uint32_t value) {
    *(volatile uint32_t*)(e1000_dev.mmio_base + reg) = value;
}

uint16_t e1000_read_eeprom(uint8_t addr) {
    uint32_t tmp;
    
    // Start EEPROM read
    tmp = ((uint32_t)addr << 8) | 0x1;
    e1000_write_reg(E1000_REG_EERD, tmp);
    
    // Wait for read to complete
    while (1) {
        tmp = e1000_read_reg(E1000_REG_EERD);
        if (tmp & 0x10) break;  // Done bit
    }
    
    return (uint16_t)(tmp >> 16);
}

static int e1000_detect_pci(void) {
    // Scan PCI bus for E1000 device
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_read_word(bus, dev, func, 0x00);
                if (vendor == 0xFFFF) continue;
                
                uint16_t device = pci_read_word(bus, dev, func, 0x02);
                
                if (vendor == E1000_VENDOR_ID && 
                    (device == E1000_DEV_82540EM || 
                     device == E1000_DEV_82545EM ||
                     device == E1000_DEV_82543GC)) {
                    
                    PRINT(GREEN, BLACK, "[E1000] Found device %x:%x at %d:%d:%d\n",
                          vendor, device, bus, dev, func);
                    
                    // Enable bus mastering and memory space
                    uint16_t cmd = pci_read_word(bus, dev, func, 0x04);
                    cmd |= 0x06;  // Bus master + Memory space
                    pci_write_word(bus, dev, func, 0x04, cmd);
                    
                    // Read BAR0 (MMIO base)
                    uint32_t bar0 = pci_read_dword(bus, dev, func, 0x10);
                    e1000_dev.mmio_base = bar0 & ~0xF;
                    e1000_dev.mmio_size = 0x20000;  // 128KB
                    
                    // Read IRQ
                    e1000_dev.irq = pci_read_byte(bus, dev, func, 0x3C);
                    
                    PRINT(WHITE, BLACK, "[E1000] MMIO at 0x%llx, IRQ %d\n",
                          e1000_dev.mmio_base, e1000_dev.irq);
                    
                    return 0;
                }
            }
        }
    }
    
    return -1;
}

static void e1000_reset(void) {
    uint32_t ctrl;
    
    // Disable interrupts
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFF);
    
    // Reset the device
    ctrl = e1000_read_reg(E1000_REG_CTRL);
    e1000_write_reg(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
    
    // Wait for reset to complete
    for (volatile int i = 0; i < 1000000; i++);
    
    // Disable interrupts again
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFF);
    e1000_read_reg(E1000_REG_ICR);  // Clear pending interrupts
}

static void e1000_read_mac(void) {
    uint16_t mac_part;
    
    mac_part = e1000_read_eeprom(0);
    e1000_dev.mac_addr[0] = mac_part & 0xFF;
    e1000_dev.mac_addr[1] = mac_part >> 8;
    
    mac_part = e1000_read_eeprom(1);
    e1000_dev.mac_addr[2] = mac_part & 0xFF;
    e1000_dev.mac_addr[3] = mac_part >> 8;
    
    mac_part = e1000_read_eeprom(2);
    e1000_dev.mac_addr[4] = mac_part & 0xFF;
    e1000_dev.mac_addr[5] = mac_part >> 8;
    
    PRINT(GREEN, BLACK, "[E1000] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
          e1000_dev.mac_addr[0], e1000_dev.mac_addr[1],
          e1000_dev.mac_addr[2], e1000_dev.mac_addr[3],
          e1000_dev.mac_addr[4], e1000_dev.mac_addr[5]);
}

static void e1000_setup_rx(void) {
    // Allocate RX descriptor ring
    e1000_dev.rx_descs = (e1000_rx_desc_t*)kmalloc(
        sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC);
    e1000_dev.rx_buffers = (uint8_t**)kmalloc(
        sizeof(uint8_t*) * E1000_NUM_RX_DESC);
    
    // Allocate RX buffers
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        e1000_dev.rx_buffers[i] = (uint8_t*)kmalloc(E1000_RX_BUFFER_SIZE);
        e1000_dev.rx_descs[i].buffer_addr = (uint64_t)e1000_dev.rx_buffers[i];
        e1000_dev.rx_descs[i].status = 0;
    }
    
    e1000_dev.rx_cur = 0;
    
    // Setup RX registers
    e1000_write_reg(E1000_REG_RDBAL, (uint64_t)e1000_dev.rx_descs & 0xFFFFFFFF);
    e1000_write_reg(E1000_REG_RDBAH, (uint64_t)e1000_dev.rx_descs >> 32);
    e1000_write_reg(E1000_REG_RDLEN, E1000_NUM_RX_DESC * 16);
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    
    // Enable receiver
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSEX | 
                    E1000_RCTL_BSIZE_4096 | E1000_RCTL_SECRC;
    e1000_write_reg(E1000_REG_RCTL, rctl);
}

static void e1000_setup_tx(void) {
    // Allocate TX descriptor ring
    e1000_dev.tx_descs = (e1000_tx_desc_t*)kmalloc(
        sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC);
    e1000_dev.tx_buffers = (uint8_t**)kmalloc(
        sizeof(uint8_t*) * E1000_NUM_TX_DESC);
    
    // Allocate TX buffers
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        e1000_dev.tx_buffers[i] = (uint8_t*)kmalloc(E1000_TX_BUFFER_SIZE);
        e1000_dev.tx_descs[i].buffer_addr = (uint64_t)e1000_dev.tx_buffers[i];
        e1000_dev.tx_descs[i].status = E1000_TXD_STAT_DD;
        e1000_dev.tx_descs[i].cmd = 0;
    }
    
    e1000_dev.tx_cur = 0;
    
    // Setup TX registers
    e1000_write_reg(E1000_REG_TDBAL, (uint64_t)e1000_dev.tx_descs & 0xFFFFFFFF);
    e1000_write_reg(E1000_REG_TDBAH, (uint64_t)e1000_dev.tx_descs >> 32);
    e1000_write_reg(E1000_REG_TDLEN, E1000_NUM_TX_DESC * 16);
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);
    
    // Enable transmitter
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | 
                    (15 << E1000_TCTL_CT_SHIFT) |
                    (64 << E1000_TCTL_COLD_SHIFT);
    e1000_write_reg(E1000_REG_TCTL, tctl);
}

int e1000_init(void) {
    PRINT(CYAN, BLACK, "\n[E1000] Initializing Intel 8254x driver...\n");
    
    // Detect device on PCI bus
    if (e1000_detect_pci() != 0) {
        PRINT(YELLOW, BLACK, "[E1000] No device found\n");
        return -1;
    }
    
    // Reset device
    e1000_reset();
    
    // Read MAC address
    e1000_read_mac();
    
    // Set MAC address in RAL/RAH
    uint32_t ral = ((uint32_t)e1000_dev.mac_addr[3] << 24) |
                   ((uint32_t)e1000_dev.mac_addr[2] << 16) |
                   ((uint32_t)e1000_dev.mac_addr[1] << 8) |
                   e1000_dev.mac_addr[0];
    uint32_t rah = ((uint32_t)e1000_dev.mac_addr[5] << 8) |
                   e1000_dev.mac_addr[4] | (1 << 31);  // Address Valid
    
    e1000_write_reg(E1000_REG_RAL0, ral);
    e1000_write_reg(E1000_REG_RAH0, rah);
    
    // Setup RX and TX rings
    e1000_setup_rx();
    e1000_setup_tx();
    
    // Enable link
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    e1000_write_reg(E1000_REG_CTRL, ctrl);
    
    // Enable interrupts
    e1000_write_reg(E1000_REG_IMS, E1000_ICR_RXT0 | E1000_ICR_RXO | 
                                    E1000_ICR_LSC | E1000_ICR_TXQE);
    
    // Wait for link
    PRINT(WHITE, BLACK, "[E1000] Waiting for link...\n");
    for (int i = 0; i < 100; i++) {
        for (volatile int j = 0; j < 1000000; j++);
        if (e1000_read_reg(E1000_REG_STATUS) & E1000_STATUS_LU) {
            PRINT(GREEN, BLACK, "[E1000] Link is up!\n");
            break;
        }
    }
    
    e1000_dev.initialized = 1;
    
    // Register with network stack
    net_register_device(e1000_dev.mac_addr);
    
    PRINT(GREEN, BLACK, "[E1000] Initialization complete!\n");
    return 0;
}

int e1000_send_packet(const void *data, uint16_t length) {
    if (!e1000_dev.initialized || length > E1000_TX_BUFFER_SIZE) {
        return -1;
    }
    
    uint32_t tail = e1000_dev.tx_cur;
    e1000_tx_desc_t *desc = &e1000_dev.tx_descs[tail];
    
    // Wait for descriptor to be available
    while (!(desc->status & E1000_TXD_STAT_DD)) {
        for (volatile int i = 0; i < 100; i++);
    }
    
    // Copy data to TX buffer
    uint8_t *dest = (uint8_t*)desc->buffer_addr;
    const uint8_t *src = (const uint8_t*)data;
    for (uint16_t i = 0; i < length; i++) {
        dest[i] = src[i];
    }
    
    // Setup descriptor
    desc->length = length;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    
    // Update tail
    e1000_dev.tx_cur = (tail + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(E1000_REG_TDT, e1000_dev.tx_cur);
    
    return 0;
}

void e1000_interrupt_handler(void) {
    uint32_t cause = e1000_read_reg(E1000_REG_ICR);
    
    if (cause & E1000_ICR_RXT0) {
        // Process received packets
        uint32_t idx = e1000_dev.rx_cur;
        
        while (e1000_dev.rx_descs[idx].status & E1000_RXD_STAT_DD) {
            e1000_rx_desc_t *desc = &e1000_dev.rx_descs[idx];
            uint16_t length = desc->length;
            uint8_t *data = (uint8_t*)desc->buffer_addr;
            
            // Pass to network stack
            net_receive_packet(data, length);
            
            // Reset descriptor
            desc->status = 0;
            
            // Move to next descriptor
            idx = (idx + 1) % E1000_NUM_RX_DESC;
        }
        
        e1000_dev.rx_cur = idx;
        
        // Update RDT
        uint32_t tail = (idx == 0) ? E1000_NUM_RX_DESC - 1 : idx - 1;
        e1000_write_reg(E1000_REG_RDT, tail);
    }
    
    if (cause & E1000_ICR_LSC) {
        if (e1000_read_reg(E1000_REG_STATUS) & E1000_STATUS_LU) {
            PRINT(GREEN, BLACK, "[E1000] Link up\n");
        } else {
            PRINT(YELLOW, BLACK, "[E1000] Link down\n");
        }
    }
}

void e1000_get_mac_address(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = e1000_dev.mac_addr[i];
    }
}

int e1000_link_status(void) {
    return (e1000_read_reg(E1000_REG_STATUS) & E1000_STATUS_LU) ? 1 : 0;
}