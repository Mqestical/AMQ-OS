#include "E1000.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"
#include "PCI.h"
#include "net.h"

static e1000_device_t e1000_dev;

#define REG_CTRL     0x0000
#define REG_STATUS   0x0008
#define REG_EERD     0x0014
#define REG_ICR      0x00C0
#define REG_IMS      0x00D0
#define REG_RCTL     0x0100
#define REG_TCTL     0x0400
#define REG_RDBAL    0x2800
#define REG_RDBAH    0x2804
#define REG_RDLEN    0x2808
#define REG_RDH      0x2810
#define REG_RDT      0x2818
#define REG_TDBAL    0x3800
#define REG_TDBAH    0x3804
#define REG_TDLEN    0x3808
#define REG_TDH      0x3810
#define REG_TDT      0x3818
#define REG_RAL      0x5400
#define REG_RAH      0x5404

uint32_t e1000_read_reg(uint16_t reg) {
    return *(volatile uint32_t*)(e1000_dev.mmio_base + reg);
}

void e1000_write_reg(uint16_t reg, uint32_t val) {
    *(volatile uint32_t*)(e1000_dev.mmio_base + reg) = val;
}

uint16_t e1000_read_eeprom(uint8_t addr) {
    e1000_write_reg(REG_EERD, 1 | ((uint32_t)addr << 8));
    uint32_t tmp;
    while (!((tmp = e1000_read_reg(REG_EERD)) & (1 << 4)));
    return (tmp >> 16) & 0xFFFF;
}

int e1000_init(void) {
    PRINT(CYAN, BLACK, "\n[E1000] Init\n");


    int found = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_read_word(bus, dev, 0, 0);
            uint16_t device = pci_read_word(bus, dev, 0, 2);

            if (vendor == 0x8086 && device == 0x100E) {
                PRINT(GREEN, BLACK, "[E1000] Found at %d:%d\n", bus, dev);

                uint32_t bar0 = pci_read_dword(bus, dev, 0, 0x10);
                e1000_dev.mmio_base = bar0 & ~0xF;


                uint16_t cmd = pci_read_word(bus, dev, 0, 4);
                pci_write_word(bus, dev, 0, 4, cmd | 0x07);

                found = 1;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        PRINT(RED, BLACK, "[E1000] Not found\n");
        return -1;
    }

    PRINT(WHITE, BLACK, "[E1000] MMIO: 0x%llx\n", e1000_dev.mmio_base);


    uint16_t mac[3];
    mac[0] = e1000_read_eeprom(0);
    mac[1] = e1000_read_eeprom(1);
    mac[2] = e1000_read_eeprom(2);

    e1000_dev.mac_addr[0] = mac[0] & 0xFF;
    e1000_dev.mac_addr[1] = mac[0] >> 8;
    e1000_dev.mac_addr[2] = mac[1] & 0xFF;
    e1000_dev.mac_addr[3] = mac[1] >> 8;
    e1000_dev.mac_addr[4] = mac[2] & 0xFF;
    e1000_dev.mac_addr[5] = mac[2] >> 8;

    PRINT(GREEN, BLACK, "[E1000] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
          e1000_dev.mac_addr[0], e1000_dev.mac_addr[1],
          e1000_dev.mac_addr[2], e1000_dev.mac_addr[3],
          e1000_dev.mac_addr[4], e1000_dev.mac_addr[5]);


    uint32_t ctrl = e1000_read_reg(REG_CTRL);
    e1000_write_reg(REG_CTRL, ctrl | (1 << 6) | (1 << 5));

    for (volatile int i = 0; i < 1000000; i++);

    uint32_t status = e1000_read_reg(REG_STATUS);
    if (status & 2) {
        PRINT(GREEN, BLACK, "[E1000] Link UP\n");
    } else {
        PRINT(YELLOW, BLACK, "[E1000] Link DOWN (status=0x%x)\n", status);
    }


    e1000_dev.rx_descs = (e1000_rx_desc_t*)kmalloc(sizeof(e1000_rx_desc_t) * 32);
    e1000_dev.rx_buffers = (uint8_t**)kmalloc(sizeof(uint8_t*) * 32);

    for (int i = 0; i < 32; i++) {
        e1000_dev.rx_buffers[i] = (uint8_t*)kmalloc(8192);
        e1000_dev.rx_descs[i].buffer_addr = (uint64_t)e1000_dev.rx_buffers[i];
        e1000_dev.rx_descs[i].length = 0;
        e1000_dev.rx_descs[i].checksum = 0;
        e1000_dev.rx_descs[i].status = 0;
        e1000_dev.rx_descs[i].errors = 0;
        e1000_dev.rx_descs[i].special = 0;
    }

    e1000_write_reg(REG_RDBAL, (uint64_t)e1000_dev.rx_descs & 0xFFFFFFFF);
    e1000_write_reg(REG_RDBAH, (uint64_t)e1000_dev.rx_descs >> 32);
    e1000_write_reg(REG_RDLEN, 32 * 16);
    e1000_write_reg(REG_RDH, 0);
    e1000_write_reg(REG_RDT, 31);

    e1000_dev.rx_cur = 0;


    e1000_write_reg(REG_RCTL, (1 << 1) | (1 << 15) | (1 << 25) | (1 << 26));

    PRINT(GREEN, BLACK, "[E1000] RX enabled\n");


    e1000_dev.tx_descs = (e1000_tx_desc_t*)kmalloc(sizeof(e1000_tx_desc_t) * 32);
    e1000_dev.tx_buffers = (uint8_t**)kmalloc(sizeof(uint8_t*) * 32);

    for (int i = 0; i < 32; i++) {
        e1000_dev.tx_buffers[i] = (uint8_t*)kmalloc(8192);
        e1000_dev.tx_descs[i].buffer_addr = (uint64_t)e1000_dev.tx_buffers[i];
        e1000_dev.tx_descs[i].length = 0;
        e1000_dev.tx_descs[i].cso = 0;
        e1000_dev.tx_descs[i].cmd = 0;
        e1000_dev.tx_descs[i].status = 1;
        e1000_dev.tx_descs[i].css = 0;
        e1000_dev.tx_descs[i].special = 0;
    }

    e1000_write_reg(REG_TDBAL, (uint64_t)e1000_dev.tx_descs & 0xFFFFFFFF);
    e1000_write_reg(REG_TDBAH, (uint64_t)e1000_dev.tx_descs >> 32);
    e1000_write_reg(REG_TDLEN, 32 * 16);
    e1000_write_reg(REG_TDH, 0);
    e1000_write_reg(REG_TDT, 0);

    e1000_dev.tx_cur = 0;


    e1000_write_reg(REG_TCTL, (1 << 1) | (1 << 3) | (15 << 4) | (64 << 12));

    PRINT(GREEN, BLACK, "[E1000] TX enabled\n");


    uint32_t ral = e1000_dev.mac_addr[0] |
                   (e1000_dev.mac_addr[1] << 8) |
                   (e1000_dev.mac_addr[2] << 16) |
                   (e1000_dev.mac_addr[3] << 24);
    uint32_t rah = e1000_dev.mac_addr[4] |
                   (e1000_dev.mac_addr[5] << 8) |
                   (1 << 31);

    e1000_write_reg(REG_RAL, ral);
    e1000_write_reg(REG_RAH, rah);


    e1000_write_reg(REG_IMS, 0xFF);

    e1000_dev.initialized = 1;
    net_register_device(e1000_dev.mac_addr);

    PRINT(GREEN, BLACK, "[E1000] Ready\n");
    return 0;
}

