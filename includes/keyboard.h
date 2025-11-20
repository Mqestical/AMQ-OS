// keyboard.h
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KEYBOARD_BUFFER_SIZE 256

// Keyboard state
typedef struct {
    uint8_t shift_pressed;
    uint8_t ctrl_pressed;
    uint8_t alt_pressed;
    uint8_t caps_lock;
} KeyboardState;

extern volatile uint8_t keyboard_buffer[KEYBOARD_BUFFER_SIZE];
extern volatile uint32_t keyboard_read_pos;
extern volatile uint32_t keyboard_write_pos;
extern KeyboardState kb_state;

void keyboard_init(void);
char scancode_to_ascii(uint8_t scancode);
int keyboard_getchar(void);  // Non-blocking
char keyboard_getchar_blocking(void);  // Blocking

#endif