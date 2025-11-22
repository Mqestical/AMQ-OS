#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "memory.h"
#include "serial.h"

#define CURSOR_BLINK_RATE 50000

extern volatile uint32_t interrupt_counter;
extern volatile uint8_t last_scancode;
extern volatile uint8_t scancode_write_pos;
extern volatile uint8_t scancode_read_pos;
extern volatile int serial_initialized;
extern void process_keyboard_buffer(void);
extern char* get_input_and_reset(void);
extern int input_available(void);

void draw_cursor(int visible) {
    if (visible) {
        draw_char(cursor.x, cursor.y, '_', cursor.fg_color, cursor.bg_color);
    } else {
        draw_char(cursor.x, cursor.y, ' ', cursor.fg_color, cursor.bg_color);
    }
}

// String compare helper
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// Process user command
void process_command(char* cmd) {
    // Skip empty input
    if (cmd[0] == '\0') return;
    
    char hello_cmd[] = "hello";
    char help_cmd[] = "help";
    char clear_cmd[] = "clear";
    char echo_prefix[] = "echo ";
    
    if (strcmp(cmd, hello_cmd) == 0) {
        char response[] = "Hello from AMQ OS!\n";
        printk(0xFF00FF00, 0x000000, response);
    } else if (strcmp(cmd, help_cmd) == 0) {
        char help[] = "Available commands:\n  hello - Say hello\n  help - Show this\n  clear - Clear screen\n  echo <text> - Echo text\n";
        printk(0xFFFFFFFF, 0x000000, help);
    } else if (strcmp(cmd, clear_cmd) == 0) {
        ClearScreen(0x000000);
        SetCursorPos(0, 0);
    } else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == ' ') {
        // Echo command
        char* text = cmd + 5;
        printk(0xFFFFFFFF, 0x000000, text);
        printc('\n');
    } else {
        char unknown[] = "Unknown command: ";
        char try_help[] = "\nTry 'help' for available commands\n";
        printk(0xFFFF0000, 0x000000, unknown);
        printk(0xFFFF0000, 0x000000, cmd);
        printk(0xFFFF0000, 0x000000, try_help);
    }
}

void run_text_demo(void) {
    char line1[] = "==========================================\n";
    char title[] = "    AMQ Operating System v0.1\n";
    char line2[] = "==========================================\n";
    char welcome[] = "Welcome! Type 'help' for commands.\n\n";
    char prompt[] = "> ";

    printk(0x00FFFFFF, 0x000000, line1);
    printk(0x00FFFFFF, 0x000000, title);
    printk(0x00FFFFFF, 0x000000, line2);
    printk(0xFFFFFFFF, 0x000000, welcome);
    printk(0xFF00FF00, 0x000000, prompt);

    int cursor_visible = 1;
    int cursor_timer = 0;

    while (1) {
        cursor_timer++;
        
        // Process keyboard input
        process_keyboard_buffer();
        
        // Check if user pressed Enter
        if (input_available()) {
            char* input = get_input_and_reset();
            
            // Process the command
            process_command(input);
            
            // Show prompt again
            printk(0xFF00FF00, 0x000000, prompt);
        }
        
        // Blink cursor
        if (cursor_timer >= CURSOR_BLINK_RATE) {
            cursor_timer = 0;
            cursor_visible = !cursor_visible;
            draw_cursor(cursor_visible);
        }
        for (volatile int i = 0; i < 4000; i++);
    }
}

void init_shell(void) {
    ClearScreen(0x000000);
    SetCursorPos(0, 0);
    run_text_demo();
}