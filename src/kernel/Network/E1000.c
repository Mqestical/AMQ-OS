#include "E1000.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"
#include "PCI.h"
#include "net.h"

static e1000_device_t e1000_dev;

#define E1000_VENDOR_ID  0x8086
#define E1000_DEV_82540EM 0x100E
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
    
    tmp = ((uint32_t)addr << 8) | 0x1;
    e1000_write_reg(E1000_REG_EERD, tmp);
    
    while (1) {
        tmp = e1000_read_reg(E1000_REG_EERD);
        if (tmp & 0x10) break;
    }
    
    return (uint16_t)(tmp >> 16);
}

static int e1000_detect_pci(void) {
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
                    cmd |= 0x07;  // Bus master + Memory space + I/O space
                    pci_write_word(bus, dev, func, 0x04, cmd);
                    
                    // Read BAR0 (MMIO base)
                    uint32_t bar0 = pci_read_dword(bus, dev, func, 0x10);
                    e1000_dev.mmio_base = bar0 & ~0xF;
                    e1000_dev.mmio_size = 0x20000;
                    
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
    // Disable interrupts
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFF);
    e1000_read_reg(E1000_REG_ICR);
    
    // Global reset
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    e1000_write_reg(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
    
    // Wait for reset (longer delay for VirtualBox)
    for (volatile int i = 0; i < 5000000; i++);
    
    // Disable interrupts again
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFF);
    e1000_read_reg(E1000_REG_ICR);
    
    PRINT(WHITE, BLACK, "[E1000] Reset complete\n");
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
    e1000_dev.rx_descs = (e1000_rx_desc_t*)kmalloc(
        sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC);
    e1000_dev.rx_buffers = (uint8_t**)kmalloc(
        sizeof(uint8_t*) * E1000_NUM_RX_DESC);
    
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        e1000_dev.rx_buffers[i] = (uint8_t*)kmalloc(E1000_RX_BUFFER_SIZE);
        e1000_dev.rx_descs[i].buffer_addr = (uint64_t)e1000_dev.rx_buffers[i];
        e1000_dev.rx_descs[i].status = 0;
    }
    
    e1000_dev.rx_cur = 0;
    
    e1000_write_reg(E1000_REG_RDBAL, (uint64_t)e1000_dev.rx_descs & 0xFFFFFFFF);
    e1000_write_reg(E1000_REG_RDBAH, (uint64_t)e1000_dev.rx_descs >> 32);
    e1000_write_reg(E1000_REG_RDLEN, E1000_NUM_RX_DESC * 16);
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    
    // VirtualBox-compatible RX configuration
    uint32_t rctl = E1000_RCTL_EN |       // Enable
                    E1000_RCTL_BAM |      // Broadcast enable
                    E1000_RCTL_BSEX |     // Buffer size extension
                    E1000_RCTL_BSIZE_4096 | // 4KB buffers
                    E1000_RCTL_SECRC;     // Strip CRC
    e1000_write_reg(E1000_REG_RCTL, rctl);
}

static void e1000_setup_tx(void) {
    e1000_dev.tx_descs = (e1000_tx_desc_t*)kmalloc(
        sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC);
    e1000_dev.tx_buffers = (uint8_t**)kmalloc(
        sizeof(uint8_t*) * E1000_NUM_TX_DESC);
    
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        e1000_dev.tx_buffers[i] = (uint8_t*)kmalloc(E1000_TX_BUFFER_SIZE);
        e1000_dev.tx_descs[i].buffer_addr = (uint64_t)e1000_dev.tx_buffers[i];
        e1000_dev.tx_descs[i].status = E1000_TXD_STAT_DD;
        e1000_dev.tx_descs[i].cmd = 0;
    }
    
    e1000_dev.tx_cur = 0;
    
    e1000_write_reg(E1000_REG_TDBAL, (uint64_t)e1000_dev.tx_descs & 0xFFFFFFFF);
    e1000_write_reg(E1000_REG_TDBAH, (uint64_t)e1000_dev.tx_descs >> 32);
    e1000_write_reg(E1000_REG_TDLEN, E1000_NUM_TX_DESC * 16);
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);
    
    uint32_t tctl = E1000_TCTL_EN |
                    E1000_TCTL_PSP |
                    (15 << E1000_TCTL_CT_SHIFT) |
                    (64 << E1000_TCTL_COLD_SHIFT);
    e1000_write_reg(E1000_REG_TCTL, tctl);
}

