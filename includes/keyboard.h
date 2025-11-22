#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Keyboard scancodes
#define KEY_ESC         0x01
#define KEY_BACKSPACE   0x0E
#define KEY_TAB         0x0F
#define KEY_ENTER       0x1C
#define KEY_LCTRL       0x1D
#define KEY_LSHIFT      0x2A
#define KEY_RSHIFT      0x36
#define KEY_LALT        0x38
#define KEY_SPACE       0x39
#define KEY_CAPSLOCK    0x3A

// External variables
extern volatile uint8_t scancode_buffer[256];
extern volatile uint8_t scancode_read_pos;
extern volatile uint8_t scancode_write_pos;
extern volatile uint32_t interrupt_counter;
extern volatile uint8_t last_scancode;

// Function prototypes
void process_keyboard_buffer(void);

#endif // KEYBOARD_H