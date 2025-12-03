#include "print.h"
#include "string_helpers.h"
#include "IO.h"
#include "sleep.h"

typedef unsigned long long ULL_t;

#define MOUSE_CHECK 0x64
#define MOUSE_AUXILIARY_PORT 0x60
#define USED 0xACE
#define ON 0x1
#define OFF 0x0

// Screen bounds (adjust to your actual resolution)
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

short status;
static int cursor_x = SCREEN_WIDTH / 2;   // Current screen position
static int cursor_y = SCREEN_HEIGHT / 2;

void mcursor(int x, int y, int prev_x, int prev_y, short signal);
void overwrite_cursor(int prev_x, int prev_y);

/* NOTE: MOUSE USES THE SAME PORT AS KEYBOARD, 0x64 PORT IS USED TO DIFFERENTIATE! */

void mouse(void) {
    static uint8_t packet[3];
    static uint8_t packet_index = 0;
    
    // One-time initialization
    if (!status) {
        while (inb(MOUSE_CHECK) & 2);             // wait for input buffer empty
        outb(0x64, 0xD4);                          // tell controller: next is mouse command
        outb(MOUSE_AUXILIARY_PORT, 0xF4);         // enable reporting
        
        while (!(inb(MOUSE_CHECK) & 1));          // wait for ACK
        inb(MOUSE_AUXILIARY_PORT);                // read ACK

        status = USED;
    }

    // Non-blocking: only read if data is available
    while (inb(MOUSE_CHECK) & 1) {
        packet[packet_index++] = inb(MOUSE_AUXILIARY_PORT);
        
        // Process when we have a complete 3-byte packet
        if (packet_index == 3) {
            uint8_t b1 = packet[0];
            uint8_t b2 = packet[1];
            uint8_t b3 = packet[2];
            
            // Extract movement deltas with sign extension
            int delta_x = b2;
            int delta_y = b3;
            
            // Check sign bits and extend to signed integers
            if (b1 & 0x10) delta_x |= 0xFFFFFF00;  // X is negative
            if (b1 & 0x20) delta_y |= 0xFFFFFF00;  // Y is negative
            
            // PS/2 Y axis is inverted
            delta_y = -delta_y;
            
            // Store previous position
            int prev_x = cursor_x;
            int prev_y = cursor_y;
            
            // Update cursor position with bounds checking
            cursor_x += delta_x;
            cursor_y += delta_y;
            
            // Clamp to screen boundaries
            if (cursor_x < 0) cursor_x = 0;
            if (cursor_y < 0) cursor_y = 0;
            if (cursor_x >= SCREEN_WIDTH - 16) cursor_x = SCREEN_WIDTH - 16;
            if (cursor_y >= SCREEN_HEIGHT - 16) cursor_y = SCREEN_HEIGHT - 16;
            
            // Update display if cursor moved
            if (cursor_x != prev_x || cursor_y != prev_y) {
                overwrite_cursor(prev_x, prev_y);
                mcursor(cursor_x, cursor_y, 0, 0, OFF);
            }
            
            // Reset for next packet
            packet_index = 0;
        }
    }
    // If no data available, function returns immediately
}


void mcursor(int x, int y, int prev_x, int prev_y, short signal) {
    (void)prev_x;  // Unused in this version
    (void)prev_y;
    (void)signal;
    
    int height = 16;
    unsigned int MOUSE_RED = 0xFF0000;

    // LS (vertical)
    for (int i = 0; i < height; i++) {
        put_pixel(x, y + i, MOUSE_RED);
    }

    // RS (diagonal)
    for (int i = 0; i < height; i++) {
        put_pixel(x + i, y + i, MOUSE_RED);
    }

    // BS (horizontal)
    for (int i = 0; i < height; i++) {
        put_pixel(x + i, y + height - 1, MOUSE_RED);
    }

    // Fill interior
    for (int yy = 1; yy < height; yy++) {
        for (int xx = 1; xx < yy; xx++) {
            put_pixel(x + xx, y + yy, MOUSE_RED);
        }
    }
}

void overwrite_cursor(int prev_x, int prev_y) {
    int overwrite_height = 16;

    // overwrite LS
    for (int i = 0; i < overwrite_height; i++) {
        put_pixel(prev_x, prev_y + i, 0x000000);
    }
    
    // overwrite RS
    for (int i = 0; i < overwrite_height; i++) {
        put_pixel(prev_x + i, prev_y + i, 0x000000);
    }
    
    // overwrite BS 
    for (int i = 0; i < overwrite_height; i++) {
        put_pixel(prev_x + i, prev_y + overwrite_height - 1, 0x000000);
    }
    
    // overwrite filling
    for (int yy = 1; yy < overwrite_height; yy++) {
        for (int xx = 1; xx < yy; xx++) {
            put_pixel(prev_x + xx, prev_y + yy, 0x000000);
        }
    }
}