int e1000_init(void) {
    PRINT(CYAN, BLACK, "\n[E1000] Initializing Intel 8254x driver...\n");
    
    if (e1000_detect_pci() != 0) {
        PRINT(YELLOW, BLACK, "[E1000] No device found\n");
        return -1;
    }
    
    e1000_reset();
    e1000_read_mac();
    
    // Set MAC address in RAL/RAH
    uint32_t ral = ((uint32_t)e1000_dev.mac_addr[3] << 24) |
                   ((uint32_t)e1000_dev.mac_addr[2] << 16) |
                   ((uint32_t)e1000_dev.mac_addr[1] << 8) |
                   e1000_dev.mac_addr[0];
    uint32_t rah = ((uint32_t)e1000_dev.mac_addr[5] << 8) |
                   e1000_dev.mac_addr[4] | (1 << 31);
    
    e1000_write_reg(E1000_REG_RAL0, ral);
    e1000_write_reg(E1000_REG_RAH0, rah);
    
    // Clear multicast table array
    for (int i = 0; i < 128; i++) {
        e1000_write_reg(0x5200 + (i * 4), 0);
    }
    
    // Setup RX and TX BEFORE link up
    e1000_setup_rx();
    e1000_setup_tx();
    
    // ===== CRITICAL: Link Configuration for VirtualBox =====
    PRINT(WHITE, BLACK, "[E1000] Configuring link...\n");
    
    // Read and modify CTRL register for link up
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    
    // Set bits for link: SLU (Set Link Up) and ASDE (Auto-Speed Detection)
    ctrl |= E1000_CTRL_SLU;    // Set Link Up - THIS IS CRITICAL
    ctrl |= E1000_CTRL_ASDE;   // Auto-speed detection
    
    // Write back to enable link
    e1000_write_reg(E1000_REG_CTRL, ctrl);
    
    PRINT(WHITE, BLACK, "[E1000] CTRL configured: 0x%08x\n", ctrl);
    
    // Give PHY time to negotiate (IMPORTANT for VirtualBox!)
    PRINT(WHITE, BLACK, "[E1000] Waiting for PHY...\n");
    for (volatile int i = 0; i < 3000000; i++);
    
    // Read STATUS to check initial state
    uint32_t status = e1000_read_reg(E1000_REG_STATUS);
    PRINT(WHITE, BLACK, "[E1000] Initial STATUS: 0x%08x\n", status);
    
    // Enable link state change interrupt
    e1000_write_reg(E1000_REG_IMS, E1000_ICR_LSC | E1000_ICR_RXT0);
    
    // Wait for link with detailed monitoring
    PRINT(WHITE, BLACK, "[E1000] Waiting for link");
    int link_up = 0;
    
    for (int i = 0; i < 300; i++) {
        for (volatile int j = 0; j < 1000000; j++);
        
        status = e1000_read_reg(E1000_REG_STATUS);
        
        if (status & E1000_STATUS_LU) {
            PRINT(WHITE, BLACK, " UP!\n");
            PRINT(GREEN, BLACK, "[E1000] Link established!\n");
            PRINT(WHITE, BLACK, "[E1000] STATUS: 0x%08x\n", status);
            
            // Show link speed
            uint32_t speed = (status >> 6) & 0x3;
            const char *speed_str[] = {"10Mbps", "100Mbps", "1000Mbps", "1000Mbps"};
            PRINT(WHITE, BLACK, "[E1000] Speed: %s, ", speed_str[speed]);
            PRINT(WHITE, BLACK, "Duplex: %s\n", (status & 0x1) ? "Full" : "Half");
            
            link_up = 1;
            break;
        }
        
        if (i % 30 == 0) {
            PRINT(WHITE, BLACK, ".");
        }
        
        if (i % 100 == 0 && i > 0) {
            PRINT(WHITE, BLACK, "\n[E1000] STATUS: 0x%08x (waiting...)", status);
        }
    }
    
    if (!link_up) {
        PRINT(WHITE, BLACK, " FAILED!\n");
        
        status = e1000_read_reg(E1000_REG_STATUS);
        ctrl = e1000_read_reg(E1000_REG_CTRL);
        
        PRINT(RED, BLACK, "\n[E1000] ERROR: Link did not come up!\n");
        PRINT(WHITE, BLACK, "[E1000] Final STATUS: 0x%08x\n", status);
        PRINT(WHITE, BLACK, "[E1000] Final CTRL:   0x%08x\n", ctrl);
        
        PRINT(YELLOW, BLACK, "\n=== VirtualBox Troubleshooting ===\n");
        PRINT(YELLOW, BLACK, "1. Open VM Settings -> Network -> Adapter 1\n");
        PRINT(YELLOW, BLACK, "2. Verify 'Enable Network Adapter' is CHECKED\n");
        PRINT(YELLOW, BLACK, "3. Verify 'Cable Connected' is CHECKED (CRITICAL!)\n");
        PRINT(YELLOW, BLACK, "4. Attached to: NAT (recommended for testing)\n");
        PRINT(YELLOW, BLACK, "5. Adapter Type: Intel PRO/1000 MT Desktop (82540EM)\n");
        PRINT(YELLOW, BLACK, "6. Click OK to save settings\n");
        PRINT(YELLOW, BLACK, "7. Fully power off VM and restart\n");
        PRINT(YELLOW, BLACK, "8. If still failing, try Bridged or Host-only adapter\n");
        PRINT(YELLOW, BLACK, "================================\n\n");
    }
    
    e1000_dev.initialized = 1;
    net_register_device(e1000_dev.mac_addr);
    
    PRINT(GREEN, BLACK, "[E1000] Driver initialization complete\n");
    return 0;
}

