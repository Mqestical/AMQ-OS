
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


    if (cursor.y + char_height >= fb.height) {


        for (uint32_t y = char_height; y < fb.height; y++) {
            for (uint32_t x = 0; x < fb.width; x++) {
                fb.base[(y - char_height) * fb.width + x] = fb.base[y * fb.width + x];
            }
        }


        for (uint32_t y = fb.height - char_height; y < fb.height; y++) {
            for (uint32_t x = 0; x < fb.width; x++) {
                fb.base[y * fb.width + x] = BLACK;
            }
        }


        cursor.y -= char_height;
        scroll_offset++;
    }
}

int auto_scroll_get_offset(void) {
    return scroll_offset;
}