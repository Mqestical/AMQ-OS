#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stddef.h>

// Intel 8254x Register Offsets
#define E1000_REG_CTRL      0x0000  // Device Control
#define E1000_REG_STATUS    0x0008  // Device Status
#define E1000_REG_EECD      0x0010  // EEPROM Control
#define E1000_REG_EERD      0x0014  // EEPROM Read
#define E1000_REG_ICR       0x00C0  // Interrupt Cause Read
#define E1000_REG_IMS       0x00D0  // Interrupt Mask Set
#define E1000_REG_IMC       0x00D8  // Interrupt Mask Clear
#define E1000_REG_RCTL      0x0100  // Receive Control
#define E1000_REG_TCTL      0x0400  // Transmit Control
#define E1000_REG_RDBAL     0x2800  // RX Descriptor Base Low
#define E1000_REG_RDBAH     0x2804  // RX Descriptor Base High
#define E1000_REG_RDLEN     0x2808  // RX Descriptor Length
#define E1000_REG_RDH       0x2810  // RX Descriptor Head
#define E1000_REG_RDT       0x2818  // RX Descriptor Tail
#define E1000_REG_TDBAL     0x3800  // TX Descriptor Base Low
#define E1000_REG_TDBAH     0x3804  // TX Descriptor Base High
#define E1000_REG_TDLEN     0x3808  // TX Descriptor Length
#define E1000_REG_TDH       0x3810  // TX Descriptor Head
#define E1000_REG_TDT       0x3818  // TX Descriptor Tail
#define E1000_REG_RAL0      0x5400  // Receive Address Low
#define E1000_REG_RAH0      0x5404  // Receive Address High

// Control Register Bits
#define E1000_CTRL_FD       (1 << 0)   // Full Duplex
#define E1000_CTRL_ASDE     (1 << 5)   // Auto-Speed Detection Enable
#define E1000_CTRL_SLU      (1 << 6)   // Set Link Up
#define E1000_CTRL_RST      (1 << 26)  // Device Reset
#define E1000_CTRL_PHY_RST  (1 << 31)  // PHY Reset

// Status Register Bits
#define E1000_STATUS_LU     (1 << 1)   // Link Up

// RCTL Bits
#define E1000_RCTL_EN       (1 << 1)   // Receive Enable
#define E1000_RCTL_SBP      (1 << 2)   // Store Bad Packets
#define E1000_RCTL_UPE      (1 << 3)   // Unicast Promiscuous
#define E1000_RCTL_MPE      (1 << 4)   // Multicast Promiscuous
#define E1000_RCTL_LPE      (1 << 5)   // Long Packet Enable
#define E1000_RCTL_BAM      (1 << 15)  // Broadcast Accept Mode
#define E1000_RCTL_BSEX     (1 << 25)  // Buffer Size Extension
#define E1000_RCTL_SECRC    (1 << 26)  // Strip Ethernet CRC

// RCTL Buffer Size (with BSEX=1)
#define E1000_RCTL_BSIZE_4096   (0x3 << 16)

// TCTL Bits
#define E1000_TCTL_EN       (1 << 1)   // Transmit Enable
#define E1000_TCTL_PSP      (1 << 3)   // Pad Short Packets
#define E1000_TCTL_CT_SHIFT 4          // Collision Threshold
#define E1000_TCTL_COLD_SHIFT 12       // Collision Distance

// Interrupt Bits
#define E1000_ICR_TXDW      (1 << 0)   // Transmit Descriptor Written Back
#define E1000_ICR_TXQE      (1 << 1)   // Transmit Queue Empty
#define E1000_ICR_LSC       (1 << 2)   // Link Status Change
#define E1000_ICR_RXDMT0    (1 << 4)   // Receive Descriptor Min Threshold
#define E1000_ICR_RXO       (1 << 6)   // Receiver Overrun
#define E1000_ICR_RXT0      (1 << 7)   // Receiver Timer Interrupt

// Transmit Descriptor Command
#define E1000_TXD_CMD_EOP   (1 << 0)   // End of Packet
#define E1000_TXD_CMD_IFCS  (1 << 1)   // Insert FCS
#define E1000_TXD_CMD_RS    (1 << 3)   // Report Status
#define E1000_TXD_CMD_DEXT  (1 << 5)   // Extension

// Transmit Descriptor Status
#define E1000_TXD_STAT_DD   (1 << 0)   // Descriptor Done

// Receive Descriptor Status
#define E1000_RXD_STAT_DD   (1 << 0)   // Descriptor Done
#define E1000_RXD_STAT_EOP  (1 << 1)   // End of Packet

// Descriptor counts
#define E1000_NUM_RX_DESC   32
#define E1000_NUM_TX_DESC   8
#define E1000_RX_BUFFER_SIZE 4096
#define E1000_TX_BUFFER_SIZE 4096

// Transmit Descriptor
typedef struct {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

// Receive Descriptor
typedef struct {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

// E1000 Device Structure
typedef struct {
    uint64_t mmio_base;
    uint32_t mmio_size;
    uint8_t  mac_addr[6];
    
    e1000_rx_desc_t *rx_descs;
    uint8_t **rx_buffers;
    uint32_t rx_cur;
    
    e1000_tx_desc_t *tx_descs;
    uint8_t **tx_buffers;
    uint32_t tx_cur;
    
    int irq;
    int initialized;
} e1000_device_t;

// Function prototypes
int e1000_init(void);
int e1000_send_packet(const void *data, uint16_t length);
void e1000_interrupt_handler(void);
void e1000_get_mac_address(uint8_t *mac);
int e1000_link_status(void);

// Internal functions
uint32_t e1000_read_reg(uint16_t reg);
void e1000_write_reg(uint16_t reg, uint32_t value);
uint16_t e1000_read_eeprom(uint8_t addr);

#endif // E1000_H