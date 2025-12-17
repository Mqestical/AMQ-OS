#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define INPUT_BUFFER_SIZE 256

// Keyboard buffer management
extern volatile uint8_t scancode_buffer[256];
extern volatile uint8_t scancode_read_pos;
extern volatile uint8_t scancode_write_pos;

// Input management
extern char input_buffer[INPUT_BUFFER_SIZE];
extern volatile int input_pos;
extern volatile int input_ready;

// Function declarations
void process_keyboard_buffer(void);
char* get_input_line(void);
int input_available(void);
char* get_input_and_reset(void);
void handle_backspace(void);

// Arrow key handlers (for history navigation)
void handle_arrow_up(void);
void handle_arrow_down(void);

#endif // KEYBOARD_H