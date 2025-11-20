#include <efi.h>
#include <efilib.h>
#include "keyboard.h"
#include "in_out_b.h"
#include "print.h"
// Circular buffer for keyboard input
volatile uint8_t keyboard_buffer[KEYBOARD_BUFFER_SIZE];
volatile uint32_t keyboard_read_pos = 0;
volatile uint32_t keyboard_write_pos = 0;

KeyboardState kb_state = {0, 0, 0, 0};

// Scancode Set 1 lookup table (US QWERTY)
static const char scancode_lowercase[] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char scancode_uppercase[] = {
    0,   27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

// Special scancodes
#define SCANCODE_LSHIFT_PRESS   0x2A
#define SCANCODE_LSHIFT_RELEASE 0xAA
#define SCANCODE_RSHIFT_PRESS   0x36
#define SCANCODE_RSHIFT_RELEASE 0xB6
#define SCANCODE_CTRL_PRESS     0x1D
#define SCANCODE_CTRL_RELEASE   0x9D
#define SCANCODE_ALT_PRESS      0x38
#define SCANCODE_ALT_RELEASE    0xB8
#define SCANCODE_CAPS_LOCK      0x3A

void keyboard_init(void) {
    keyboard_read_pos = 0;
    keyboard_write_pos = 0;
    kb_state.shift_pressed = 0;
    kb_state.ctrl_pressed = 0;
    kb_state.alt_pressed = 0;
    kb_state.caps_lock = 0;
}

char scancode_to_ascii(uint8_t scancode) {
    // Handle special keys
    if (scancode == SCANCODE_LSHIFT_PRESS || scancode == SCANCODE_RSHIFT_PRESS) {
        kb_state.shift_pressed = 1;
        return 0;
    }
    if (scancode == SCANCODE_LSHIFT_RELEASE || scancode == SCANCODE_RSHIFT_RELEASE) {
        kb_state.shift_pressed = 0;
        return 0;
    }
    if (scancode == SCANCODE_CTRL_PRESS) {
        kb_state.ctrl_pressed = 1;
        return 0;
    }
    if (scancode == SCANCODE_CTRL_RELEASE) {
        kb_state.ctrl_pressed = 0;
        return 0;
    }
    if (scancode == SCANCODE_ALT_PRESS) {
        kb_state.alt_pressed = 1;
        return 0;
    }
    if (scancode == SCANCODE_ALT_RELEASE) {
        kb_state.alt_pressed = 0;
        return 0;
    }
    if (scancode == SCANCODE_CAPS_LOCK) {
        kb_state.caps_lock = !kb_state.caps_lock;
        return 0;
    }

    // Ignore key releases (bit 7 set)
    if (scancode & 0x80) {
        return 0;
    }

    // Convert scancode to ASCII
    if (scancode < sizeof(scancode_lowercase)) {
        int use_uppercase = kb_state.shift_pressed ^ kb_state.caps_lock;
        
        // For letters, use caps lock + shift logic
        char c = use_uppercase ? scancode_uppercase[scancode] : scancode_lowercase[scancode];
        
        // For non-letters, only shift matters (not caps lock)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z')) {
            c = kb_state.shift_pressed ? scancode_uppercase[scancode] : scancode_lowercase[scancode];
        }
        
        return c;
    }

    return 0;
}

// POLLING VERSION - check keyboard port directly
void keyboard_poll(void) {
    uint8_t status = inb(0x64);
    if (status & 0x01) {  // Output buffer full
        uint8_t scancode = inb(0x60);

        // TEMP: print scancode in hex
        char buf[8];
        // Simple hex conversion: "%02X\n"
        buf[0] = "0123456789ABCDEF"[scancode >> 4];
        buf[1] = "0123456789ABCDEF"[scancode & 0xF];
        buf[2] = '\n';
        buf[3] = 0;
        printk(0xFFFFFF, 0x000000, buf);

        // Convert scancode to ASCII
        char c = scancode_to_ascii(scancode);
        if (c != 0) {
            keyboard_buffer[keyboard_write_pos] = c;
            keyboard_write_pos = (keyboard_write_pos + 1) % KEYBOARD_BUFFER_SIZE;

            // TEMP: print ASCII char received
            char ch[2] = {c, 0};
            printk(0xFFFFFF, 0x000000, ch);
        }
    }
}


// Non-blocking read
int keyboard_getchar(void) {
    // Poll keyboard first
    keyboard_poll();
    
    if (keyboard_read_pos == keyboard_write_pos) {
        return -1;  // Buffer empty
    }

    uint8_t c = keyboard_buffer[keyboard_read_pos];
    keyboard_read_pos = (keyboard_read_pos + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

// Blocking read
char keyboard_getchar_blocking(void) {
    while (1) {
        keyboard_poll();
        
        if (keyboard_read_pos != keyboard_write_pos) {
            uint8_t c = keyboard_buffer[keyboard_read_pos];
            keyboard_read_pos = (keyboard_read_pos + 1) % KEYBOARD_BUFFER_SIZE;
            return c;
        }
    }
}