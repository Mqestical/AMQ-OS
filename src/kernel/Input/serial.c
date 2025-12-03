#include "serial.h"
#include "IO.h"
#include "print.h"
#include "string_helpers.h"

static volatile int shift_pressed = 0;
volatile int serial_initialized;


void serial_init(uint16_t port) {
    serial_initialized = 0;
    
    outb(port + 1, 0x00);
    
    for (volatile int i = 0; i < 1000; i++);
    
    outb(port + 3, 0x80);
    for (volatile int i = 0; i < 1000; i++);
    
    outb(port + 0, 0x01);
    for (volatile int i = 0; i < 1000; i++);
    
    outb(port + 1, 0x00);
    for (volatile int i = 0; i < 1000; i++);
    
    outb(port + 3, 0x03);
    for (volatile int i = 0; i < 1000; i++);
    
    outb(port + 2, 0xC7);
    for (volatile int i = 0; i < 1000; i++);
    
    outb(port + 4, 0x0B);
    for (volatile int i = 0; i < 1000; i++);
    
    outb(port + 4, 0x1E);
    for (volatile int i = 0; i < 1000; i++);
    
    outb(port + 0, 0xAE);
    for (volatile int i = 0; i < 1000; i++);
    
    uint8_t test = inb(port + 0);
    
    outb(port + 4, 0x0F);
    for (volatile int i = 0; i < 1000; i++);
    
    if (test == 0xAE) {
        serial_initialized = 1;
    }
}

int serial_can_write(uint16_t port) {
    if (!serial_initialized) return 0;
    uint8_t status = inb(port + SERIAL_LINE_STATUS);
    return status & SERIAL_LSR_TRANSMIT_EMPTY;
}

void serial_write_byte(uint16_t port, uint8_t data) {
    if (!serial_initialized) return;
    
    int timeout = 100000;
    while (!serial_can_write(port) && timeout > 0) {
        timeout--;
        for (volatile int i = 0; i < 10; i++);
    }
    
    if (timeout > 0) {
        outb(port + SERIAL_DATA, data);
        for (volatile int i = 0; i < 100; i++);
    }
}

int serial_can_read(uint16_t port) {
    if (!serial_initialized) return 0;
    return inb(port + SERIAL_LINE_STATUS) & SERIAL_LSR_DATA_READY;
}

uint8_t serial_read_byte(uint16_t port) {
    if (!serial_initialized) return 0;
    
    int timeout = 10000;
    while (!serial_can_read(port) && timeout > 0) {
        timeout--;
    }
    
    if (timeout > 0) {
        return inb(port + SERIAL_DATA);
    }
    return 0;
}

void serial_write_string(uint16_t port, const char* str) {
    if (!serial_initialized || !str) return;
    
    while (*str) {
        serial_write_byte(port, *str);
        str++;
    }
}

static char scancode_to_char(uint8_t scancode, int shifted) {
    if (!shifted) {
        switch(scancode) {
            case 0x02: return '1';
            case 0x03: return '2';
            case 0x04: return '3';
            case 0x05: return '4';
            case 0x06: return '6';
            case 0x07: return '6';
            case 0x08: return '7';
            case 0x09: return '8';
            case 0x0A: return '9';
            case 0x0B: return '0';
            case 0x0C: return '-';
            case 0x0D: return '=';
            case 0x0E: return '\b';
            case 0x0F: return '\t';
            case 0x10: return 'q';
            case 0x11: return 'w';
            case 0x12: return 'e';
            case 0x13: return 'r';
            case 0x14: return 't';
            case 0x15: return 'y';
            case 0x16: return 'u';
            case 0x17: return 'i';
            case 0x18: return 'o';
            case 0x19: return 'p';
            case 0x1A: return '[';
            case 0x1B: return ']';
            case 0x1C: return '\n';
            case 0x1E: return 'a';
            case 0x1F: return 's';
            case 0x20: return 'd';
            case 0x21: return 'f';
            case 0x22: return 'g';
            case 0x23: return 'h';
            case 0x24: return 'j';
            case 0x25: return 'k';
            case 0x26: return 'l';
            case 0x27: return ';';
            case 0x28: return '\'';
            case 0x29: return '`';
            case 0x2B: return '\\';
            case 0x2C: return 'z';
            case 0x2D: return 'x';
            case 0x2E: return 'c';
            case 0x2F: return 'v';
            case 0x30: return 'b';
            case 0x31: return 'n';
            case 0x32: return 'm';
            case 0x33: return ',';
            case 0x34: return '.';
            case 0x35: return '/';
            case 0x39: return ' ';
            
            default: return 0;
        }
    } else {
        switch(scancode) {
            case 0x02: return '!';
            case 0x03: return '@';
            case 0x04: return '#';
            case 0x05: return '$';
            case 0x06: return '%';
            case 0x07: return '^';
            case 0x08: return '&';
            case 0x09: return '*';
            case 0x0A: return '(';
            case 0x0B: return ')';
            case 0x10: return 'Q';
            case 0x11: return 'W';
            case 0x12: return 'E';
            case 0x13: return 'R';
            case 0x14: return 'T';
            case 0x15: return 'Y';
            case 0x16: return 'U';
            case 0x17: return 'I';
            case 0x18: return 'O';
            case 0x19: return 'P';
            case 0x1E: return 'A';
            case 0x1F: return 'S';
            case 0x20: return 'D';
            case 0x21: return 'F';
            case 0x22: return 'G';
            case 0x23: return 'H';
            case 0x24: return 'J';
            case 0x25: return 'K';
            case 0x26: return 'L';
            case 0x2C: return 'Z';
            case 0x2D: return 'X';
            case 0x2E: return 'C';
            case 0x2F: return 'V';
            case 0x30: return 'B';
            case 0x31: return 'N';
            case 0x32: return 'M';
            default: return 0;
        }
    }
}

void serial_write_scancode(uint8_t scancode) {
    if (!serial_initialized) return;
    
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }
    
    if (scancode & 0x80) return;
    
    char ascii = scancode_to_char(scancode, shift_pressed);
    if (ascii) {
        serial_write_byte(COM1, ascii);
    }
}

void serial_process_input(void) {
    if (!serial_initialized) return;
    
    if (serial_can_read(COM1)) {
        uint8_t data = serial_read_byte(COM1);
        printc(data);
        serial_write_byte(COM1, data);
    }
}