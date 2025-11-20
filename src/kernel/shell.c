// shell.c - Polling-based shell with blinking cursor
#include <efi.h>
#include <efilib.h>
#include "print.h"
#include "keyboard.h"
#include "memory.h"

#define MAX_INPUT_LENGTH 256
#define CURSOR_BLINK_RATE 50000  // Delay cycles between blinks

// String comparison helper
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// String length helper
int strlen(const char *str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

// Draw cursor at current position
void draw_cursor(int visible) {
    if (visible) {
        draw_char(cursor.x, cursor.y, '_', cursor.fg_color, cursor.bg_color);
    } else {
        draw_char(cursor.x, cursor.y, ' ', cursor.fg_color, cursor.bg_color);
    }
}

// Process a command
void process_command(const char *input) {
    if (strlen(input) == 0) {
        return;
    }
    
    if (strcmp(input, "info") == 0) {
        char* msg1 = "\nAMQ OS v0.1 - Early Development\n";
        printk(0xFFFFFFFF, 0x000000, msg1);

        char* msg2 = "Features: Keyboard, Graphics, Memory, Shell\n";
        printk(0xFFFFFFFF, 0x000000, msg2);
    }
    else if (strcmp(input, "clear") == 0 || strcmp(input, "cls") == 0) {
        ClearScreen(0x000000);
        SetCursorPos(0, 0);
    }
    else if (strcmp(input, "help") == 0) {
        char* msg1 = "\nCommands:\n";
        printk(0xFFFFFFFF, 0x000000, msg1);

        char* msg2 = "  help    - Show commands\n";
        printk(0xFFFFFFFF, 0x000000, msg2);

        char* msg3 = "  info    - OS info\n";
        printk(0xFFFFFFFF, 0x000000, msg3);

        char* msg4 = "  clear   - Clear screen\n";
        printk(0xFFFFFFFF, 0x000000, msg4);

        char* msg5 = "  memstat - Memory stats\n";
        printk(0xFFFFFFFF, 0x000000, msg5);

        char* msg6 = "  echo    - Echo text\n";
        printk(0xFFFFFFFF, 0x000000, msg6);
    }
    else if (strcmp(input, "memstat") == 0) {
        memory_stats();
    }
    else if (strlen(input) >= 5 && input[0] == 'e' && input[1] == 'c' && 
             input[2] == 'h' && input[3] == 'o' && input[4] == ' ') {
        char* msg = "\n"; // optional newline before echo
        printk(0xFFFFFFFF, 0x000000, msg);

        char* echo_msg = (char*)(input + 5);
        printk(0xFFFFFFFF, 0x000000, echo_msg);
    }
    else {
        char* msg1 = "\nUnknown command (try 'help')\n";
        printk(0xFFFFFFFF, 0x000000, msg1);
    }

    char* newline = "\n";
    printk(0xFFFFFFFF, 0x000000, newline);
}


// Main shell loop with polling
void run_shell(void) {
    SetCursorPos(0,0);
    
    keyboard_init();
    
    char line1[] = "========================================\n";
    printk(0x00FFFFFF, 0x000000, line1);

    char line2[] = "  AMQ Operating System v0.1\n";
    printk(0x00FFFFFF, 0x000000, line2);
    printk(0x00FFFFFF, 0x000000, line1);
    char line3[] = "Type 'help' for commands\n\n";
    printk(0xFFFFFFFF, 0x000000, line3);
    
    char input_buffer[MAX_INPUT_LENGTH];
    int input_pos = 0;
    int cursor_visible = 1;
    int cursor_timer = 0;
    
    // Draw prompt
    printk(0x00FF00FF, 0x000000, "> ");
    
    while (1) {
        // Blink cursor
        cursor_timer++;
        if (cursor_timer >= CURSOR_BLINK_RATE) {
            cursor_timer = 0;
            cursor_visible = !cursor_visible;
            draw_cursor(cursor_visible);
        }
        
        // Poll for keyboard input (non-blocking)
        int c = keyboard_getchar();
        
        if (c != -1) {
            // Got a character - erase cursor first
            draw_cursor(0);
            
            if (c == '\n') {
                // Enter key
                input_buffer[input_pos] = '\0';
               printc('\n');
                
                process_command(input_buffer);
                
                // Reset
                input_pos = 0;
        //      printk(0x00FF00FF, 0x000000, "> ");
                cursor_visible = 1;
                cursor_timer = 0;
            }
            else if (c == '\b') {
                // Backspace
                if (input_pos > 0) {
                    input_pos--;
                    
                    if (cursor.x >= 8) {
                        cursor.x -= 8;
                    } else if (cursor.y >= 8) {
                        cursor.y -= 8;
                        cursor.x = fb.width - 8;
                    }
                    
                    draw_char(cursor.x, cursor.y, ' ', cursor.fg_color, cursor.bg_color);
                    cursor_visible = 1;
                    cursor_timer = 0;
                }
            }
            else if (c >= 32 && c < 127) {
                // Printable character
                if (input_pos < MAX_INPUT_LENGTH - 1) {
                    input_buffer[input_pos++] = c;
                    printc(c);
                    cursor_visible = 1;
                    cursor_timer = 0;
                }
            }
            
            // Redraw cursor
            draw_cursor(cursor_visible);
        }
        
        // Small delay (adjust for cursor blink speed)
        for (volatile int i = 0; i < 100; i++);
    }
}