int e1000_send_packet(const void *data, uint16_t length) {
    if (!e1000_dev.initialized || length > E1000_TX_BUFFER_SIZE) {
        return -1;
    }
    
    uint32_t tail = e1000_dev.tx_cur;
    e1000_tx_desc_t *desc = &e1000_dev.tx_descs[tail];
    
    // Wait for descriptor
    int timeout = 10000;
    while (!(desc->status & E1000_TXD_STAT_DD) && timeout-- > 0) {
        for (volatile int i = 0; i < 100; i++);
    }
    
    if (timeout <= 0) {
        PRINT(YELLOW, BLACK, "[E1000] TX timeout\n");
        return -1;
    }
    
    // Copy data
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
    if (!e1000_dev.initialized) return;
    
    uint32_t cause = e1000_read_reg(E1000_REG_ICR);
    
    if (cause & E1000_ICR_RXT0) {
        uint32_t idx = e1000_dev.rx_cur;
        
        while (e1000_dev.rx_descs[idx].status & E1000_RXD_STAT_DD) {
            e1000_rx_desc_t *desc = &e1000_dev.rx_descs[idx];
            uint16_t length = desc->length;
            uint8_t *data = (uint8_t*)desc->buffer_addr;
            
            net_receive_packet(data, length);
            
            desc->status = 0;
            idx = (idx + 1) % E1000_NUM_RX_DESC;
        }
        
        e1000_dev.rx_cur = idx;
        uint32_t tail = (idx == 0) ? E1000_NUM_RX_DESC - 1 : idx - 1;
        e1000_write_reg(E1000_REG_RDT, tail);
    }
    
    if (cause & E1000_ICR_LSC) {
        if (e1000_read_reg(E1000_REG_STATUS) & E1000_STATUS_LU) {
            PRINT(GREEN, BLACK, "[E1000] Link state changed: UP\n");
        } else {
            PRINT(YELLOW, BLACK, "[E1000] Link state changed: DOWN\n");
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