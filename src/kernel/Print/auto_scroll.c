// ========== auto_scroll.c - SIMPLE AUTO SCROLL ==========
#include "auto_scroll.h"
#include "print.h"

extern Framebuffer fb;
extern Cursor cursor;

static int scroll_offset = 0;
static int max_lines = 0;
static int char_height = 8;

void auto_scroll_init(void) {
    scroll_offset = 0;
    max_lines = fb.height / char_height;
}

void auto_scroll_check(void) {
    int current_line = cursor.y / char_height;
    
    // If we're at the bottom of the screen
    if (cursor.y + char_height >= fb.height) {
        // Scroll up by one line (8 pixels)
        // Copy screen content up
        for (uint32_t y = char_height; y < fb.height; y++) {
            for (uint32_t x = 0; x < fb.width; x++) {
                fb.base[(y - char_height) * fb.width + x] = fb.base[y * fb.width + x];
            }
        }
        
        // Clear the bottom line
        for (uint32_t y = fb.height - char_height; y < fb.height; y++) {
            for (uint32_t x = 0; x < fb.width; x++) {
                fb.base[y * fb.width + x] = BLACK;
            }
        }
        
        // Move cursor back up one line
        cursor.y -= char_height;
        scroll_offset++;
    }
}

int auto_scroll_get_offset(void) {
    return scroll_offset;
}