int e1000_send_packet(const void *data, uint16_t len) {
    if (!e1000_dev.initialized || len > 8192) return -1;

    uint32_t tail = e1000_dev.tx_cur;
    e1000_tx_desc_t *desc = &e1000_dev.tx_descs[tail];


    while (!(desc->status & 1));


    uint8_t *buf = (uint8_t*)desc->buffer_addr;
    for (uint16_t i = 0; i < len; i++) buf[i] = ((uint8_t*)data)[i];

    desc->length = len;
    desc->cmd = (1 << 0) | (1 << 1) | (1 << 3);
    desc->status = 0;

    e1000_dev.tx_cur = (tail + 1) % 32;
    e1000_write_reg(REG_TDT, e1000_dev.tx_cur);

    return 0;
}

void e1000_interrupt_handler(void) {
    if (!e1000_dev.initialized) return;

    uint32_t cause = e1000_read_reg(REG_ICR);


    uint32_t idx = e1000_dev.rx_cur;
    int got_packets = 0;

    while (e1000_dev.rx_descs[idx].status & 1) {
        e1000_rx_desc_t *desc = &e1000_dev.rx_descs[idx];
        uint16_t len = desc->length;
        uint8_t *data = (uint8_t*)desc->buffer_addr;

        net_receive_packet(data, len);

        desc->status = 0;
        idx = (idx + 1) % 32;
        got_packets++;
    }

    e1000_dev.rx_cur = idx;
    e1000_write_reg(REG_RDT, (idx == 0) ? 31 : idx - 1);
}

void e1000_get_mac_address(uint8_t *mac) {
    for (int i = 0; i < 6; i++) mac[i] = e1000_dev.mac_addr[i];
}

int e1000_link_status(void) {
    return (e1000_read_reg(REG_STATUS) & 2) ? 1 : 0;
}