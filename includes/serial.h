#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

// Serial port base addresses
#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

// Serial port register offsets
#define SERIAL_DATA          0    // Data register (read/write)
#define SERIAL_INT_ENABLE    1    // Interrupt enable register
#define SERIAL_FIFO_CTRL     2    // FIFO control register
#define SERIAL_LINE_CTRL     3    // Line control register
#define SERIAL_MODEM_CTRL    4    // Modem control register
#define SERIAL_LINE_STATUS   5    // Line status register
#define SERIAL_MODEM_STATUS  6    // Modem status register

// Line status register bits
#define SERIAL_LSR_DATA_READY       0x01
#define SERIAL_LSR_OVERRUN_ERROR    0x02
#define SERIAL_LSR_PARITY_ERROR     0x04
#define SERIAL_LSR_FRAMING_ERROR    0x08
#define SERIAL_LSR_BREAK_INDICATOR  0x10
#define SERIAL_LSR_TRANSMIT_EMPTY   0x20
#define SERIAL_LSR_TRANSMITTER_IDLE 0x40
#define SERIAL_LSR_IMPENDING_ERROR  0x80

// External variable for checking init status
extern volatile int serial_initialized;

// Function prototypes
void serial_init(uint16_t port);
int serial_can_write(uint16_t port);
void serial_write_byte(uint16_t port, uint8_t data);
int serial_can_read(uint16_t port);
uint8_t serial_read_byte(uint16_t port);
void serial_write_string(uint16_t port, const char* str);
void serial_write_scancode(uint8_t scancode);
void serial_process_input(void);

#endif // SERIAL_H