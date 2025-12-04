#include "print.h"
#include "string_helpers.h"
#include "IO.h"
#include "sleep.h"
#include "anthropic.h"
typedef unsigned long long ULL_t;

#define MOUSE_CHECK 0x64
#define MOUSE_AUXILIARY_PORT 0x60
#define USED 0xACE
#define ON 0x1
#define OFF 0x0

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

short status;
static int cursor_x = SCREEN_WIDTH / 2;
static int cursor_y = SCREEN_HEIGHT / 2;

void mcursor(int x, int y, int prev_x, int prev_y, short signal);
void overwrite_cursor(int prev_x, int prev_y);



void mouse(void) {
    static uint8_t packet[3];
    static uint8_t packet_index = 0;

    if (!status) {
        while (inb(MOUSE_CHECK) & 2);
        outb(0x64, 0xD4);
        outb(MOUSE_AUXILIARY_PORT, 0xF4);

        while (!(inb(MOUSE_CHECK) & 1));
        inb(MOUSE_AUXILIARY_PORT);

        status = USED;
    }

    while (inb(MOUSE_CHECK) & 1) {
        packet[packet_index++] = inb(MOUSE_AUXILIARY_PORT);

        if (packet_index == 3) {
            uint8_t b1 = packet[0];
            uint8_t b2 = packet[1];
            uint8_t b3 = packet[2];

            int delta_x = b2;
            int delta_y = b3;

            if (b1 & 0x10) delta_x |= 0xFFFFFF00;
            if (b1 & 0x20) delta_y |= 0xFFFFFF00;

            delta_y = -delta_y;

            int prev_x = cursor_x;
            int prev_y = cursor_y;

            cursor_x += delta_x;
            cursor_y += delta_y;

            if (cursor_x < 0) cursor_x = 0;
            if (cursor_y < 0) cursor_y = 0;
            if (cursor_x >= SCREEN_WIDTH - 16) cursor_x = SCREEN_WIDTH - 16;
            if (cursor_y >= SCREEN_HEIGHT - 16) cursor_y = SCREEN_HEIGHT - 16;

            if (cursor_x != prev_x || cursor_y != prev_y) {
                overwrite_cursor(prev_x, prev_y);
                mcursor(cursor_x, cursor_y, 0, 0, OFF);
            }

            packet_index = 0;
        }
    }
}


void mcursor(int x, int y, int prev_x, int prev_y, short signal) {
    (void)prev_x;
    (void)prev_y;
    (void)signal;

    int height = 16;
    unsigned int MOUSE_RED = 0xFF0000;

    for (int i = 0; i < height; i++) {
        put_pixel(x, y + i, MOUSE_RED);
    }

    for (int i = 0; i < height; i++) {
        put_pixel(x + i, y + i, MOUSE_RED);
    }

    for (int i = 0; i < height; i++) {
        put_pixel(x + i, y + height - 1, MOUSE_RED);
    }

    for (int yy = 1; yy < height; yy++) {
        for (int xx = 1; xx < yy; xx++) {
            put_pixel(x + xx, y + yy, MOUSE_RED);
        }
    }
}

void overwrite_cursor(int prev_x, int prev_y) {
    int overwrite_height = 16;

    for (int i = 0; i < overwrite_height; i++) {
        put_pixel(prev_x, prev_y + i, 0x000000);
    }

    for (int i = 0; i < overwrite_height; i++) {
        put_pixel(prev_x + i, prev_y + i, 0x000000);
    }

    for (int i = 0; i < overwrite_height; i++) {
        put_pixel(prev_x + i, prev_y + overwrite_height - 1, 0x000000);
    }

    for (int yy = 1; yy < overwrite_height; yy++) {
        for (int xx = 1; xx < yy; xx++) {
            put_pixel(prev_x + xx, prev_y + yy, 0x000000);
        }
    }
}



int get_mouse_x(void) {
    return cursor_x;
}

int get_mouse_y(void) {
    return cursor_y;
}

int get_mouse_button(void) {
        return mouse_button_state;

}