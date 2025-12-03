#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

#define SERIAL_DATA          0
#define SERIAL_INT_ENABLE    1
#define SERIAL_FIFO_CTRL     2
#define SERIAL_LINE_CTRL     3
#define SERIAL_MODEM_CTRL    4
#define SERIAL_LINE_STATUS   5
#define SERIAL_MODEM_STATUS  6

#define SERIAL_LSR_DATA_READY       0x01
#define SERIAL_LSR_OVERRUN_ERROR    0x02
#define SERIAL_LSR_PARITY_ERROR     0x04
#define SERIAL_LSR_FRAMING_ERROR    0x08
#define SERIAL_LSR_BREAK_INDICATOR  0x10
#define SERIAL_LSR_TRANSMIT_EMPTY   0x20
#define SERIAL_LSR_TRANSMITTER_IDLE 0x40
#define SERIAL_LSR_IMPENDING_ERROR  0x80

extern volatile int serial_initialized;

void serial_init(uint16_t port);
int serial_can_write(uint16_t port);
void serial_write_byte(uint16_t port, uint8_t data);
int serial_can_read(uint16_t port);
uint8_t serial_read_byte(uint16_t port);
void serial_write_string(uint16_t port, const char* str);
void serial_write_scancode(uint8_t scancode);
void serial_process_input(void);

#endif
